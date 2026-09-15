#include "gfcTarArchive.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <system_error>

namespace
{
	constexpr size_t kBlock = 512;

	void setErr(std::string* err, const std::string& msg)
	{
		if (err) *err = msg;
	}

	// Zero-padded octal into a field of `width` bytes, NUL-terminated.
	void putOctal(char* field, size_t width, uint64_t value)
	{
		std::string digits(width - 1, '0');
		for (size_t i = width - 1; i-- > 0 && value > 0; )
		{
			digits[i] = static_cast<char>('0' + (value & 7));
			value >>= 3;
		}
		std::memcpy(field, digits.data(), width - 1);
		field[width - 1] = '\0';
	}

	bool parseOctal(const char* field, size_t width, uint64_t& out)
	{
		out = 0;
		size_t i = 0;
		while (i < width && field[i] == ' ') ++i;
		bool any = false;
		for (; i < width && field[i] >= '0' && field[i] <= '7'; ++i)
		{
			out = (out << 3) | static_cast<uint64_t>(field[i] - '0');
			any = true;
		}
		for (; i < width; ++i)
		{
			if (field[i] != ' ' && field[i] != '\0') return false;
		}
		return any;
	}

	// Sum of all header bytes with the checksum field counted as spaces.
	uint64_t headerChecksum(const unsigned char* block)
	{
		uint64_t sum = 0;
		for (size_t i = 0; i < kBlock; ++i)
		{
			sum += (i >= 148 && i < 156) ? static_cast<unsigned char>(' ') : block[i];
		}
		return sum;
	}

	std::string fieldString(const char* p, size_t width)
	{
		size_t n = 0;
		while (n < width && p[n] != '\0') ++n;
		return std::string(p, n);
	}

	uint64_t paddedSize(uint64_t size)
	{
		return (size + kBlock - 1) / kBlock * kBlock;
	}
}

bool gfcTar::isSafeName(const std::string& name)
{
	if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos) return false;
	// ':' covers drive-qualified names ("C:/x", "C:x") and NTFS alternate
	// streams ("a.exr:stream"): on Windows `dir / "C:/x"` discards `dir`.
	if (name.find(':') != std::string::npos) return false;
	const std::filesystem::path asPath(name);
	if (asPath.has_root_name() || asPath.has_root_directory()) return false;
	size_t start = 0;
	while (start <= name.size())
	{
		size_t slash = name.find('/', start);
		if (slash == std::string::npos) slash = name.size();
		const std::string segment = name.substr(start, slash - start);
		if (segment.empty() || segment == "." || segment == "..") return false;
		start = slash + 1;
	}
	return true;
}

bool gfcTar::splitName(const std::string& name, std::string& prefix, std::string& base)
{
	if (name.size() <= 100)
	{
		prefix.clear();
		base = name;
		return true;
	}
	if (name.size() > 255) return false;
	for (size_t p = std::min<size_t>(155, name.size() - 1); p > 0; --p)
	{
		const size_t baseLen = name.size() - p - 1;
		if (name[p] == '/' && baseLen > 0 && baseLen <= 100)
		{
			prefix = name.substr(0, p);
			base = name.substr(p + 1);
			return true;
		}
	}
	return false;
}

bool gfcTar::Writer::open(const std::string& path, std::string* err)
{
	out.open(path, std::ios::binary | std::ios::trunc);
	pos = 0;
	inEntry = false;
	if (!out)
	{
		setErr(err, "cannot write " + path);
		return false;
	}
	return true;
}

bool gfcTar::Writer::writeRaw(const char* data, size_t len, std::string* err)
{
	out.write(data, static_cast<std::streamsize>(len));
	if (!out)
	{
		setErr(err, "write failed");
		return false;
	}
	pos += len;
	return true;
}

