#include "gfcMediaFingerprint.h"

#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <system_error>

#include "gfcNoteStore.h"
#include "gfcSha1.h"

namespace
{
	void setErr(std::string* err, const std::string& msg)
	{
		if (err) *err = msg;
	}

	void putInt(gfcSha1& sha, int64_t v)
	{
		unsigned char bytes[8];
		for (int i = 0; i < 8; ++i)
		{
			bytes[7 - i] = static_cast<unsigned char>(static_cast<uint64_t>(v) >> (8 * i));
		}
		sha.update(bytes, 8);
	}

	// Every 16th row and column of channels 0..2, as native float bytes (all
	// supported platforms are little-endian).
	bool hashFrameSamples(gfcSha1& sha, const std::string& path, std::string* err)
	{
		auto in = OIIO::ImageInput::open(path);
		if (!in)
		{
			setErr(err, "cannot open " + path + ": " + OIIO::geterror());
			return false;
		}
		const OIIO::ImageSpec& spec = in->spec();
		const size_t nch = static_cast<size_t>(spec.nchannels);
		const size_t sampled = std::min<size_t>(nch, 3);
		std::vector<float> row(static_cast<size_t>(spec.width) * nch);
		for (int y = 0; y < spec.height; y += 16)
		{
			if (!in->read_scanlines(0, 0, spec.y + y, spec.y + y + 1, 0, 0, spec.nchannels,
									OIIO::TypeDesc::FLOAT, row.data()))
			{
				setErr(err, "cannot read " + path + ": " + in->geterror());
				return false;
			}
			for (int x = 0; x < spec.width; x += 16)
			{
				const float* px = row.data() + static_cast<size_t>(x) * nch;
				sha.update(reinterpret_cast<const unsigned char*>(px), sizeof(float) * sampled);
			}
		}
		return true;
	}
}

bool gfcMediaFingerprint::probe(const std::string& framePath, Probe& out, std::string* err)
{
	auto in = OIIO::ImageInput::open(framePath);
	if (!in)
	{
		setErr(err, "cannot open " + framePath + ": " + OIIO::geterror());
		return false;
	}
	const OIIO::ImageSpec& spec = in->spec();
	out.width = spec.width;
	out.height = spec.height;
	out.channels.assign(spec.channelnames.begin(), spec.channelnames.end());
	return true;
}

std::string gfcMediaFingerprint::compute(const std::vector<std::string>& framePaths, std::string* err)
{
	if (framePaths.empty())
	{
		setErr(err, "no frames to fingerprint");
		return {};
	}
	Probe first;
	if (!probe(framePaths.front(), first, err)) return {};

	gfcSha1 sha;
	sha.update(std::string("jefecheck-fp1"));
	putInt(sha, first.width);
	putInt(sha, first.height);
	putInt(sha, static_cast<int64_t>(first.channels.size()));
	for (const std::string& name : first.channels)
	{
		sha.update(name);
		putInt(sha, 0);   // separator: names cannot run together
	}
	const size_t n = framePaths.size();
	putInt(sha, static_cast<int64_t>(n));

	std::vector<size_t> samples = {0, n / 2, n - 1};
	samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
	for (size_t i : samples)
	{
		if (!hashFrameSamples(sha, framePaths[i], err)) return {};
	}
	return "fp1:" + sha.hexDigest();
}