bool gfcTar::Writer::beginEntry(const std::string& name, uint64_t size, std::string* err)
{
	if (inEntry) { setErr(err, "an entry is still open: " + name); return false; }
	if (!isSafeName(name)) { setErr(err, "unsafe entry name: " + name); return false; }
	if (size > kMaxEntrySize) { setErr(err, "entry larger than 8 GiB - 1: " + name); return false; }
	std::string prefix, base;
	if (!splitName(name, prefix, base)) { setErr(err, "entry name too long for the tar format: " + name); return false; }

	char header[kBlock];
	std::memset(header, 0, sizeof(header));
	std::memcpy(header, base.data(), base.size());
	putOctal(header + 100, 8, 0644);
	putOctal(header + 108, 8, 0);
	putOctal(header + 116, 8, 0);
	putOctal(header + 124, 12, size);
	putOctal(header + 136, 12, static_cast<uint64_t>(time(nullptr)));
	header[156] = '0';
	std::memcpy(header + 257, "ustar", 6);   // "ustar" and its NUL
	std::memcpy(header + 263, "00", 2);
	std::memcpy(header + 345, prefix.data(), prefix.size());
	putOctal(header + 148, 7, headerChecksum(reinterpret_cast<const unsigned char*>(header)));   // 6 digits + NUL
	header[155] = ' ';
	if (!writeRaw(header, sizeof(header), err)) return false;

	inEntry = true;
	entrySize = size;
	entryWritten = 0;
	return true;
}

bool gfcTar::Writer::write(const char* data, size_t len, std::string* err)
{
	if (!inEntry || entryWritten + len > entrySize)
	{
		setErr(err, "write beyond the entry's declared size");
		return false;
	}
	if (!writeRaw(data, len, err)) return false;
	entryWritten += len;
	return true;
}

bool gfcTar::Writer::endEntry(std::string* err)
{
	if (!inEntry) { setErr(err, "no entry is open"); return false; }
	if (entryWritten != entrySize) { setErr(err, "entry shorter than its declared size"); return false; }
	inEntry = false;
	static const char zeros[kBlock] = {};
	const uint64_t pad = paddedSize(entrySize) - entrySize;
	return pad == 0 || writeRaw(zeros, static_cast<size_t>(pad), err);
}

bool gfcTar::Writer::addBytes(const std::string& name, const std::string& data, std::string* err)
{
	return beginEntry(name, data.size(), err) && write(data.data(), data.size(), err) && endEntry(err);
}

bool gfcTar::Writer::finish(std::string* err)
{
	if (inEntry) { setErr(err, "an entry is still open at finish"); return false; }
	static const char zeros[2 * kBlock] = {};
	if (!writeRaw(zeros, sizeof(zeros), err)) return false;
	out.close();
	if (out.fail()) { setErr(err, "close failed"); return false; }
	return true;
}

void gfcTar::Writer::abandon()
{
	if (out.is_open()) out.close();
	inEntry = false;
}

bool gfcTar::Reader::open(const std::string& path, std::string* err)
{
	list.clear();
	archivePath = path;
	std::ifstream in(path, std::ios::binary);
	if (!in) { setErr(err, "cannot open " + path); return false; }
	std::error_code ec;
	const uint64_t fileSize = std::filesystem::file_size(path, ec);
	if (ec) { setErr(err, "cannot read the size of " + path); return false; }

	uint64_t offset = 0;
	unsigned char block[kBlock];
	while (true)
	{
		if (offset + kBlock > fileSize) { setErr(err, "truncated archive: no end-of-archive marker"); return false; }
		in.seekg(static_cast<std::streamoff>(offset));
		in.read(reinterpret_cast<char*>(block), kBlock);
		if (!in) { setErr(err, "truncated archive"); return false; }
		if (std::all_of(block, block + kBlock, [](unsigned char c) { return c == 0; })) return true;

		const char* h = reinterpret_cast<const char*>(block);
		uint64_t stored = 0;
		if (!parseOctal(h + 148, 8, stored) || stored != headerChecksum(block))
		{
			setErr(err, "corrupt header at offset " + std::to_string(offset));
			return false;
		}
		if (std::memcmp(h + 257, "ustar", 5) != 0) { setErr(err, "not a ustar archive"); return false; }
		uint64_t size = 0;
		if (!parseOctal(h + 124, 12, size)) { setErr(err, "bad size field at offset " + std::to_string(offset)); return false; }

		const std::string base = fieldString(h, 100);
		const std::string prefix = fieldString(h + 345, 155);
		const std::string name = prefix.empty() ? base : prefix + "/" + base;
		const uint64_t dataOffset = offset + kBlock;
		if (dataOffset + paddedSize(size) > fileSize) { setErr(err, "truncated archive: " + name); return false; }
		if (block[156] == '0' || block[156] == '\0')
		{
			if (!isSafeName(name)) { setErr(err, "unsafe entry name: " + name); return false; }
			list.push_back(Entry{name, size, dataOffset});
		}
		offset = dataOffset + paddedSize(size);
	}
}

const gfcTar::Entry* gfcTar::Reader::find(const std::string& name) const
{
	for (const Entry& e : list)
	{
		if (e.name == name) return &e;
	}
	return nullptr;
}

bool gfcTar::Reader::readBytes(const Entry& entry, std::string& out, std::string* err) const
{
	std::ifstream in(archivePath, std::ios::binary);
	if (!in) { setErr(err, "cannot open " + archivePath); return false; }
	in.seekg(static_cast<std::streamoff>(entry.dataOffset));
	out.assign(static_cast<size_t>(entry.size), '\0');
	if (entry.size > 0) in.read(&out[0], static_cast<std::streamsize>(entry.size));
	if (!in) { setErr(err, "read failed: " + entry.name); return false; }
	return true;
}

bool gfcTar::Reader::extractTo(const Entry& entry, const std::string& destPath, std::string* err) const
{
	std::error_code ec;
	const std::filesystem::path dest(destPath);
	if (dest.has_parent_path()) std::filesystem::create_directories(dest.parent_path(), ec);
	std::ifstream in(archivePath, std::ios::binary);
	std::ofstream outFile(destPath, std::ios::binary | std::ios::trunc);
	if (!in || !outFile) { setErr(err, "cannot extract " + entry.name + " to " + destPath); return false; }
	in.seekg(static_cast<std::streamoff>(entry.dataOffset));
	std::vector<char> buf(1 << 20);
	uint64_t left = entry.size;
	while (left > 0)
	{
		const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
		in.read(buf.data(), static_cast<std::streamsize>(n));
		if (!in) { setErr(err, "read failed: " + entry.name); return false; }
		outFile.write(buf.data(), static_cast<std::streamsize>(n));
		if (!outFile) { setErr(err, "write failed: " + destPath); return false; }
		left -= n;
	}
	outFile.close();
	if (outFile.fail()) { setErr(err, "close failed: " + destPath); return false; }
	return true;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int tarSelfTest()
{
	int pass = 0;
	int fail = 0;
	auto check = [&](bool cond, const char* msg)
	{
		if (cond)
		{
			++pass;
		}
		else
		{
			++fail;
			std::fprintf(stderr, "NOTE-TAR FAIL: %s\n", msg);
		}
	};
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path() /
		("jefe_tar_test_" + std::to_string(static_cast<long long>(time(nullptr))));
	fs::create_directories(dir, ec);
	const std::string archive = (dir / "a.tar").string();
	auto readFile = [](const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};
	auto writeFile = [](const std::string& path, const std::string& data)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(data.data(), static_cast<std::streamsize>(data.size()));
	};

	std::string big(3 * 1024 * 1024 + 17, '\0');
	for (size_t i = 0; i < big.size(); ++i)
	{
		big[i] = static_cast<char>((i * 31) % 251);
	}
	const std::string longName = "media/000/" + std::string(100, 'b') + "/" + std::string(89, 'c');
	const std::string tooLong = std::string(150, 'd') + "/" + std::string(149, 'e');
	check(longName.size() == 200 && tooLong.size() == 300, "fixture name lengths");

	check(gfcTar::isSafeName("media/000/x.exr"), "a relative name is safe");
	check(!gfcTar::isSafeName("../evil") && !gfcTar::isSafeName("a/../b") && !gfcTar::isSafeName("/abs") &&
		  !gfcTar::isSafeName("a//b") && !gfcTar::isSafeName("a\\b") && !gfcTar::isSafeName(""),
		  "absolute, parent, empty-segment and backslash names are unsafe");
	// On Windows, std::filesystem's `dir / "C:/..."` discards `dir`, so a
	// drive-qualified name (or an NTFS alternate-stream name) would escape
	// the extraction directory.
	check(!gfcTar::isSafeName("C:/evil.txt"), "a drive-qualified absolute name is unsafe");
	check(!gfcTar::isSafeName("C:evil.txt"), "a drive-relative name is unsafe");
	check(!gfcTar::isSafeName("a.exr:stream"), "an alternate-data-stream name is unsafe");

	std::string err;
	{
		gfcTar::Writer w;
		check(w.open(archive, &err), "writer opens");
		check(w.addBytes("manifest.json", "{\"format\":\"x\"}", &err), "small entry written");
		check(w.addBytes("notes/empty.jnotes", "", &err), "empty entry written");
		check(w.addBytes("media/000/big.bin", big, &err), "multi-megabyte entry written");
		check(w.addBytes(longName, "long", &err), "a 200-byte name is split into prefix and name");
		check(!w.beginEntry(tooLong, 1, &err), "a 300-byte name is refused");
		check(!w.beginEntry("../evil", 1, &err), "an unsafe name is refused");
		check(!w.beginEntry("x", gfcTar::kMaxEntrySize + 1, &err), "an oversize entry is refused");
		check(w.finish(&err), "writer finishes");
	}

	gfcTar::Reader r;
	check(r.open(archive, &err), "reader opens the archive");
	const std::vector<gfcTar::Entry>& e = r.entries();
	check(e.size() == 4, "four entries listed");
	if (e.size() == 4)
	{
		check(e[0].name == "manifest.json" && e[1].name == "notes/empty.jnotes" &&
			  e[2].name == "media/000/big.bin" && e[3].name == longName,
			  "entry order and names preserved");
		std::string got;
		check(r.readBytes(e[0], got, &err) && got == "{\"format\":\"x\"}", "small entry bytes");
		check(r.readBytes(e[1], got, &err) && got.empty(), "empty entry bytes");
		check(e[2].size == big.size(), "big entry size");
		const std::string extracted = (dir / "out" / "big.bin").string();
		check(r.extractTo(e[2], extracted, &err), "big entry extracts into a new directory");
		check(readFile(extracted) == big, "extracted bytes match");
		const gfcTar::Entry* longEntry = r.find(longName);
		check(longEntry && r.readBytes(*longEntry, got, &err) && got == "long", "the long name round-trips");
	}

	const std::string bytes = readFile(archive);
	std::string corrupt = bytes;
	corrupt[0] = static_cast<char>(corrupt[0] ^ 0x20);
	writeFile((dir / "corrupt.tar").string(), corrupt);
	gfcTar::Reader rc;
	check(!rc.open((dir / "corrupt.tar").string(), &err) && err.find("corrupt") != std::string::npos,
		  "a header whose checksum no longer matches is refused");

	writeFile((dir / "truncated.tar").string(), bytes.substr(0, 512 * 6 + 1000));
	gfcTar::Reader rt;
	check(!rt.open((dir / "truncated.tar").string(), &err) && err.find("truncated") != std::string::npos,
		  "an archive cut inside an entry is detected as truncated");

	fs::remove_all(dir, ec);
	std::printf("NOTE-TAR: pass=%d fail=%d\n", pass, fail);
	return fail;
}