std::map<std::string, std::vector<std::string>> gfcMediaFingerprint::sequencesIn(const std::string& dir, bool recursive)
{
	namespace fs = std::filesystem;
	std::map<std::string, std::vector<std::string>> out;
	auto visit = [&out](const fs::directory_entry& entry)
	{
		std::error_code fec;
		if (!entry.is_regular_file(fec)) return;
		const std::string ext = entry.path().extension().string();
		if (ext == ".jnotes" || ext == ".jcs" || ext == ".jcreview" || ext == ".partial") return;
		const std::string path = entry.path().string();
		out[gfcNoteStore::normalisePath(path)].push_back(path);
	};
	std::error_code ec;
	if (recursive)
	{
		for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
			 !ec && it != end; it.increment(ec))
		{
			visit(*it);
		}
	}
	else
	{
		for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			visit(*it);
		}
	}
	for (auto& kv : out)
	{
		std::sort(kv.second.begin(), kv.second.end());
	}
	return out;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int mediaFingerprintSelfTest()
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
			std::fprintf(stderr, "NOTE-FINGERPRINT FAIL: %s\n", msg);
		}
	};
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path() /
		("jefe_fp_test_" + std::to_string(static_cast<long long>(time(nullptr))));
	const int w = 64;
	const int h = 48;

	// A small float RGB EXR with a deterministic pattern; `poke` changes one
	// pixel that the fingerprint samples (row 16, column 16, channel 0).
	auto writeFrame = [&](const fs::path& path, int seed, bool poke)
	{
		fs::create_directories(path.parent_path(), ec);
		std::vector<float> px(static_cast<size_t>(w) * h * 3);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				for (int c = 0; c < 3; ++c)
					px[(static_cast<size_t>(y) * w + x) * 3 + c] =
						static_cast<float>((x * 7 + y * 13 + c * 29 + seed) % 97) / 97.0f;
		if (poke) px[(16 * static_cast<size_t>(w) + 16) * 3] += 0.5f;
		auto out = OIIO::ImageOutput::create(path.string());
		if (!out) return false;
		OIIO::ImageSpec spec(w, h, 3, OIIO::TypeDesc::FLOAT);
		if (!out->open(path.string(), spec)) return false;
		const bool wrote = out->write_image(OIIO::TypeDesc::FLOAT, px.data());
		return out->close() && wrote;
	};

	const fs::path a = dir / "a";
	const fs::path b = dir / "b" / "deep";
	const fs::path c = dir / "c";
	const fs::path d = dir / "d";
	check(writeFrame(a / "shot.0001.exr", 1, false) && writeFrame(a / "shot.0002.exr", 2, false), "fixture sequence written");
	check(writeFrame(b / "renamed.0001.exr", 1, false) && writeFrame(b / "renamed.0002.exr", 2, false), "same pixels under another name written");
	check(writeFrame(c / "shot.0001.exr", 1, false) && writeFrame(c / "shot.0002.exr", 2, true), "one-pixel change written");
	check(writeFrame(d / "shot.0001.exr", 1, false), "single frame written");

	std::string err;
	const std::string fpA = gfcMediaFingerprint::compute({(a / "shot.0001.exr").string(), (a / "shot.0002.exr").string()}, &err);
	const std::string fpB = gfcMediaFingerprint::compute({(b / "renamed.0001.exr").string(), (b / "renamed.0002.exr").string()}, &err);
	const std::string fpC = gfcMediaFingerprint::compute({(c / "shot.0001.exr").string(), (c / "shot.0002.exr").string()}, &err);
	const std::string fpD = gfcMediaFingerprint::compute({(d / "shot.0001.exr").string()}, &err);
	check(fpA.rfind("fp1:", 0) == 0 && fpA.size() == 44, "fp1 prefix and a 40-digit SHA-1");
	check(!fpA.empty() && fpA == fpB, "same pixels under a different path and name give the same fingerprint");
	check(!fpC.empty() && fpA != fpC, "a changed sampled pixel changes the fingerprint");
	check(!fpD.empty() && fpA != fpD, "a different frame count changes the fingerprint");
	err.clear();
	check(gfcMediaFingerprint::compute({(dir / "missing.0001.exr").string()}, &err).empty() && !err.empty(),
		  "a missing frame reports an error");

	gfcMediaFingerprint::Probe p;
	check(gfcMediaFingerprint::probe((a / "shot.0001.exr").string(), p, &err) &&
		  p.width == 64 && p.height == 48 && p.channels.size() == 3,
		  "probe reads size and channels");

	check(gfcMediaFingerprint::sequencesIn(dir.string(), false).empty(),
		  "a non-recursive search of a directory holding only directories finds nothing");
	const auto deep = gfcMediaFingerprint::sequencesIn((dir / "b").string(), true);
	const std::string key = gfcNoteStore::normalisePath((b / "renamed.0001.exr").string());
	check(deep.count(key) == 1 && deep.at(key).size() == 2 && deep.at(key)[0] < deep.at(key)[1],
		  "a recursive search groups a sequence with its frames sorted");

	fs::remove_all(dir, ec);
	std::printf("NOTE-FINGERPRINT: pass=%d fail=%d\n", pass, fail);
	return fail;
}
