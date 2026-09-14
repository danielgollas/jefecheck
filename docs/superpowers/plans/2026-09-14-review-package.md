# Review Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** File → Export Review Package… writes one `.jcreview` file (session, every media's notes, optionally the media, non-bundled LUTs); File → Open Review Package… restores that session's full latest state, finding media that was left out by a pixel fingerprint.

**Architecture:** Pure C++ units do the format work — SHA-1, a ustar tar writer/reader, a media fingerprint, `.jcs` path rewriting, and a union merge of reviews. A QtCore unit (`ReviewPackage_qt`) owns the manifest JSON, an incremental exporter, and an opener that takes its app-side actions as callbacks. The bridge exposes the few manager facts those need; `MainWindow_Qt` gathers inputs, loads the opened session, and hosts the menu actions and an export dialog.

**Tech Stack:** C++20 (`std::filesystem`), OpenImageIO (already linked), vendored xmlParser, Qt 6 Core/Widgets (`QJsonDocument`, `QStandardPaths`, `QDialog`).

**Spec:** `docs/superpowers/specs/2026-09-14-review-package-design.md`

**Depends on:** `docs/superpowers/plans/2026-09-14-review-summary-export.md` being complete — this plan uses `jefe::qt::SessionMedia`, `jefe::qt::getSessionMediaSet()`, and `gfcReviewSummary::isoUtc()` from it.

## Global Constraints

- Work in `/Users/dgollas/projects/jefecheck2` on branch `feature/review-export`. Run the app from the repo root (it finds `FX/` and `fonts/` there).
- Only `src/qt/SequenceLoadBridge_qt.cpp` may include the rendering-chain managers (`gfcplatemanager.h`, `gfctrackmanager.h`, `gfclutmanager.h`, `gfcfxmanager.h`, …). Other `src/qt/*` files call `jefe::qt::*` functions declared in `src/qt/SequenceLoadBridge_qt.h`. Qt files include core headers as `"../gfcX.h"`.
- No new dependencies and no new Qt modules.
- There is no unit-test framework. Pure tests are functions that print `NAME: pass=N fail=N`, print `NAME FAIL: <message>` to stderr per failed check, return the fail count, and are called from the `--notes-test` block in `src/main_qt.cpp`. To add one: declare `const int <x>Fail = <selfTest>();` after the last `const int …Fail = …;` line in that block and append `&& <x>Fail == 0` to the `std::_Exit((…) ? 0 : 2)` condition. GL tests are flags handled after `window.show()` with `QTimer::singleShot(5000, …)`, `fflush(stdout)`, then `std::_Exit(code)` with 0 = pass, 2 = fail.
- New `.cpp` files are picked up by `file(GLOB)` only after reconfiguring: `cmake -S . -B build_qt > /dev/null` before `cmake --build build_qt -j8`. A new `Q_OBJECT` class may also need `rm -rf build_qt/jefecheck_autogen` (generated files only) if moc output is stale.
- Binary: `./build_qt/jefecheck.app/Contents/MacOS/jefecheck`. Baseline: `--notes-test` exits 0.
- GL end-to-end runs need a settings directory that does not open the Load Sequence Manager at startup. Qt's INI format stores the `General` group as `[%General]`:
  `T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini"` then pass `--config-dir "$T"`.
- Test image: `/Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr` (1000×1000 EXR, one frame).
- Style: core `src/gfc*` files use tabs and Allman braces; `src/qt/*` uses 4 spaces and K&R braces. Doc comments are `/** … */`.
- Package format (spec): uncompressed POSIX ustar; entry names are safe relative paths up to 255 bytes; one entry up to 8 GiB − 1; entry order `manifest.json`, `session.jcs`, `notes/NNN.jnotes`, `luts/<file>`, `media/NNN/<frame files>`; `NNN` is the zero-padded media index; manifest `format` = `jefecheck-review-package`, `version` = 1; fingerprint = `fp1:` + SHA-1 hex.
- Output files are written to `<path>.partial` and renamed on success; nothing partial is left on failure or cancel.
- xmlParser: `XMLNode::parseString(text, tag, &results)`; strings returned by `createXMLString` are released with `free()` (this xmlParser has no `freeXMLString`); `XMLCSTR` is `const char*`.
- Bash commands must be single-line (no embedded newlines; use `&&`/`;`).
- Commits: stage the task's files explicitly (the repo has many untracked files — never `git add -A` or `git add .`). Write the message to a file under `/private/tmp/claude-501/-Users-dgollas-projects-jefecheck2/08ddc330-7d2c-4dfd-ab4b-8499ea2a66ca/scratchpad/` and `git commit -F <file>`. Subject `JEF-39: <plain-English summary>`, a short body saying why, and these exact last two lines whichever model writes the commit:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_0139ixvqjdzvAuwuFUVcBz37`
- Do not push.

## File Structure

| File | Status | Responsibility |
|------|--------|----------------|
| `src/gfcSha1.{h,cpp}` | create | Incremental SHA-1 + `NOTE-SHA1` self-test. |
| `src/gfcNoteStore.{h,cpp}` | modify | Use `gfcSha1`; add `toXmlString` / `fromXmlString`. |
| `src/gfcTarArchive.{h,cpp}` | create | ustar `gfcTar::Writer` / `gfcTar::Reader` + `NOTE-TAR`. |
| `src/gfcMediaFingerprint.{h,cpp}` | create | `probe`, `compute`, `sequencesIn` + `NOTE-FINGERPRINT`. |
| `src/gfcSessionPaths.{h,cpp}` | create | List/rewrite `.jcs` media paths; list LUT and FX names + `NOTE-SESSIONPATHS`. |
| `src/gfcNoteMerge.{h,cpp}` | create | Union merge by revision/note id + `NOTE-MERGE`. |
| `src/qt/ReviewPackage_qt.{h,cpp}` | create | Manifest JSON, `Exporter`, `openPackage`, `findByFingerprint` + `NOTE-PACKAGE`, `NOTE-PACKAGE-OPEN`. |
| `src/qt/ReviewPackageDialog_qt.{h,cpp}` | create | Export dialog (path, Include media + size, progress, cancel). |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}` | modify | Frames of a sequence, review XML/fingerprint, LUT source path, install-LUT check, review reload, search paths, FX loaded. |
| `src/qt/MainWindow_qt.{h,cpp}` | modify | Gather export input, export/open package, title, menu actions, headless tests. |
| `src/main_qt.cpp` | modify | Self-tests; `--package-test`, `--relink-test`, `--package-dialog-test`, `--export-package`, `--open-package`. |

---

### Task 1: Shared SHA-1

**Files:**
- Create: `src/gfcSha1.h`, `src/gfcSha1.cpp`
- Modify: `src/gfcNoteStore.cpp` (remove the file-local `Sha1State`/`sha1Hex` block, currently lines ~39–154; its one use is in `fallbackSidecarPath`, ~line 187)
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Produces: `class gfcSha1 { public: gfcSha1(); void update(const unsigned char* data, size_t len); void update(const std::string& data); std::string hexDigest(); static std::string hex(const std::string& data); };` and `int sha1SelfTest();`

- [ ] **Step 1: Write the header**

Create `src/gfcSha1.h`:

```cpp
#ifndef GFCSHA1_H
#define GFCSHA1_H

#include <cstddef>
#include <cstdint>
#include <string>

/**
	@brief SHA-1 (FIPS PUB 180-1), fed incrementally. Used for identity --
	fallback sidecar names, media fingerprints, package cache ids -- not for
	security.
*/
class gfcSha1
{
	public:
		gfcSha1();

		void update(const unsigned char* data, size_t len);
		void update(const std::string& data);

		/** Finishes the digest and returns it as 40 lowercase hex digits. Further
		    update() calls are ignored; calling this again returns the same value. */
		std::string hexDigest();

		/** One-shot digest of `data`. */
		static std::string hex(const std::string& data);

	private:
		void processBlock(const unsigned char* block);

		uint32_t h[5];
		uint64_t bitLen;
		unsigned char buffer[64];
		size_t bufferLen;
		bool finished;
		std::string digestHex;
};

/** FIPS 180 vectors; prints NOTE-SHA1: pass=N fail=N, returns the fail count. */
int sha1SelfTest();

#endif
```

- [ ] **Step 2: Write the self-test with stub methods**

Create `src/gfcSha1.cpp`:

```cpp
#include "gfcSha1.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

gfcSha1::gfcSha1()
	: h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u}
	, bitLen(0)
	, buffer{}
	, bufferLen(0)
	, finished(false)
{
}

void gfcSha1::processBlock(const unsigned char*) {}
void gfcSha1::update(const unsigned char*, size_t) {}
void gfcSha1::update(const std::string&) {}
std::string gfcSha1::hexDigest() { return {}; }
std::string gfcSha1::hex(const std::string&) { return {}; }

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int sha1SelfTest()
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
			std::fprintf(stderr, "NOTE-SHA1 FAIL: %s\n", msg);
		}
	};

	const std::string msg56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	check(gfcSha1::hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "empty message");
	check(gfcSha1::hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "abc");
	check(gfcSha1::hex(msg56) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "the 56-byte message");

	gfcSha1 chunked;
	for (size_t i = 0; i < msg56.size(); i += 7)
	{
		chunked.update(msg56.substr(i, 7));
	}
	check(chunked.hexDigest() == "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "chunked updates equal the one-shot digest");

	gfcSha1 million;
	const std::string thousand(1000, 'a');
	for (int i = 0; i < 1000; ++i)
	{
		million.update(thousand);
	}
	const std::string m = million.hexDigest();
	check(m == "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "one million a");
	check(million.hexDigest() == m, "the digest is stable once finished");

	std::printf("NOTE-SHA1: pass=%d fail=%d\n", pass, fail);
	return fail;
}
```

In `src/main_qt.cpp`, add `#include "gfcSha1.h"` beside the other core includes and add `sha1SelfTest()` to the `--notes-test` block as described in Global Constraints.

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-SHA1'`
Expected: `NOTE-SHA1 FAIL:` lines for the five digest checks and `NOTE-SHA1: pass=1 fail=5` (only "stable once finished" passes, comparing two empty strings).

- [ ] **Step 4: Implement**

Replace the five stub lines in `src/gfcSha1.cpp` with:

```cpp
namespace
{
	uint32_t rol(uint32_t v, int bits)
	{
		return (v << bits) | (v >> (32 - bits));
	}
}

void gfcSha1::processBlock(const unsigned char* p)
{
	uint32_t w[80];
	for (int i = 0; i < 16; ++i)
	{
		w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
			   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
			   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
			   (static_cast<uint32_t>(p[i * 4 + 3]));
	}
	for (int i = 16; i < 80; ++i)
	{
		w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	}

	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
	for (int i = 0; i < 80; ++i)
	{
		uint32_t f, k;
		if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999u; }
		else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
		else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }

		uint32_t temp = rol(a, 5) + f + e + k + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = temp;
	}

	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void gfcSha1::update(const unsigned char* data, size_t len)
{
	if (finished) return;
	bitLen += static_cast<uint64_t>(len) * 8;
	while (len > 0)
	{
		size_t take = std::min(len, sizeof(buffer) - bufferLen);
		std::memcpy(buffer + bufferLen, data, take);
		bufferLen += take;
		data += take;
		len -= take;
		if (bufferLen == sizeof(buffer))
		{
			processBlock(buffer);
			bufferLen = 0;
		}
	}
}

void gfcSha1::update(const std::string& data)
{
	update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

std::string gfcSha1::hexDigest()
{
	if (finished) return digestHex;

	// Merkle-Damgard padding: 0x80, zeros to 56 mod 64, then the ORIGINAL bit
	// length as a big-endian 64-bit integer. Capture the length before the
	// padding bytes go through update() and grow it.
	const uint64_t originalBitLen = bitLen;
	unsigned char pad = 0x80;
	update(&pad, 1);
	unsigned char zero = 0x00;
	while (bufferLen != 56)
	{
		update(&zero, 1);
	}
	for (int i = 0; i < 8; ++i)
	{
		buffer[63 - i] = static_cast<unsigned char>(originalBitLen >> (8 * i));
	}
	processBlock(buffer);
	bufferLen = 0;

	static const char hexDigits[] = "0123456789abcdef";
	std::string out;
	out.reserve(40);
	for (int i = 0; i < 5; ++i)
	{
		for (int shift = 24; shift >= 0; shift -= 8)
		{
			const unsigned char byte = static_cast<unsigned char>(h[i] >> shift);
			out.push_back(hexDigits[(byte >> 4) & 0xF]);
			out.push_back(hexDigits[byte & 0xF]);
		}
	}
	finished = true;
	digestHex = out;
	return digestHex;
}

std::string gfcSha1::hex(const std::string& data)
{
	gfcSha1 sha;
	sha.update(data);
	return sha.hexDigest();
}
```

- [ ] **Step 5: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-SHA1: pass=6 fail=0`, every other `NOTE-*` line `fail=0`.

- [ ] **Step 6: Switch the notes store to the shared unit**

In `src/gfcNoteStore.cpp`: add `#include "gfcSha1.h"` with the other includes; delete the whole block from the comment `// ---- minimal self-contained SHA-1 ---…` through the closing brace of `std::string sha1Hex(const std::string& input)`; change the one call in `fallbackSidecarPath` from `sha1Hex(normalisedPath)` to `gfcSha1::hex(normalisedPath)`. Remove includes that only the deleted block used (check `<cstring>`/`<cstdint>` are still needed elsewhere in the file before removing them).

- [ ] **Step 7: Build and run — the store self-test is unchanged**

Run the command from Step 5.
Expected: `NOTE-STORE: pass=34 fail=0` (same as before the change) and `NOTE-SHA1: pass=6 fail=0`; `--notes-test` exits 0.

- [ ] **Step 8: Commit**

`git add src/gfcSha1.h src/gfcSha1.cpp src/gfcNoteStore.cpp src/main_qt.cpp` and commit with subject `JEF-39: one SHA-1 for the notes store and what comes next`.

---

### Task 2: ustar archive writer and reader

**Files:**
- Create: `src/gfcTarArchive.h`, `src/gfcTarArchive.cpp`
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Produces (namespace `gfcTar`): `constexpr uint64_t kMaxEntrySize`; `struct Entry { std::string name; uint64_t size; uint64_t dataOffset; }`; `bool isSafeName(const std::string&)`; `bool splitName(const std::string& name, std::string& prefix, std::string& base)`; `class Writer { bool open(path, err); bool beginEntry(name, size, err); bool write(const char*, size_t, err); bool endEntry(err); bool addBytes(name, data, err); bool finish(err); void abandon(); uint64_t bytesWritten() const; }`; `class Reader { bool open(path, err); const std::vector<Entry>& entries() const; const Entry* find(name) const; bool readBytes(const Entry&, std::string& out, err) const; bool extractTo(const Entry&, const std::string& destPath, err) const; }`; all `err` parameters are `std::string*` and may be null. Global `int tarSelfTest();`

- [ ] **Step 1: Write the header**

Create `src/gfcTarArchive.h`:

```cpp
#ifndef GFCTARARCHIVE_H
#define GFCTARARCHIVE_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/**
	@brief Minimal POSIX ustar archive writer and reader for review packages:
	regular files only, uncompressed, streamed. Names are safe relative paths
	up to 255 bytes (ustar prefix + name); one entry holds up to 8 GiB - 1.
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcTar
{
	constexpr uint64_t kMaxEntrySize = 077777777777ULL;   // 11 octal digits

	struct Entry
	{
		std::string name;
		uint64_t size = 0;
		uint64_t dataOffset = 0;   // where the entry's bytes start in the archive
	};

	/** Relative, no leading '/', no backslash, no empty, "." or ".." segment. */
	bool isSafeName(const std::string& name);

	/** Splits a name into the ustar prefix (<= 155) and name (<= 100) fields;
	    false when it cannot fit. */
	bool splitName(const std::string& name, std::string& prefix, std::string& base);

	class Writer
	{
		public:
			bool open(const std::string& path, std::string* err);
			bool beginEntry(const std::string& name, uint64_t size, std::string* err);
			bool write(const char* data, size_t len, std::string* err);
			bool endEntry(std::string* err);
			/** beginEntry + write + endEntry for an in-memory entry. */
			bool addBytes(const std::string& name, const std::string& data, std::string* err);
			/** Writes the two end-of-archive blocks and closes. */
			bool finish(std::string* err);
			/** Closes without finishing; the caller deletes the file. */
			void abandon();
			uint64_t bytesWritten() const { return pos; }

		private:
			bool writeRaw(const char* data, size_t len, std::string* err);

			std::ofstream out;
			uint64_t pos = 0;
			uint64_t entrySize = 0;
			uint64_t entryWritten = 0;
			bool inEntry = false;
	};

	class Reader
	{
		public:
			/** Reads and validates every header; false for a corrupt, truncated,
			    non-ustar or unsafe-named archive. */
			bool open(const std::string& path, std::string* err);
			const std::vector<Entry>& entries() const { return list; }
			const Entry* find(const std::string& name) const;
			bool readBytes(const Entry& entry, std::string& out, std::string* err) const;
			/** Streams the entry to destPath, creating parent directories. */
			bool extractTo(const Entry& entry, const std::string& destPath, std::string* err) const;

		private:
			std::string archivePath;
			std::vector<Entry> list;
	};
}

/** Prints NOTE-TAR: pass=N fail=N; returns the fail count. */
int tarSelfTest();

#endif
```

- [ ] **Step 2: Write the self-test with stub implementations**

Create `src/gfcTarArchive.cpp`:

```cpp
#include "gfcTarArchive.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iterator>
#include <system_error>

bool gfcTar::isSafeName(const std::string&) { return false; }
bool gfcTar::splitName(const std::string&, std::string&, std::string&) { return false; }
bool gfcTar::Writer::open(const std::string&, std::string*) { return false; }
bool gfcTar::Writer::writeRaw(const char*, size_t, std::string*) { return false; }
bool gfcTar::Writer::beginEntry(const std::string&, uint64_t, std::string*) { return false; }
bool gfcTar::Writer::write(const char*, size_t, std::string*) { return false; }
bool gfcTar::Writer::endEntry(std::string*) { return false; }
bool gfcTar::Writer::addBytes(const std::string&, const std::string&, std::string*) { return false; }
bool gfcTar::Writer::finish(std::string*) { return false; }
void gfcTar::Writer::abandon() {}
bool gfcTar::Reader::open(const std::string&, std::string*) { return false; }
const gfcTar::Entry* gfcTar::Reader::find(const std::string&) const { return nullptr; }
bool gfcTar::Reader::readBytes(const Entry&, std::string&, std::string*) const { return false; }
bool gfcTar::Reader::extractTo(const Entry&, const std::string&, std::string*) const { return false; }

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
```

Add `#include "gfcTarArchive.h"` to `src/main_qt.cpp` and `tarSelfTest()` to the `--notes-test` block.

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-TAR'`
Expected: `NOTE-TAR FAIL:` lines and a non-zero `fail=` count.

- [ ] **Step 4: Implement**

Replace the stub lines in `src/gfcTarArchive.cpp` with:

```cpp
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
```

- [ ] **Step 5: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-TAR: pass=23 fail=0`; every other `NOTE-*` line `fail=0`; exit 0.
Cross-check the format with the system `tar`: temporarily comment out the final `fs::remove_all(dir, ec);`, rebuild, run `--notes-test`, run `tar -tvf "$(ls -dt ${TMPDIR:-/tmp}/jefe_tar_test_* | head -1)/a.tar"` and confirm it lists the four entries with the right sizes, then restore the line and rebuild. Record the listing in the report.

- [ ] **Step 6: Commit**

`git add src/gfcTarArchive.h src/gfcTarArchive.cpp src/main_qt.cpp` and commit with subject `JEF-39: write and read the tar a review package lives in`.

---

### Task 3: Media fingerprint

**Files:**
- Create: `src/gfcMediaFingerprint.h`, `src/gfcMediaFingerprint.cpp`
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Consumes: Task 1 `gfcSha1`; `gfcNoteStore::normalisePath(const std::string&)`; OpenImageIO (`#include <OpenImageIO/imageio.h>`, as `src/gfcNoteStamp.cpp` does).
- Produces (namespace `gfcMediaFingerprint`): `struct Probe { int width; int height; std::vector<std::string> channels; }`; `bool probe(const std::string& framePath, Probe& out, std::string* err)`; `std::string compute(const std::vector<std::string>& framePaths, std::string* err)` (empty on failure); `std::map<std::string, std::vector<std::string>> sequencesIn(const std::string& dir, bool recursive)`; global `int mediaFingerprintSelfTest();`

- [ ] **Step 1: Write the header**

Create `src/gfcMediaFingerprint.h`:

```cpp
#ifndef GFCMEDIAFINGERPRINT_H
#define GFCMEDIAFINGERPRINT_H

#include <map>
#include <string>
#include <vector>

/**
	@brief Content identity for a media sequence, independent of its file names
	and location, so a review package can find moved media again. "fp1:" +
	SHA-1 over the tag "jefecheck-fp1", the first frame's width, height and
	channel names, the frame count, and every 16th row and column (channels
	0-2, as float) of the first, middle and last frames.
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcMediaFingerprint
{
	struct Probe
	{
		int width = 0;
		int height = 0;
		std::vector<std::string> channels;
	};

	/** Reads a frame's header only, no pixels. */
	bool probe(const std::string& framePath, Probe& out, std::string* err);

	/** The fingerprint of the sequence whose frames, in frame order, are
	    `framePaths`. Empty (with `err` set) on failure. */
	std::string compute(const std::vector<std::string>& framePaths, std::string* err);

	/** Every file sequence in `dir` (and below when `recursive`), keyed by
	    gfcNoteStore::normalisePath, frames sorted by path. Skips JefeCheck's own
	    .jnotes, .jcs, .jcreview and .partial files. */
	std::map<std::string, std::vector<std::string>> sequencesIn(const std::string& dir, bool recursive);
}

/** Prints NOTE-FINGERPRINT: pass=N fail=N; returns the fail count. */
int mediaFingerprintSelfTest();

#endif
```

- [ ] **Step 2: Write the self-test with stub implementations**

Create `src/gfcMediaFingerprint.cpp`:

```cpp
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

bool gfcMediaFingerprint::probe(const std::string&, Probe&, std::string*) { return false; }
std::string gfcMediaFingerprint::compute(const std::vector<std::string>&, std::string*) { return {}; }
std::map<std::string, std::vector<std::string>> gfcMediaFingerprint::sequencesIn(const std::string&, bool) { return {}; }

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
```

Add `#include "gfcMediaFingerprint.h"` to `src/main_qt.cpp` and `mediaFingerprintSelfTest()` to the `--notes-test` block.

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-FINGERPRINT'`
Expected: the four fixture checks and the "non-recursive" check pass; the rest fail: `NOTE-FINGERPRINT: pass=5 fail=7`.

- [ ] **Step 4: Implement**

Replace the three stub lines in `src/gfcMediaFingerprint.cpp` with:

```cpp
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
```

- [ ] **Step 5: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-FINGERPRINT: pass=12 fail=0`; every other `NOTE-*` line `fail=0`; exit 0.
Sanity-check on a real file: add nothing to the code — instead confirm `compute` handles the test image by temporarily calling it from the self-test on `/Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr`, print the value, and remove the temporary call before committing. Record the value and the time it took in the report.

- [ ] **Step 6: Commit**

`git add src/gfcMediaFingerprint.h src/gfcMediaFingerprint.cpp src/main_qt.cpp` and commit with subject `JEF-39: fingerprint media by its pixels, so a package can find it again`.

---

### Task 4: Session paths, LUT names and FX names

**Files:**
- Create: `src/gfcSessionPaths.h`, `src/gfcSessionPaths.cpp`
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Consumes: vendored xmlParser (`src/xmlParser.h`): `XMLNode::parseString(const char*, const char* tag, XMLResults*)`, `XMLResults::error`, `eXMLErrorNone`, `getChildNode(name)`, `getChildNode(name, i)`, `getChildNode(i)`, `nChildNode(name)`, `nChildNode()`, `getName()`, `getAttribute(name)`, `updateAttribute(newValue, NULL, oldName)`, `isEmpty()`, `createXMLString(int nFormat, int* size)` (release with `free()`).
- Produces (namespace `gfcSessionPaths`): `enum class Kind { Track, Playlist }`; `struct MediaRef { Kind kind; int index; int sub; std::string path; }`; `bool listMedia(const std::string& jcsXml, std::vector<MediaRef>& out, std::string* err)`; `bool listLutNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err)`; `bool listFxNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err)`; `bool rewriteMedia(const std::string& jcsXml, const std::map<std::string, std::string>& mapping, std::string& out, std::string* err)`; global `int sessionPathsSelfTest();`

- [ ] **Step 1: Write the header**

Create `src/gfcSessionPaths.h`:

```cpp
#ifndef GFCSESSIONPATHS_H
#define GFCSESSIONPATHS_H

#include <map>
#include <string>
#include <vector>

/**
	@brief Reads and rewrites what a .jcs session references outside itself:
	media paths (tracks/track@filename, playlist/e/t@fn), LUT names (plate@lut,
	FX widgets of type cube or lut) and FX names (FX@name).
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcSessionPaths
{
	enum class Kind { Track, Playlist };

	struct MediaRef
	{
		Kind kind = Kind::Track;
		int index = 0;       // position among <track>s, or among playlist <e>s
		int sub = -1;        // position of the <t> inside its <e>; -1 for tracks
		std::string path;
	};

	/** Non-empty track filenames in document order, then playlist entry tracks. */
	bool listMedia(const std::string& jcsXml, std::vector<MediaRef>& out, std::string* err);

	/** Unique LUT names in document order; "" and "no LUT" are not names. */
	bool listLutNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err);

	/** Unique FX names in document order. */
	bool listFxNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err);

	/** The document with every media path that is a key of `mapping` replaced by
	    its value; every other attribute keeps its value. */
	bool rewriteMedia(const std::string& jcsXml, const std::map<std::string, std::string>& mapping,
					  std::string& out, std::string* err);
}

/** Prints NOTE-SESSIONPATHS: pass=N fail=N; returns the fail count. */
int sessionPathsSelfTest();

#endif
```

- [ ] **Step 2: Write the self-test with stub implementations**

Create `src/gfcSessionPaths.cpp`:

```cpp
#include "gfcSessionPaths.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "xmlParser.h"

bool gfcSessionPaths::listMedia(const std::string&, std::vector<MediaRef>&, std::string*) { return false; }
bool gfcSessionPaths::listLutNames(const std::string&, std::vector<std::string>&, std::string*) { return false; }
bool gfcSessionPaths::listFxNames(const std::string&, std::vector<std::string>&, std::string*) { return false; }
bool gfcSessionPaths::rewriteMedia(const std::string&, const std::map<std::string, std::string>&, std::string&, std::string*) { return false; }

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int sessionPathsSelfTest()
{
	using namespace gfcSessionPaths;
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
			std::fprintf(stderr, "NOTE-SESSIONPATHS FAIL: %s\n", msg);
		}
	};

	// The shape gfcSessionManager::saveSession writes, trimmed to what matters.
	const std::string xml = R"xml(<?xml version="1.0"?>
<root comment="fixture">
  <settings framingMode="1" from="1" to="10"/>
  <plates>
    <plate plateID="0" trackID="0" gamma="1.2" lut="show_look.cube">
      <stack><FXS><FX name="Grade" menuName="Grade" hash="h1" active="1">
        <widget varName="lut" type="cube" value="grade_a.cube" group="g"/>
        <widget varName="amount" type="float" value="0.5" group="g"/>
      </FX></FXS></stack>
    </plate>
    <plate plateID="1" trackID="1" gamma="1" lut="no LUT"/>
  </plates>
  <tracks>
    <track trackID="0" filename="/shots/a/sh010.0001.exr" from="1" to="10"/>
    <track trackID="1" filename="" from="1" to="1"/>
  </tracks>
  <playlist>
    <e>
      <t fn="/shots/b/sh020.0001.exr" fr="1" to="5"/>
      <t fn="/shots/a/sh010.0001.exr" fr="1" to="10"/>
      <FXS><FX name="Look" menuName="Look" hash="h2" active="1"><widget varName="l" type="lut" value="film.lut" group="g"/></FX><FX name="Grade" menuName="Grade" hash="h1" active="1"/></FXS>
    </e>
  </playlist>
</root>
)xml";

	std::string err;
	std::vector<MediaRef> refs;
	check(listMedia(xml, refs, &err), "media listed");
	check(refs.size() == 3, "empty track filenames are skipped");
	if (refs.size() == 3)
	{
		check(refs[0].kind == Kind::Track && refs[0].index == 0 && refs[0].sub == -1 &&
			  refs[0].path == "/shots/a/sh010.0001.exr", "the track path");
		check(refs[1].kind == Kind::Playlist && refs[1].index == 0 && refs[1].sub == 0 &&
			  refs[1].path == "/shots/b/sh020.0001.exr", "the first playlist track");
		check(refs[2].kind == Kind::Playlist && refs[2].index == 0 && refs[2].sub == 1 &&
			  refs[2].path == "/shots/a/sh010.0001.exr", "the second playlist track");
	}

	std::vector<std::string> luts;
	check(listLutNames(xml, luts, &err) &&
		  luts == std::vector<std::string>{"show_look.cube", "grade_a.cube", "film.lut"},
		  "LUT names from plates and cube/lut widgets, unique, in document order");
	std::vector<std::string> fx;
	check(listFxNames(xml, fx, &err) && fx == std::vector<std::string>{"Grade", "Look"},
		  "FX names, unique, in document order");

	std::string rewritten;
	check(rewriteMedia(xml, {{"/shots/a/sh010.0001.exr", "media/000/sh010.0001.exr"}}, rewritten, &err),
		  "media rewritten");
	std::vector<MediaRef> after;
	check(listMedia(rewritten, after, &err) && after.size() == 3 &&
		  after[0].path == "media/000/sh010.0001.exr" && after[1].path == "/shots/b/sh020.0001.exr" &&
		  after[2].path == "media/000/sh010.0001.exr",
		  "mapped paths are replaced everywhere, others kept");
	check(rewritten.find("gamma=\"1.2\"") != std::string::npos && rewritten.find("to=\"10\"") != std::string::npos &&
		  rewritten.find("value=\"0.5\"") != std::string::npos && rewritten.find("comment=\"fixture\"") != std::string::npos,
		  "untouched attributes keep their values");
	std::vector<std::string> lutsAfter;
	check(listLutNames(rewritten, lutsAfter, &err) && lutsAfter == luts, "LUT names survive a rewrite");

	err.clear();
	check(!listMedia("<other/>", refs, &err) && !err.empty(), "a document without <root> is refused");

	std::printf("NOTE-SESSIONPATHS: pass=%d fail=%d\n", pass, fail);
	return fail;
}
```

Add `#include "gfcSessionPaths.h"` to `src/main_qt.cpp` and `sessionPathsSelfTest()` to the `--notes-test` block.

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-SESSIONPATHS'`
Expected: `NOTE-SESSIONPATHS FAIL:` lines; `NOTE-SESSIONPATHS: pass=0 fail=9`.

- [ ] **Step 4: Implement**

Replace the four stub lines in `src/gfcSessionPaths.cpp` with:

```cpp
namespace
{
	void setErr(std::string* err, const std::string& msg)
	{
		if (err) *err = msg;
	}

	bool parseRoot(const std::string& xml, XMLNode& top, XMLNode& root, std::string* err)
	{
		XMLResults results;
		top = XMLNode::parseString(xml.c_str(), NULL, &results);
		if (results.error != eXMLErrorNone)
		{
			setErr(err, "session XML does not parse (xmlParser error " + std::to_string(static_cast<int>(results.error)) + ")");
			return false;
		}
		root = top.getChildNode("root");
		if (root.isEmpty())
		{
			setErr(err, "session XML has no <root>");
			return false;
		}
		return true;
	}

	// Calls fn(node, attributeName, kind, index, sub) for every media reference.
	template <typename Fn>
	void forEachMediaAttribute(XMLNode root, Fn fn)
	{
		XMLNode tracks = root.getChildNode("tracks");
		const int trackCount = tracks.isEmpty() ? 0 : tracks.nChildNode("track");
		for (int i = 0; i < trackCount; ++i)
		{
			fn(tracks.getChildNode("track", i), "filename", gfcSessionPaths::Kind::Track, i, -1);
		}
		XMLNode playlist = root.getChildNode("playlist");
		const int entryCount = playlist.isEmpty() ? 0 : playlist.nChildNode("e");
		for (int i = 0; i < entryCount; ++i)
		{
			XMLNode entry = playlist.getChildNode("e", i);
			const int tCount = entry.nChildNode("t");
			for (int k = 0; k < tCount; ++k)
			{
				fn(entry.getChildNode("t", k), "fn", gfcSessionPaths::Kind::Playlist, i, k);
			}
		}
	}

	void addUnique(std::vector<std::string>& out, XMLCSTR value)
	{
		if (!value) return;
		const std::string name(value);
		if (name.empty() || name == "no LUT") return;
		if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
	}

	// Depth-first, document order.
	template <typename Fn>
	void walk(XMLNode node, Fn fn)
	{
		fn(node);
		const int n = node.nChildNode();
		for (int i = 0; i < n; ++i)
		{
			walk(node.getChildNode(i), fn);
		}
	}

	bool nameIs(XMLNode node, const char* tag)
	{
		XMLCSTR name = node.getName();
		return name && std::strcmp(name, tag) == 0;
	}
}

bool gfcSessionPaths::listMedia(const std::string& jcsXml, std::vector<MediaRef>& out, std::string* err)
{
	out.clear();
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	forEachMediaAttribute(root, [&out](XMLNode node, const char* attribute, Kind kind, int index, int sub)
	{
		XMLCSTR value = node.getAttribute(attribute);
		if (!value || !*value) return;
		MediaRef ref;
		ref.kind = kind;
		ref.index = index;
		ref.sub = sub;
		ref.path = value;
		out.push_back(ref);
	});
	return true;
}

bool gfcSessionPaths::listLutNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err)
{
	out.clear();
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	walk(root, [&out](XMLNode node)
	{
		if (nameIs(node, "plate"))
		{
			addUnique(out, node.getAttribute("lut"));
		}
		else if (nameIs(node, "widget"))
		{
			XMLCSTR type = node.getAttribute("type");
			if (type && (std::strcmp(type, "cube") == 0 || std::strcmp(type, "lut") == 0))
			{
				addUnique(out, node.getAttribute("value"));
			}
		}
	});
	return true;
}

bool gfcSessionPaths::listFxNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err)
{
	out.clear();
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	walk(root, [&out](XMLNode node)
	{
		if (nameIs(node, "FX")) addUnique(out, node.getAttribute("name"));
	});
	return true;
}

bool gfcSessionPaths::rewriteMedia(const std::string& jcsXml, const std::map<std::string, std::string>& mapping,
								   std::string& out, std::string* err)
{
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	forEachMediaAttribute(root, [&mapping](XMLNode node, const char* attribute, Kind, int, int)
	{
		XMLCSTR value = node.getAttribute(attribute);
		if (!value) return;
		const auto it = mapping.find(value);
		if (it != mapping.end()) node.updateAttribute(it->second.c_str(), NULL, attribute);
	});
	int size = 0;
	XMLSTR text = top.createXMLString(1, &size);
	if (!text)
	{
		setErr(err, "cannot serialise the session XML");
		return false;
	}
	out.assign(text, static_cast<size_t>(size));
	free(text);   // this xmlParser allocates with malloc and has no freeXMLString
	return true;
}
```

`addUnique` treats `"no LUT"` as "not a name" for plates; FX names never equal it, so sharing the helper is safe.

- [ ] **Step 5: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-SESSIONPATHS: pass=12 fail=0`; every other `NOTE-*` line `fail=0`; exit 0.
If `XMLNode::createXMLString` in `src/xmlParser.cpp` turns out not to allocate with `malloc` (read the function and `xmlParser.cpp` around line 446 where `free(t)` releases one), release the string the way that code does instead.

- [ ] **Step 6: Commit**

`git add src/gfcSessionPaths.h src/gfcSessionPaths.cpp src/main_qt.cpp` and commit with subject `JEF-39: read and rewrite the paths a session file points at`.

---

### Task 5: Reviews as XML strings, and the union merge

**Files:**
- Modify: `src/gfcNoteStore.h`, `src/gfcNoteStore.cpp` (new functions next to `save`/`load`; two checks added to `noteStoreSelfTest`)
- Create: `src/gfcNoteMerge.h`, `src/gfcNoteMerge.cpp`
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Consumes: in `src/gfcNoteStore.cpp`, the file-local `buildXml(const gfcReview&, const std::string& normalisedPath) -> XMLNode` and `loadFromXml(const XMLNode& xTop, gfcReview& out) -> bool`; `gfcReview` (`mediaPath`, `fingerprint`, `revisions`), `gfcRevision` (`id`, `author`, `locked`, `notes`), `gfcNoteStroke`.
- Produces:
  - `std::string gfcNoteStore::toXmlString(const gfcReview& review);`
  - `bool gfcNoteStore::fromXmlString(const std::string& xml, gfcReview& out);` (out untouched on failure)
  - `namespace gfcNoteMerge { struct Result { int revisionsAdded = 0; int notesAdded = 0; }; Result mergeInto(gfcReview& local, gfcReview&& incoming); }`
  - global `int noteMergeSelfTest();`

- [ ] **Step 1: Declare the XML string API and add its checks**

In `src/gfcNoteStore.h`, inside `namespace gfcNoteStore` after `load`, add:

```cpp
	/** The sidecar document for `review` (what save() writes), as a string --
	    how a review package carries notes without touching disk. */
	std::string toXmlString(const gfcReview& review);

	/** Parses a sidecar document into `out`. Returns false, leaving `out`
	    untouched, when `xml` is not a notes document. */
	bool fromXmlString(const std::string& xml, gfcReview& out);
```

In `src/gfcNoteStore.cpp`, add stubs after `gfcNoteStore::load`:

```cpp
std::string gfcNoteStore::toXmlString(const gfcReview&)
{
	return {};
}

bool gfcNoteStore::fromXmlString(const std::string&, gfcReview&)
{
	return false;
}
```

and in `noteStoreSelfTest()`, immediately before `std::printf("NOTE-STORE: pass=%d fail=%d\n", pass, fail);`, add (the review `w` is the one the round-trip test built earlier in the function):

```cpp
	// XML strings: what a review package carries.
	gfcReview fromString;
	check(gfcNoteStore::fromXmlString(gfcNoteStore::toXmlString(w), fromString) &&
		  gfcNoteStore::toJsonString(fromString) == gfcNoteStore::toJsonString(w),
		  "a review survives an XML string round trip");
	gfcReview untouched;
	check(!gfcNoteStore::fromXmlString("not a notes document", untouched) && untouched.revisions.empty(),
		  "a string that is not a notes document is refused");
```

- [ ] **Step 2: Build and run — expect the new checks to fail**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-STORE'`
Expected: `NOTE-STORE FAIL: a review survives an XML string round trip` and `NOTE-STORE: pass=35 fail=1`.

- [ ] **Step 3: Implement the XML string API**

Replace the two stubs with:

```cpp
std::string gfcNoteStore::toXmlString(const gfcReview& review)
{
	XMLNode xTop = buildXml(review, review.mediaPath);
	int size = 0;
	XMLSTR text = xTop.createXMLString(1, &size);
	if (!text)
	{
		return {};
	}
	std::string out(text, static_cast<size_t>(size));
	free(text);   // this xmlParser allocates with malloc and has no freeXMLString
	return out;
}

bool gfcNoteStore::fromXmlString(const std::string& xml, gfcReview& out)
{
	XMLResults results;
	XMLNode xTop = XMLNode::parseString(xml.c_str(), "jefecheckNotes", &results);
	if (results.error != eXMLErrorNone || xTop.isEmpty())
	{
		return false;
	}
	gfcReview parsed;
	if (!loadFromXml(xTop, parsed))
	{
		return false;
	}
	out.mediaPath = parsed.mediaPath;
	out.fingerprint = parsed.fingerprint;
	out.revisions = std::move(parsed.revisions);
	return true;
}
```

Add `#include <cstdlib>` if `free` is not already available in the file. These definitions must come after `buildXml` and `loadFromXml` in the file (they do if placed after `gfcNoteStore::load`).

- [ ] **Step 4: Build and run — expect pass**

Run the command from Step 2.
Expected: `NOTE-STORE: pass=36 fail=0`.

- [ ] **Step 5: Write the merge header, self-test and stub**

Create `src/gfcNoteMerge.h`:

```cpp
#ifndef GFCNOTEMERGE_H
#define GFCNOTEMERGE_H

class gfcReview;

/**
	@brief Union of two copies of one media's review, by id: what opening a
	review package does when the media already has notes. Nothing local is
	removed or changed; the same idempotent-by-id rule as live note sync.
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcNoteMerge
{
	struct Result
	{
		int revisionsAdded = 0;
		int notesAdded = 0;
	};

	/**
	 * Moves into `local` every revision of `incoming` whose id `local` lacks
	 * (appended, in incoming order), and every note of a shared revision whose id
	 * that revision lacks (appended, even into a locked revision -- both copies
	 * are the same round). An existing revision or note with the same id is kept
	 * as it is. `local` adopts `incoming`'s fingerprint only if it has none.
	 * `incoming` is consumed.
	 */
	Result mergeInto(gfcReview& local, gfcReview&& incoming);
}

/** Prints NOTE-MERGE: pass=N fail=N; returns the fail count. */
int noteMergeSelfTest();

#endif
```

Create `src/gfcNoteMerge.cpp`:

```cpp
#include "gfcNoteMerge.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gfcreview.h"
#include "gfcrevision.h"
#include "gfcnote.h"
#include "gfcnotestroke.h"

gfcNoteMerge::Result gfcNoteMerge::mergeInto(gfcReview&, gfcReview&&)
{
	return {};
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int noteMergeSelfTest()
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
			std::fprintf(stderr, "NOTE-MERGE FAIL: %s\n", msg);
		}
	};

	auto note = [](const char* id, const char* author)
	{
		auto n = std::make_unique<gfcNoteStroke>();
		n->id = id;
		n->author = author;
		n->pts = { gfcNotePoint{0.1f, 0.1f} };
		return n;
	};
	// Appends a revision and returns it; use it before appending another,
	// since the vector may reallocate.
	auto revision = [](gfcReview& review, const char* id, bool locked) -> gfcRevision&
	{
		review.revisions.emplace_back();
		gfcRevision& rev = review.revisions.back();
		rev.id = id;
		rev.author = "tester";
		rev.locked = locked;
		return rev;
	};
	auto ids = [](const gfcRevision& rev)
	{
		std::vector<std::string> out;
		for (const auto& n : rev.notes) out.push_back(n->id);
		return out;
	};
	auto makeIncoming = [&](gfcReview& in)
	{
		in.mediaPath = "/elsewhere/a.exr";
		in.fingerprint = "fp1:abc";
		{
			gfcRevision& r = revision(in, "r1", false);
			r.notes.push_back(note("n2", "B"));
			r.notes.push_back(note("n3", "B"));
		}
		{
			gfcRevision& r = revision(in, "r3", false);
			r.notes.push_back(note("n4", "B"));
		}
		{
			gfcRevision& r = revision(in, "r2", false);
			r.notes.push_back(note("n6", "B"));
		}
	};

	gfcReview local;
	local.mediaPath = "/m/a.exr";
	{
		gfcRevision& r = revision(local, "r1", false);
		r.notes.push_back(note("n1", "A"));
		r.notes.push_back(note("n2", "A"));
	}
	{
		gfcRevision& r = revision(local, "r2", true);
		r.notes.push_back(note("n5", "A"));
	}

	gfcReview incoming;
	makeIncoming(incoming);
	const gfcNoteMerge::Result first = gfcNoteMerge::mergeInto(local, std::move(incoming));
	check(first.revisionsAdded == 1 && first.notesAdded == 3, "one revision and three notes added");
	const bool shaped = local.revisions.size() == 3;
	check(shaped && local.revisions[0].id == "r1" && local.revisions[1].id == "r2" && local.revisions[2].id == "r3",
		  "local revisions first, the new one appended");
	if (shaped)
	{
		check(ids(local.revisions[0]) == std::vector<std::string>{"n1", "n2", "n3"},
			  "a shared revision gains only the missing note");
		check(local.revisions[0].notes[1]->author == "A", "an existing note is kept as it is");
		check(ids(local.revisions[1]) == std::vector<std::string>{"n5", "n6"} && local.revisions[1].locked,
			  "a locked local revision keeps its lock and still gains the note");
		check(ids(local.revisions[2]) == std::vector<std::string>{"n4"}, "a new revision keeps its notes");
	}
	check(local.mediaPath == "/m/a.exr" && local.fingerprint == "fp1:abc",
		  "local identity kept, empty fingerprint adopted");

	gfcReview again;
	makeIncoming(again);
	const gfcNoteMerge::Result second = gfcNoteMerge::mergeInto(local, std::move(again));
	check(second.revisionsAdded == 0 && second.notesAdded == 0 && local.revisions.size() == 3 &&
		  local.revisions[0].notes.size() == 3 && local.revisions[1].notes.size() == 2,
		  "merging the same package again changes nothing");

	gfcReview empty;
	const gfcNoteMerge::Result third = gfcNoteMerge::mergeInto(local, std::move(empty));
	check(third.revisionsAdded == 0 && third.notesAdded == 0 && local.fingerprint == "fp1:abc",
		  "merging an empty review changes nothing");

	std::printf("NOTE-MERGE: pass=%d fail=%d\n", pass, fail);
	return fail;
}
```

Add `#include "gfcNoteMerge.h"` to `src/main_qt.cpp` and `noteMergeSelfTest()` to the `--notes-test` block.

- [ ] **Step 6: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-MERGE'`
Expected: `NOTE-MERGE FAIL:` lines; `NOTE-MERGE: pass=0 fail=5`.

- [ ] **Step 7: Implement the merge**

Replace the stub `mergeInto` with:

```cpp
gfcNoteMerge::Result gfcNoteMerge::mergeInto(gfcReview& local, gfcReview&& incoming)
{
	Result result;
	if (local.fingerprint.empty())
	{
		local.fingerprint = incoming.fingerprint;
	}
	for (gfcRevision& inRev : incoming.revisions)
	{
		const auto existing = std::find_if(local.revisions.begin(), local.revisions.end(),
										   [&inRev](const gfcRevision& r) { return r.id == inRev.id; });
		if (existing == local.revisions.end())
		{
			for (const auto& n : inRev.notes)
			{
				if (n) ++result.notesAdded;
			}
			local.revisions.push_back(std::move(inRev));
			++result.revisionsAdded;
			continue;
		}
		for (auto& n : inRev.notes)
		{
			if (!n) continue;
			const bool present = std::any_of(existing->notes.begin(), existing->notes.end(),
											 [&n](const std::unique_ptr<gfcNote>& mine) { return mine && mine->id == n->id; });
			if (present) continue;
			// Direct push rather than addNote(): addNote refuses a locked round, and
			// this is the same round arriving from another copy, not an edit.
			existing->notes.push_back(std::move(n));
			++result.notesAdded;
		}
	}
	incoming.revisions.clear();
	return result;
}
```

- [ ] **Step 8: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-MERGE: pass=9 fail=0`, `NOTE-STORE: pass=36 fail=0`, every other `NOTE-*` line `fail=0`; exit 0.

- [ ] **Step 9: Commit**

`git add src/gfcNoteStore.h src/gfcNoteStore.cpp src/gfcNoteMerge.h src/gfcNoteMerge.cpp src/main_qt.cpp` and commit with subject `JEF-39: carry a review as a string, and merge two copies of it by id`.

---

### Task 6: Manifest and incremental exporter

**Files:**
- Create: `src/qt/ReviewPackage_qt.h`, `src/qt/ReviewPackage_qt.cpp`
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Consumes: Task 2 `gfcTar::Writer`, `gfcTar::Reader`, `gfcTar::isSafeName`; Task 4 `gfcSessionPaths::listMedia`, `gfcSessionPaths::rewriteMedia`; `gfcNoteStore::normalisePath`.
- Produces (namespace `jefe::qt::package`):
  - `constexpr const char* kFormat = "jefecheck-review-package"; constexpr int kVersion = 1;`
  - `struct ManifestMedia { int index; std::string originalPath, packagedPath, fingerprint; int width, height; std::vector<std::string> frames; std::string notes; };`
  - `struct ManifestLut { std::string name; std::string file; };`
  - `struct Manifest { std::string created, app, session = "session.jcs"; bool mediaIncluded = true; std::vector<ManifestMedia> media; std::vector<ManifestLut> luts; };`
  - `QByteArray manifestToJson(const Manifest&); bool manifestFromJson(const QByteArray&, Manifest& out, QString* err); std::string indexDir(const char* root, int index);`
  - `struct ExportMedia { std::string mediaPath; std::vector<std::string> frames; std::string notesXml; std::string fingerprint; int width = 0; int height = 0; };`
  - `struct ExportInput { std::string outPath; bool includeMedia = true; std::string sessionXml; std::vector<ExportMedia> media; std::vector<std::pair<std::string, std::string>> luts; std::string appVersion; std::string createdIso; };`
  - `class Exporter { enum class State { Running, Done, Failed, Cancelled }; bool begin(const ExportInput&, QString* err); State step(QString* err); void cancel(); qint64 bytesDone() const; qint64 bytesTotal() const; };`
  - `int packageSelfTest();`

- [ ] **Step 1: Write the header**

Create `src/qt/ReviewPackage_qt.h`:

```cpp
// Review package (.jcreview): manifest, exporter and opener.
// docs/superpowers/specs/2026-09-14-review-package-design.md
// QtCore only — no glad, no managers (developer_notes.md §1). App-side actions
// the opener needs arrive as callbacks.
#ifndef JEFECHECK_QT_REVIEW_PACKAGE_H
#define JEFECHECK_QT_REVIEW_PACKAGE_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../gfcTarArchive.h"

namespace jefe::qt::package {

inline constexpr const char* kFormat = "jefecheck-review-package";
inline constexpr int kVersion = 1;

struct ManifestMedia {
    int index = 0;
    std::string originalPath;           // normalised pattern when exported
    std::string packagedPath;           // "media/NNN/<pattern file name>"; empty when media is not included
    std::string fingerprint;            // "fp1:…"
    int width = 0;                      // first frame
    int height = 0;
    std::vector<std::string> frames;    // bare frame file names, in frame order
    std::string notes;                  // "notes/NNN.jnotes"
};

struct ManifestLut {
    std::string name;                   // the name sessions refer to
    std::string file;                   // "luts/<file name>"
};

struct Manifest {
    std::string created;                // ISO-8601 UTC
    std::string app;
    std::string session = "session.jcs";
    bool mediaIncluded = true;
    std::vector<ManifestMedia> media;
    std::vector<ManifestLut> luts;
};

QByteArray manifestToJson(const Manifest& manifest);
/** Refuses a foreign format, an unknown version (named in @a err) and unsafe entry names. */
bool manifestFromJson(const QByteArray& json, Manifest& out, QString* err);

/** "<root>/NNN", e.g. indexDir("media", 7) == "media/007". */
std::string indexDir(const char* root, int index);

/** One media as the caller gathered it. */
struct ExportMedia {
    std::string mediaPath;              // normalised pattern
    std::vector<std::string> frames;    // absolute frame paths, in frame order
    std::string notesXml;               // gfcNoteStore::toXmlString of its review
    std::string fingerprint;
    int width = 0;
    int height = 0;
};

struct ExportInput {
    std::string outPath;
    bool includeMedia = true;
    std::string sessionXml;             // the saved session; media paths still absolute
    std::vector<ExportMedia> media;
    std::vector<std::pair<std::string, std::string>> luts;   // LUT name -> source file (non-install LUTs only)
    std::string appVersion;
    std::string createdIso;
};

/**
 * Writes a package a little at a time so a dialog can stay responsive:
 * begin() plans every entry (manifest, session, notes, LUTs, media — in that
 * order) and opens <out>.partial; each step() writes one in-memory entry or one
 * 8 MiB chunk of a file; the step that writes the last entry finishes the
 * archive and renames it to <out>. A failure or cancel() removes the partial.
 */
class Exporter {
public:
    enum class State { Running, Done, Failed, Cancelled };

    bool begin(const ExportInput& input, QString* err);
    State step(QString* err);
    void cancel();
    State state() const { return state_; }
    qint64 bytesDone() const { return done_; }
    qint64 bytesTotal() const { return total_; }

private:
    struct Item {
        std::string entryName;
        std::string bytes;              // in-memory entries
        std::string sourcePath;         // file entries
        uint64_t size = 0;
    };

    void failWith(const std::string& message, QString* err);

    std::vector<Item> items_;
    size_t next_ = 0;
    uint64_t offsetInItem_ = 0;
    bool entryOpen_ = false;
    gfcTar::Writer writer_;
    std::string outPath_;
    std::string partialPath_;
    qint64 done_ = 0;
    qint64 total_ = 0;
    State state_ = State::Failed;
};

/** Manifest and exporter self-test; prints NOTE-PACKAGE: pass=N fail=N. */
int packageSelfTest();

}  // namespace jefe::qt::package

#endif
```

- [ ] **Step 2: Write the self-test with stub implementations**

Create `src/qt/ReviewPackage_qt.cpp`:

```cpp
#include "ReviewPackage_qt.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <system_error>

#include "../gfcNoteStore.h"
#include "../gfcSessionPaths.h"

namespace jefe::qt::package {

QByteArray manifestToJson(const Manifest&) { return {}; }
bool manifestFromJson(const QByteArray&, Manifest&, QString*) { return false; }
std::string indexDir(const char*, int) { return {}; }
bool Exporter::begin(const ExportInput&, QString*) { return false; }
Exporter::State Exporter::step(QString*) { return State::Failed; }
void Exporter::cancel() {}
void Exporter::failWith(const std::string&, QString*) {}

namespace {
std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

Exporter::State runToEnd(Exporter& exporter, QString* err) {
    Exporter::State state = Exporter::State::Running;
    while ((state = exporter.step(err)) == Exporter::State::Running) {}
    return state;
}
}  // namespace

int packageSelfTest() {
    namespace fs = std::filesystem;
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
        } else {
            ++fail;
            std::fprintf(stderr, "NOTE-PACKAGE FAIL: %s\n", msg);
        }
    };

    // --- manifest ---
    Manifest m;
    m.created = "2026-09-14T20:10:00Z";
    m.app = "1.7.0";
    ManifestMedia mm;
    mm.index = 0;
    mm.originalPath = "/shots/sh010.####.exr";
    mm.packagedPath = "media/000/sh010.####.exr";
    mm.fingerprint = "fp1:abc";
    mm.width = 1920;
    mm.height = 1080;
    mm.frames = {"sh010.0001.exr", "sh010.0002.exr"};
    mm.notes = "notes/000.jnotes";
    m.media.push_back(mm);
    m.luts.push_back(ManifestLut{"look.cube", "luts/look.cube"});

    Manifest back;
    QString err;
    check(manifestFromJson(manifestToJson(m), back, &err), "a manifest round trip parses");
    check(back.created == m.created && back.app == m.app && back.session == "session.jcs" && back.mediaIncluded &&
          back.media.size() == 1 && back.media[0].frames == mm.frames && back.media[0].width == 1920 &&
          back.media[0].height == 1080 && back.media[0].packagedPath == mm.packagedPath &&
          back.media[0].fingerprint == "fp1:abc" && back.media[0].notes == mm.notes &&
          back.luts.size() == 1 && back.luts[0].name == "look.cube" && back.luts[0].file == "luts/look.cube",
          "every manifest field survives");
    auto withField = [&m](const char* key, const QJsonValue& value) {
        QJsonObject o = QJsonDocument::fromJson(manifestToJson(m)).object();
        o[key] = value;
        return QJsonDocument(o).toJson();
    };
    check(!manifestFromJson(withField("format", "something-else"), back, &err), "a foreign format is refused");
    err.clear();
    check(!manifestFromJson(withField("version", 2), back, &err) && err.contains("version 2"),
          "an unknown version is refused, naming it");
    Manifest unsafe = m;
    unsafe.media[0].notes = "../escape.jnotes";
    check(!manifestFromJson(manifestToJson(unsafe), back, &err), "an unsafe entry name is refused");
    check(!manifestFromJson("not json", back, &err), "text that is not JSON is refused");
    check(indexDir("media", 7) == "media/007", "index directories are zero-padded");

    // --- exporter ---
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("jefe_package_test_" + std::to_string(QDateTime::currentMSecsSinceEpoch()));
    fs::create_directories(dir / "src", ec);
    const std::string f1 = (dir / "src" / "sh010.0001.exr").string();
    const std::string f2 = (dir / "src" / "sh010.0002.exr").string();
    const std::string lut = (dir / "look.cube").string();
    writeText(f1, "frame-one");
    writeText(f2, "frame-two-xyz");
    writeText(lut, "LUT");

    ExportInput in;
    in.outPath = (dir / "with.jcreview").string();
    in.includeMedia = true;
    in.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates><plate plateID=\"0\" gamma=\"1.5\" lut=\"look.cube\"/></plates>") +
                    "<tracks><track trackID=\"0\" filename=\"" + f1 + "\" from=\"1\" to=\"2\"/></tracks><playlist/></root>\n";
    in.appVersion = "1.7.0";
    in.createdIso = "2026-09-14T20:10:00Z";
    ExportMedia em;
    em.mediaPath = gfcNoteStore::normalisePath(f1);
    em.frames = {f1, f2};
    em.notesXml = "<jefecheckNotes version=\"1\"/>";
    em.fingerprint = "fp1:abc";
    em.width = 2;
    em.height = 2;
    in.media.push_back(em);
    in.luts.emplace_back("look.cube", lut);

    Exporter exporter;
    check(exporter.begin(in, &err), "the exporter begins");
    check(runToEnd(exporter, &err) == Exporter::State::Done, "the exporter finishes");
    check(exporter.bytesTotal() > 0 && exporter.bytesDone() == exporter.bytesTotal(), "progress reaches the total");
    check(fs::exists(in.outPath, ec) && !fs::exists(in.outPath + ".partial", ec), "package written, no partial left");

    gfcTar::Reader reader;
    std::string terr;
    check(reader.open(in.outPath, &terr), "the package is a valid archive");
    std::vector<std::string> names;
    for (const gfcTar::Entry& e : reader.entries()) names.push_back(e.name);
    check(names == std::vector<std::string>{"manifest.json", "session.jcs", "notes/000.jnotes", "luts/look.cube",
                                            "media/000/sh010.0001.exr", "media/000/sh010.0002.exr"},
          "entries in the stated order");
    std::string bytes;
    const gfcTar::Entry* frame2 = reader.find("media/000/sh010.0002.exr");
    check(frame2 && reader.readBytes(*frame2, bytes, &terr) && bytes == "frame-two-xyz", "media packaged byte for byte");
    const gfcTar::Entry* session = reader.find("session.jcs");
    check(session && reader.readBytes(*session, bytes, &terr) &&
          bytes.find("filename=\"media/000/sh010.0001.exr\"") != std::string::npos &&
          bytes.find(f1) == std::string::npos && bytes.find("gamma=\"1.5\"") != std::string::npos,
          "the session points at the packaged media and keeps its settings");
    Manifest written;
    check(!reader.entries().empty() && reader.readBytes(reader.entries()[0], bytes, &terr) &&
          manifestFromJson(QByteArray::fromStdString(bytes), written, &err) && written.mediaIncluded &&
          written.media.size() == 1 && written.media[0].packagedPath == "media/000/sh010.####.exr" &&
          written.media[0].frames == std::vector<std::string>{"sh010.0001.exr", "sh010.0002.exr"} &&
          written.luts.size() == 1 && written.luts[0].file == "luts/look.cube",
          "the manifest describes what was packaged");

    ExportInput lean = in;
    lean.outPath = (dir / "without.jcreview").string();
    lean.includeMedia = false;
    Exporter leanExporter;
    check(leanExporter.begin(lean, &err) && runToEnd(leanExporter, &err) == Exporter::State::Done, "a lean package is written");
    gfcTar::Reader leanReader;
    bool anyMedia = false;
    if (leanReader.open(lean.outPath, &terr)) {
        for (const gfcTar::Entry& e : leanReader.entries()) {
            if (e.name.rfind("media/", 0) == 0) anyMedia = true;
        }
    }
    const gfcTar::Entry* leanSession = leanReader.find("session.jcs");
    check(!anyMedia && leanSession && leanReader.readBytes(*leanSession, bytes, &terr) &&
          bytes.find(f1) != std::string::npos,
          "a lean package has no media and keeps absolute paths");

    ExportInput cancelled = in;
    cancelled.outPath = (dir / "cancelled.jcreview").string();
    Exporter cancelExporter;
    cancelExporter.begin(cancelled, &err);
    cancelExporter.step(&err);
    cancelExporter.cancel();
    check(cancelExporter.state() == Exporter::State::Cancelled && !fs::exists(cancelled.outPath, ec) &&
          !fs::exists(cancelled.outPath + ".partial", ec),
          "cancel leaves neither package nor partial file");

    ExportInput missing = in;
    missing.outPath = (dir / "missing.jcreview").string();
    missing.media[0].frames.push_back((dir / "src" / "gone.0003.exr").string());
    Exporter missingExporter;
    err.clear();
    check(!missingExporter.begin(missing, &err) && err.contains("gone.0003.exr") &&
          !fs::exists(missing.outPath + ".partial", ec),
          "a missing frame fails before anything is written");

    fs::remove_all(dir, ec);
    std::printf("NOTE-PACKAGE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt::package
```

In `src/main_qt.cpp`, add `#include "qt/ReviewPackage_qt.h"` and add `jefe::qt::package::packageSelfTest()` to the `--notes-test` block.

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-PACKAGE'`
Expected: `NOTE-PACKAGE FAIL:` lines and a non-zero `fail=` count.

- [ ] **Step 4: Implement the manifest and exporter**

Replace the seven stub lines (from `QByteArray manifestToJson(const Manifest&) { return {}; }` through `void Exporter::failWith(...) {}`) with:

```cpp
namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QJsonValue& v) { return v.toString().toStdString(); }
}  // namespace

std::string indexDir(const char* root, int index) {
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%03d", index);
    return std::string(root) + "/" + digits;
}

QByteArray manifestToJson(const Manifest& manifest) {
    QJsonObject root;
    root["format"] = QLatin1String(kFormat);
    root["version"] = kVersion;
    root["created"] = qs(manifest.created);
    root["app"] = qs(manifest.app);
    root["session"] = qs(manifest.session);
    root["mediaIncluded"] = manifest.mediaIncluded;
    QJsonArray media;
    for (const ManifestMedia& mm : manifest.media) {
        QJsonObject o;
        o["index"] = mm.index;
        o["originalPath"] = qs(mm.originalPath);
        o["packagedPath"] = qs(mm.packagedPath);
        o["fingerprint"] = qs(mm.fingerprint);
        o["width"] = mm.width;
        o["height"] = mm.height;
        QJsonArray frames;
        for (const std::string& f : mm.frames) frames.append(qs(f));
        o["frames"] = frames;
        o["notes"] = qs(mm.notes);
        media.append(o);
    }
    root["media"] = media;
    QJsonArray luts;
    for (const ManifestLut& lut : manifest.luts) {
        QJsonObject o;
        o["name"] = qs(lut.name);
        o["file"] = qs(lut.file);
        luts.append(o);
    }
    root["luts"] = luts;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool manifestFromJson(const QByteArray& json, Manifest& out, QString* err) {
    auto fail = [err](const QString& message) {
        if (err) *err = message;
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QStringLiteral("manifest.json does not parse: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != QLatin1String(kFormat)) {
        return fail(QStringLiteral("Not a JefeCheck review package"));
    }
    const int version = root.value("version").toInt(-1);
    if (version != kVersion) {
        return fail(QStringLiteral("Unsupported review package version %1").arg(version));
    }

    Manifest m;
    m.created = ss(root.value("created"));
    m.app = ss(root.value("app"));
    m.session = root.value("session").toString(QStringLiteral("session.jcs")).toStdString();
    m.mediaIncluded = root.value("mediaIncluded").toBool(false);
    if (!gfcTar::isSafeName(m.session)) return fail(QStringLiteral("Unsafe session name in the manifest"));
    for (const QJsonValue& value : root.value("media").toArray()) {
        const QJsonObject o = value.toObject();
        ManifestMedia mm;
        mm.index = o.value("index").toInt();
        mm.originalPath = ss(o.value("originalPath"));
        mm.packagedPath = ss(o.value("packagedPath"));
        mm.fingerprint = ss(o.value("fingerprint"));
        mm.width = o.value("width").toInt();
        mm.height = o.value("height").toInt();
        mm.notes = ss(o.value("notes"));
        for (const QJsonValue& f : o.value("frames").toArray()) mm.frames.push_back(ss(f));
        if (!gfcTar::isSafeName(mm.notes) || (!mm.packagedPath.empty() && !gfcTar::isSafeName(mm.packagedPath))) {
            return fail(QStringLiteral("Unsafe entry name in the manifest"));
        }
        for (const std::string& f : mm.frames) {
            if (f.find('/') != std::string::npos || !gfcTar::isSafeName(f)) {
                return fail(QStringLiteral("Unsafe frame name in the manifest"));
            }
        }
        m.media.push_back(std::move(mm));
    }
    for (const QJsonValue& value : root.value("luts").toArray()) {
        const QJsonObject o = value.toObject();
        ManifestLut lut{ss(o.value("name")), ss(o.value("file"))};
        if (!gfcTar::isSafeName(lut.file)) return fail(QStringLiteral("Unsafe LUT name in the manifest"));
        m.luts.push_back(std::move(lut));
    }
    out = std::move(m);
    return true;
}

bool Exporter::begin(const ExportInput& input, QString* err) {
    namespace fs = std::filesystem;
    auto fail = [this, err](const std::string& message) {
        if (err) *err = QString::fromStdString(message);
        state_ = State::Failed;
        return false;
    };
    items_.clear();
    next_ = 0;
    offsetInItem_ = 0;
    entryOpen_ = false;
    done_ = 0;
    total_ = 0;
    outPath_ = input.outPath;
    partialPath_ = input.outPath + ".partial";

    Manifest manifest;
    manifest.created = input.createdIso;
    manifest.app = input.appVersion;
    manifest.mediaIncluded = input.includeMedia;

    std::vector<Item> notes, luts, media;
    for (size_t i = 0; i < input.media.size(); ++i) {
        const ExportMedia& m = input.media[i];
        ManifestMedia mm;
        mm.index = int(i);
        mm.originalPath = m.mediaPath;
        mm.fingerprint = m.fingerprint;
        mm.width = m.width;
        mm.height = m.height;
        mm.notes = indexDir("notes", int(i)) + ".jnotes";
        for (const std::string& frame : m.frames) {
            const std::string name = fs::path(frame).filename().string();
            mm.frames.push_back(name);
            if (!input.includeMedia) continue;
            std::error_code ec;
            const uint64_t size = fs::file_size(frame, ec);
            if (ec) return fail("Cannot read " + frame);
            media.push_back(Item{indexDir("media", int(i)) + "/" + name, std::string(), frame, size});
        }
        if (input.includeMedia) {
            mm.packagedPath = indexDir("media", int(i)) + "/" + fs::path(m.mediaPath).filename().string();
        }
        notes.push_back(Item{mm.notes, m.notesXml, std::string(), m.notesXml.size()});
        manifest.media.push_back(std::move(mm));
    }
    for (const auto& [name, source] : input.luts) {
        std::error_code ec;
        const uint64_t size = fs::file_size(source, ec);
        if (ec) return fail("Cannot read " + source);
        const std::string entry = "luts/" + fs::path(source).filename().string();
        luts.push_back(Item{entry, std::string(), source, size});
        manifest.luts.push_back(ManifestLut{name, entry});
    }

    std::string session = input.sessionXml;
    if (input.includeMedia) {
        std::vector<gfcSessionPaths::MediaRef> refs;
        std::string perr;
        if (!gfcSessionPaths::listMedia(input.sessionXml, refs, &perr)) return fail(perr);
        std::map<std::string, std::string> mapping;
        for (const gfcSessionPaths::MediaRef& ref : refs) {
            const std::string key = gfcNoteStore::normalisePath(ref.path);
            for (size_t i = 0; i < input.media.size(); ++i) {
                if (input.media[i].mediaPath == key) {
                    mapping[ref.path] = indexDir("media", int(i)) + "/" + fs::path(ref.path).filename().string();
                }
            }
        }
        if (!gfcSessionPaths::rewriteMedia(input.sessionXml, mapping, session, &perr)) return fail(perr);
    }

    const std::string manifestJson = manifestToJson(manifest).toStdString();
    items_.push_back(Item{"manifest.json", manifestJson, std::string(), manifestJson.size()});
    items_.push_back(Item{manifest.session, session, std::string(), session.size()});
    for (std::vector<Item>* group : {&notes, &luts, &media}) {
        for (Item& item : *group) items_.push_back(std::move(item));
    }
    for (const Item& item : items_) total_ += qint64(item.size);

    std::string werr;
    if (!writer_.open(partialPath_, &werr)) return fail(werr);
    state_ = State::Running;
    return true;
}

Exporter::State Exporter::step(QString* err) {
    namespace fs = std::filesystem;
    if (state_ != State::Running) return state_;
    std::string e;

    if (next_ >= items_.size()) {
        if (!writer_.finish(&e)) {
            failWith(e, err);
            return state_;
        }
        std::error_code ec;
        fs::remove(outPath_, ec);
        fs::rename(partialPath_, outPath_, ec);
        if (ec) {
            failWith("Cannot rename " + partialPath_ + ": " + ec.message(), err);
            return state_;
        }
        state_ = State::Done;
        return state_;
    }

    Item& item = items_[next_];
    if (!entryOpen_) {
        if (!writer_.beginEntry(item.entryName, item.size, &e)) {
            failWith(e, err);
            return state_;
        }
        entryOpen_ = true;
        offsetInItem_ = 0;
    }
    if (item.sourcePath.empty()) {
        if (!writer_.write(item.bytes.data(), item.bytes.size(), &e)) {
            failWith(e, err);
            return state_;
        }
        offsetInItem_ = item.size;
        done_ += qint64(item.size);
    } else {
        constexpr uint64_t kChunk = 8 * 1024 * 1024;
        const size_t n = size_t(std::min<uint64_t>(kChunk, item.size - offsetInItem_));
        std::string chunk(n, '\0');
        std::ifstream in(item.sourcePath, std::ios::binary);
        in.seekg(std::streamoff(offsetInItem_));
        if (n > 0) in.read(&chunk[0], std::streamsize(n));
        if (!in) {
            failWith("Cannot read " + item.sourcePath, err);
            return state_;
        }
        if (!writer_.write(chunk.data(), n, &e)) {
            failWith(e, err);
            return state_;
        }
        offsetInItem_ += n;
        done_ += qint64(n);
    }
    if (offsetInItem_ >= item.size) {
        if (!writer_.endEntry(&e)) {
            failWith(e, err);
            return state_;
        }
        entryOpen_ = false;
        ++next_;
    }
    return state_;
}

void Exporter::cancel() {
    if (state_ != State::Running) return;
    writer_.abandon();
    std::error_code ec;
    std::filesystem::remove(partialPath_, ec);
    state_ = State::Cancelled;
}

void Exporter::failWith(const std::string& message, QString* err) {
    writer_.abandon();
    std::error_code ec;
    std::filesystem::remove(partialPath_, ec);
    state_ = State::Failed;
    if (err) *err = QString::fromStdString(message);
}
```

- [ ] **Step 5: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-PACKAGE: pass=20 fail=0`; every other `NOTE-*` line `fail=0`; exit 0.

- [ ] **Step 6: Commit**

`git add src/qt/ReviewPackage_qt.h src/qt/ReviewPackage_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: write a review package a step at a time`.

---

### Task 7: Export a package from the running app

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp` (new functions after `prepareTrackForRender` from the summary plan)
- Modify: `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp`
- Modify: `src/main_qt.cpp` (flags after `--export-summary`)

**Interfaces:**
- Consumes: Task 3 `gfcMediaFingerprint::probe/compute`; Task 4 `gfcSessionPaths::listLutNames`; Task 5 `gfcNoteStore::toXmlString/fromXmlString`; Task 6 `jefe::qt::package::{ExportInput, ExportMedia, Exporter, Manifest, manifestFromJson}`; summary plan `jefe::qt::getSessionMediaSet()`, `jefe::qt::SessionMedia`, `gfcReviewSummary::isoUtc`. Bridge internals: `reviewForPath(const std::string&) -> gfcReview&`, `lutManager.getLutIndexByName(std::string) -> int`, `lutManager.getLUT(int).filename` (char[250] full path), `sett.lutPath`, `::getApplicationDataPath()`, the global free function `void findSequence(std::vector<std::string>& refFiles, std::string inputFilename, std::string& label, int& startNum, int& endNum)` defined in `src/gfcSequence.cpp`. Existing app API: `jefe::qt::saveSession(const std::string&) -> bool`, `jefe::qt::adjustPlateExposure(int, float)`, `MainWindow_Qt::loadFileIntoPlate(int, const QString&)`, `jefe::qt::setActivePlate(int)`.
- Produces:
  - bridge: `std::vector<std::string> listSequenceFrames(const std::string& anyFramePath); std::string reviewXmlForMedia(const std::string& mediaPath); std::string reviewFingerprint(const std::string& mediaPath); bool setReviewFingerprint(const std::string& mediaPath, const std::string& fingerprint); std::string lutSourcePath(const std::string& lutName); bool isInstallLutPath(const std::string& path);`
  - `MainWindow_Qt::PackageStats { int media; bool mediaIncluded; qint64 bytes; int resolved; int missing; QString extractDir; QStringList missingMedia; QStringList missingFx; }`
  - `bool MainWindow_Qt::gatherPackageInput(const QString& outPath, bool includeMedia, jefe::qt::package::ExportInput& input, QString* message);`
  - `qint64 MainWindow_Qt::packageMediaBytes();`
  - `bool MainWindow_Qt::exportReviewPackage(const QString& outPath, bool includeMedia, PackageStats* stats, QString* message);`
  - `int MainWindow_Qt::runHeadlessPackageTest(const QString& imagePath);` and private `QString MainWindow_Qt::makePackageFixture(const QString& imagePath, const QString& work);`
  - CLI: `--package-test <image>`, `--export-package <out> [--no-media]`

- [ ] **Step 1: Bridge declarations and definitions**

In `src/qt/SequenceLoadBridge_qt.h`, after `prepareTrackForRender`, add:

```cpp
/** Every frame file of the sequence @a anyFramePath belongs to, in frame order. */
std::vector<std::string> listSequenceFrames(const std::string& anyFramePath);

/** The sidecar document of the in-memory review for @a mediaPath (loaded on first touch). */
std::string reviewXmlForMedia(const std::string& mediaPath);

/** The fingerprint recorded on the review for @a mediaPath, or "". */
std::string reviewFingerprint(const std::string& mediaPath);

/** Records @a fingerprint on the review for @a mediaPath and saves its sidecar.
    Returns false when the sidecar could not be written (the value stays in memory). */
bool setReviewFingerprint(const std::string& mediaPath, const std::string& fingerprint);

/** The source file of the loaded LUT sessions call @a lutName, or "" when none is loaded under that name. */
std::string lutSourcePath(const std::string& lutName);

/** Whether @a path lies inside a directory LUTs autoload from (sett.lutPath, the bundle FX/, ./FX/). */
bool isInstallLutPath(const std::string& path);
```

In `src/qt/SequenceLoadBridge_qt.cpp`, at file scope near the top (outside every namespace, after the includes), declare the finder that lives in `gfcSequence.cpp`:

```cpp
// Defined in gfcSequence.cpp; not declared in any header.
void findSequence(std::vector<std::string>& refFiles, std::string inputFilename,
                  std::string& label, int& startNum, int& endNum);
```

and after `prepareTrackForRender`, add:

```cpp
std::vector<std::string> listSequenceFrames(const std::string& anyFramePath) {
    std::vector<std::string> files;
    std::string label;
    int startNum = 0;
    int endNum = 0;
    ::findSequence(files, anyFramePath, label, startNum, endNum);
    return files;
}

std::string reviewXmlForMedia(const std::string& mediaPath) {
    return gfcNoteStore::toXmlString(reviewForPath(mediaPath));
}

std::string reviewFingerprint(const std::string& mediaPath) {
    return reviewForPath(mediaPath).fingerprint;
}

bool setReviewFingerprint(const std::string& mediaPath, const std::string& fingerprint) {
    gfcReview& review = reviewForPath(mediaPath);
    review.fingerprint = fingerprint;
    return gfcNoteStore::save(review);
}

std::string lutSourcePath(const std::string& lutName) {
    const int index = lutManager.getLutIndexByName(lutName);
    if (index < 0) return {};
    return std::string(lutManager.getLUT(index).filename);
}

bool isInstallLutPath(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string file = fs::weakly_canonical(fs::path(path), ec).string();
    if (ec) return false;
    std::vector<std::string> dirs;
    if (!sett.lutPath.empty()) dirs.push_back(sett.lutPath);
    dirs.push_back(::getApplicationDataPath() + "FX/");
    dirs.push_back("FX/");
    for (const std::string& d : dirs) {
        std::error_code dec;
        std::string dir = fs::weakly_canonical(fs::path(d), dec).string();
        if (dec || dir.empty()) continue;
        if (dir.back() != '/') dir += '/';
        if (file.compare(0, dir.size(), dir) == 0) return true;
    }
    return false;
}
```

Verify in `src/gfcSequence.cpp` that `findSequence` fills `refFiles` with full paths for both a numbered sequence and a single un-numbered file (e.g. `Blobbies.exr`). If it returns bare names, join each with the directory of `anyFramePath` inside `listSequenceFrames`. Record what you found in the report.

- [ ] **Step 2: MainWindow declarations**

In `src/qt/MainWindow_qt.h`, add `#include "ReviewPackage_qt.h"` and `#include <QStringList>` with the other includes, and after the review summary declarations add:

```cpp
    /** Counts from exportReviewPackage() and openReviewPackage(). */
    struct PackageStats {
        int media = 0;
        bool mediaIncluded = false;
        qint64 bytes = 0;
        int resolved = 0;
        int missing = 0;
        QString extractDir;
        QStringList missingMedia;
        QStringList missingFx;
    };

    /** Collects what a package of the current session needs: the saved session,
        every media's frames, fingerprint (computed and saved if missing) and
        notes, and the non-bundled LUTs the session uses. */
    bool gatherPackageInput(const QString& outPath, bool includeMedia,
                            jefe::qt::package::ExportInput& input, QString* message);

    /** Total size of every frame file in the session (for "Include media"). */
    qint64 packageMediaBytes();

    /** Writes a review package synchronously (CLI and tests; the dialog steps it). See
        docs/superpowers/specs/2026-09-14-review-package-design.md. */
    bool exportReviewPackage(const QString& outPath, bool includeMedia, PackageStats* stats, QString* message);

    /** Headless end-to-end proof of the review package (--package-test <image>). */
    int runHeadlessPackageTest(const QString& imagePath);
```

and in the `private:` section:

```cpp
    /** Copies @a imagePath into @a work/src and writes a one-round sidecar beside it.
        Returns the copy's path, or an empty string on failure. */
    QString makePackageFixture(const QString& imagePath, const QString& work);
```

- [ ] **Step 3: Write the end-to-end test and flags (failing)**

In `src/qt/MainWindow_qt.cpp`, add includes (skip any present): `"../gfcMediaFingerprint.h"`, `"../gfcSessionPaths.h"`, `"../gfcTarArchive.h"`, `"../gfcStructures.h"`, `<ctime>`. Add stubs and the complete test:

```cpp
bool MainWindow_Qt::gatherPackageInput(const QString&, bool, jefe::qt::package::ExportInput&, QString* message) {
    if (message) *message = tr("Not implemented");
    return false;
}

qint64 MainWindow_Qt::packageMediaBytes() { return 0; }

bool MainWindow_Qt::exportReviewPackage(const QString&, bool, PackageStats*, QString* message) {
    if (message) *message = tr("Not implemented");
    return false;
}

QString MainWindow_Qt::makePackageFixture(const QString& imagePath, const QString& work) {
    const QString srcDir = work + "/src";
    QDir().mkpath(srcDir);
    const QString media = srcDir + "/" + QFileInfo(imagePath).fileName();
    if (!QFile::copy(imagePath, media)) return QString();
    gfcReview review;
    review.mediaPath = gfcNoteStore::normalisePath(media.toStdString());
    gfcRevision& round = review.beginRevision("Supervisor");
    auto stroke = std::make_unique<gfcNoteStroke>();
    stroke->author = "Supervisor";
    stroke->quadID = 0;
    stroke->from = 1;
    stroke->to = 1;
    stroke->pts = { gfcNotePoint{0.2f, 0.2f}, gfcNotePoint{0.8f, 0.8f} };
    round.addNote(std::move(stroke));
    round.locked = true;
    return gfcNoteStore::save(review) ? media : QString();
}

int MainWindow_Qt::runHeadlessPackageTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("PACKAGE-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto readBytes = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    if (!viewport_) { printf("PACKAGE-TEST FAIL no viewport\n"); fflush(stdout); return 2; }

    const QString work = QDir::tempPath() + "/jefecheck_packagetest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString media = makePackageFixture(imagePath, work);
    if (media.isEmpty()) { printf("PACKAGE-TEST FAIL fixture\n"); fflush(stdout); return 2; }
    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);
    jefe::qt::adjustPlateExposure(0, 1.5f);
    const std::string mediaKey = gfcNoteStore::normalisePath(media.toStdString());
    const std::string imageName = QFileInfo(media).fileName().toStdString();

    PackageStats stats;
    QString msg;
    const QString withMedia = work + "/with.jcreview";
    check(exportReviewPackage(withMedia, true, &stats, &msg), "package with media exported");
    printf("PACKAGE-TEST export: %s\n", qPrintable(msg));
    check(stats.media == 1 && stats.mediaIncluded && stats.bytes > QFileInfo(media).size(),
          "stats: one media, included, larger than the image");

    gfcTar::Reader reader;
    std::string terr;
    std::string bytes;
    check(reader.open(withMedia.toStdString(), &terr), "the package is a valid archive");
    check(!reader.entries().empty() && reader.entries()[0].name == "manifest.json", "manifest.json is the first entry");
    const gfcTar::Entry* image = reader.find("media/000/" + imageName);
    check(image && reader.readBytes(*image, bytes, &terr) && QByteArray::fromStdString(bytes) == readBytes(media),
          "the image is packaged byte for byte");
    const gfcTar::Entry* session = reader.find("session.jcs");
    check(session && reader.readBytes(*session, bytes, &terr) &&
          bytes.find("filename=\"media/000/" + imageName + "\"") != std::string::npos,
          "the session points at the packaged image");
    gfcReview packagedNotes;
    const gfcTar::Entry* notes = reader.find("notes/000.jnotes");
    check(notes && reader.readBytes(*notes, bytes, &terr) && gfcNoteStore::fromXmlString(bytes, packagedNotes) &&
          packagedNotes.revisions.size() == 1,
          "the notes are packaged");
    jefe::qt::package::Manifest manifest;
    QString merr;
    std::string manifestBytes;
    check(!reader.entries().empty() && reader.readBytes(reader.entries()[0], manifestBytes, &terr) &&
          jefe::qt::package::manifestFromJson(QByteArray::fromStdString(manifestBytes), manifest, &merr) &&
          manifest.mediaIncluded && manifest.media.size() == 1 &&
          manifest.media[0].fingerprint.rfind("fp1:", 0) == 0 &&
          manifest.media[0].frames == std::vector<std::string>{imageName} && manifest.media[0].width > 0,
          "the manifest describes the media");
    gfcReview onDisk;
    check(gfcNoteStore::load(mediaKey, onDisk) && !manifest.media.empty() &&
          onDisk.fingerprint == manifest.media[0].fingerprint,
          "the source sidecar now records the fingerprint");

    const QString withoutMedia = work + "/without.jcreview";
    check(exportReviewPackage(withoutMedia, false, &stats, &msg), "package without media exported");
    gfcTar::Reader lean;
    bool anyMedia = false;
    const bool leanOpened = lean.open(withoutMedia.toStdString(), &terr);
    if (leanOpened) {
        for (const gfcTar::Entry& e : lean.entries()) {
            if (e.name.rfind("media/", 0) == 0) anyMedia = true;
        }
    }
    const gfcTar::Entry* leanSession = leanOpened ? lean.find("session.jcs") : nullptr;
    check(leanOpened && !anyMedia && leanSession && lean.readBytes(*leanSession, bytes, &terr) &&
          bytes.find(media.toStdString()) != std::string::npos,
          "the lean package has no media and keeps the absolute path");
    check(!QFile::exists(withMedia + ".partial") && !QFile::exists(withoutMedia + ".partial"), "no partial files left");

    // (Task 9 inserts the open round trip here.)

    printf("PACKAGE-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}
```

In `src/main_qt.cpp`, after the `--export-summary` loop, add:

```cpp
    // --package-test <image>: end-to-end proof of the review package.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--package-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessPackageTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --export-package <out> [--no-media]: package what is loaded, then quit.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--export-package") != 0) continue;
        const QString out = QString::fromUtf8(argv[i + 1]);
        bool includeMedia = true;
        for (int k = 1; k < argc; ++k) {
            if (std::strcmp(argv[k], "--no-media") == 0) includeMedia = false;
        }
        QTimer::singleShot(6000, &window, [&window, out, includeMedia]() {
            MainWindow_Qt::PackageStats s;
            QString msg;
            const bool ok = window.exportReviewPackage(out, includeMedia, &s, &msg);
            if (ok) {
                printf("PACKAGE: wrote=%s media=%d included=%d bytes=%lld\n", qPrintable(out), s.media,
                       s.mediaIncluded ? 1 : 0, static_cast<long long>(s.bytes));
            } else {
                printf("PACKAGE: FAIL %s\n", qPrintable(msg));
            }
            fflush(stdout);
            std::_Exit(ok ? 0 : 2);
        });
        break;
    }
```

- [ ] **Step 4: Build and run — expect failures**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --package-test /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr 2>&1 | grep PACKAGE-TEST; echo "exit=${pipestatus[1]}"`
Expected: `PACKAGE-TEST FAIL package with media exported` and the dependent checks failing; `PACKAGE-TEST: FAIL`; `exit=2`.

- [ ] **Step 5: Implement gathering and export**

Replace the three stubs with:

```cpp
bool MainWindow_Qt::gatherPackageInput(const QString& outPath, bool includeMedia,
                                       jefe::qt::package::ExportInput& input, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    const std::vector<jefe::qt::SessionMedia> media = jefe::qt::getSessionMediaSet();
    if (media.empty()) {
        say(tr("Nothing to package: the session has no media"));
        return false;
    }

    const QString dir = QDir::tempPath() + "/jefecheck_package_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(dir);
    const QString sessionFile = dir + "/session.jcs";
    if (!jefe::qt::saveSession(sessionFile.toStdString())) {
        say(tr("Cannot save the session"));
        return false;
    }
    QFile saved(sessionFile);
    if (!saved.open(QIODevice::ReadOnly)) {
        say(tr("Cannot read the saved session"));
        return false;
    }

    input = jefe::qt::package::ExportInput{};
    input.outPath = outPath.toStdString();
    input.includeMedia = includeMedia;
    input.sessionXml = saved.readAll().toStdString();
    input.appVersion = JEFE_VERSION;
    input.createdIso = gfcReviewSummary::isoUtc(time(nullptr));

    for (const jefe::qt::SessionMedia& m : media) {
        jefe::qt::package::ExportMedia em;
        em.mediaPath = m.mediaPath;
        em.frames = jefe::qt::listSequenceFrames(m.anyFramePath);
        if (em.frames.empty()) {
            say(tr("Cannot find the frames of %1").arg(QString::fromStdString(m.anyFramePath)));
            return false;
        }
        std::string err;
        gfcMediaFingerprint::Probe probe;
        if (!gfcMediaFingerprint::probe(em.frames.front(), probe, &err)) {
            say(QString::fromStdString(err));
            return false;
        }
        em.width = probe.width;
        em.height = probe.height;
        em.fingerprint = jefe::qt::reviewFingerprint(m.mediaPath);
        if (em.fingerprint.empty()) {
            em.fingerprint = gfcMediaFingerprint::compute(em.frames, &err);
            if (em.fingerprint.empty()) {
                say(QString::fromStdString(err));
                return false;
            }
            // Best effort: an unwritable sidecar keeps the value in memory, and
            // the packaged notes below carry it either way.
            jefe::qt::setReviewFingerprint(m.mediaPath, em.fingerprint);
        }
        em.notesXml = jefe::qt::reviewXmlForMedia(m.mediaPath);
        input.media.push_back(std::move(em));
    }

    std::vector<std::string> lutNames;
    std::string perr;
    if (!gfcSessionPaths::listLutNames(input.sessionXml, lutNames, &perr)) {
        say(QString::fromStdString(perr));
        return false;
    }
    for (const std::string& name : lutNames) {
        const std::string source = jefe::qt::lutSourcePath(name);
        if (!source.empty() && !jefe::qt::isInstallLutPath(source)) input.luts.emplace_back(name, source);
    }
    return true;
}

qint64 MainWindow_Qt::packageMediaBytes() {
    qint64 total = 0;
    for (const jefe::qt::SessionMedia& m : jefe::qt::getSessionMediaSet()) {
        for (const std::string& frame : jefe::qt::listSequenceFrames(m.anyFramePath)) {
            total += QFileInfo(QString::fromStdString(frame)).size();
        }
    }
    return total;
}

bool MainWindow_Qt::exportReviewPackage(const QString& outPath, bool includeMedia, PackageStats* stats, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    jefe::qt::package::ExportInput input;
    if (!gatherPackageInput(outPath, includeMedia, input, message)) return false;

    jefe::qt::package::Exporter exporter;
    QString err;
    if (!exporter.begin(input, &err)) {
        say(err);
        return false;
    }
    using State = jefe::qt::package::Exporter::State;
    State state = State::Running;
    while ((state = exporter.step(&err)) == State::Running) {}
    if (state != State::Done) {
        say(err.isEmpty() ? tr("Export failed") : err);
        return false;
    }

    PackageStats s;
    s.media = int(input.media.size());
    s.mediaIncluded = includeMedia;
    s.bytes = QFileInfo(outPath).size();
    if (stats) *stats = s;
    say(tr("Review package written: %1 (%2 media, %3)")
            .arg(QFileInfo(outPath).fileName())
            .arg(s.media)
            .arg(includeMedia ? tr("media included") : tr("media referenced")));
    return true;
}
```

- [ ] **Step 6: Build and run — expect pass**

Run the command from Step 4.
Expected: every `PACKAGE-TEST ok`, `PACKAGE-TEST: PASS`, `exit=0`. Also `--notes-test` exits 0.

- [ ] **Step 7: CLI check**

Run: `T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && cp /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr "$T/" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --open-file "$T/Blobbies.exr" --export-package "$T/cli.jcreview" 2>&1 | grep '^PACKAGE:'; tar -tvf "$T/cli.jcreview"`
Expected: `PACKAGE: wrote=<T>/cli.jcreview media=1 included=1 bytes=<n>`, and `tar -tvf` lists `manifest.json`, `session.jcs`, `notes/000.jnotes`, `media/000/Blobbies.exr`.

- [ ] **Step 8: Commit**

`git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: package the current session, with or without its media`.

---

### Task 8: Open a package — extract, relink, merge, rewrite

**Files:**
- Modify: `src/qt/ReviewPackage_qt.h`, `src/qt/ReviewPackage_qt.cpp`
- Modify: `src/main_qt.cpp` (`--notes-test` block)

**Interfaces:**
- Consumes: Task 1 `gfcSha1::hex`; Task 2 `gfcTar::Reader`; Task 3 `gfcMediaFingerprint::{probe, compute, sequencesIn, Probe}`; Task 4 `gfcSessionPaths::{listMedia, rewriteMedia, listFxNames, MediaRef}`; Task 5 `gfcNoteStore::{fromXmlString, toXmlString, load, save, normalisePath}`, `gfcNoteMerge::mergeInto`; Task 6 `Manifest`, `ManifestMedia`, `manifestFromJson`, `indexDir`, `Exporter`, `ExportInput`, `ExportMedia`.
- Produces (namespace `jefe::qt::package`):
  - `struct OpenServices { std::function<bool(const std::string& lutPath)> loadLut; std::function<void(const std::string& mediaPath)> reloadReview; std::vector<std::string> searchPaths; bool searchRecursive = false; bool interactive = false; std::function<std::string(const ManifestMedia&)> locate; std::function<bool(const ManifestMedia&, const std::string& chosenFrame)> confirmMismatch; };`
  - `struct OpenResult { Manifest manifest; std::string extractDir; std::string sessionPath; int resolved = 0; int missing = 0; std::vector<std::string> missingMedia; std::vector<std::string> fxNames; };`
  - `std::vector<std::string> findByFingerprint(const ManifestMedia& media, const std::vector<std::string>& roots, bool recursive);`
  - `bool openPackage(const std::string& packagePath, const std::string& cacheRoot, const OpenServices& services, OpenResult& result, QString* err);`
  - `int packageOpenSelfTest();`

- [ ] **Step 1: Declare the opener**

In `src/qt/ReviewPackage_qt.h`, add `#include <functional>` with the includes, and before `int packageSelfTest();` add:

```cpp
/** What opening a package needs from the running app. */
struct OpenServices {
    /** Loads a packaged LUT file; called for every LUT before the session. */
    std::function<bool(const std::string& lutPath)> loadLut;
    /** Called after a media's notes are merged on disk, so in-memory copies reload. */
    std::function<void(const std::string& mediaPath)> reloadReview;
    /** Preferences -> Search Paths, used whether or not "use search paths" is ticked. */
    std::vector<std::string> searchPaths;
    bool searchRecursive = false;
    /** When false, nothing prompts: unresolved media count as missing. */
    bool interactive = false;
    /** Returns a chosen frame file of the media, or "" to skip it. */
    std::function<std::string(const ManifestMedia& media)> locate;
    /** Asked when the located media's fingerprint differs; true uses it anyway. */
    std::function<bool(const ManifestMedia& media, const std::string& chosenFrame)> confirmMismatch;
};

struct OpenResult {
    Manifest manifest;
    std::string extractDir;
    std::string sessionPath;                  // the rewritten session, ready to load
    int resolved = 0;
    int missing = 0;
    std::vector<std::string> missingMedia;    // display names
    std::vector<std::string> fxNames;         // every FX the session uses
};

/**
 * The frames of the first sequence under @a roots whose fingerprint equals the
 * media's. Candidates must match the frame count and first-frame size before
 * any fingerprint is computed; candidates with the media's file-name pattern
 * are tried first. Empty when nothing matches.
 */
std::vector<std::string> findByFingerprint(const ManifestMedia& media, const std::vector<std::string>& roots,
                                           bool recursive);

/**
 * Validates the manifest, extracts into <cacheRoot>/<id> (id = SHA-1 of the
 * package's absolute path, size and modification time; reused once it holds a
 * .complete marker), loads packaged LUTs, resolves every media (packaged ->
 * original path -> fingerprint search -> services.locate), union-merges each
 * media's notes into the sidecar beside the resolved media, and writes the
 * session with media paths rewritten to the resolved frames. Media that stays
 * unresolved keeps its original path and is counted as missing. Returns false
 * (loading nothing) for an unreadable, truncated, foreign or unknown-version
 * package.
 */
bool openPackage(const std::string& packagePath, const std::string& cacheRoot, const OpenServices& services,
                 OpenResult& result, QString* err);

/** Opener self-test; prints NOTE-PACKAGE-OPEN: pass=N fail=N. */
int packageOpenSelfTest();
```

- [ ] **Step 2: Write the self-test with stubs (failing)**

In `src/qt/ReviewPackage_qt.cpp`, add includes (skip any present): `"../gfcMediaFingerprint.h"`, `"../gfcNoteMerge.h"`, `"../gfcSha1.h"`, `"../gfcreview.h"`, `"../gfcrevision.h"`, `"../gfcnotestroke.h"`, `<memory>`, `<functional>`. Inside `namespace jefe::qt::package`, after `failWith`, add the stubs:

```cpp
std::vector<std::string> findByFingerprint(const ManifestMedia&, const std::vector<std::string>&, bool) { return {}; }

bool openPackage(const std::string&, const std::string&, const OpenServices&, OpenResult&, QString*) { return false; }
```

and after `packageSelfTest()`, the complete self-test:

```cpp
int packageOpenSelfTest() {
    namespace fs = std::filesystem;
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
        } else {
            ++fail;
            std::fprintf(stderr, "NOTE-PACKAGE-OPEN FAIL: %s\n", msg);
        }
    };
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("jefe_package_open_test_" + std::to_string(QDateTime::currentMSecsSinceEpoch()));
    fs::create_directories(dir / "src", ec);
    const std::string f1 = (dir / "src" / "sh010.0001.exr").string();
    const std::string lut = (dir / "look.cube").string();
    writeText(f1, "frame-one");
    writeText(lut, "LUT");

    gfcReview review;
    review.mediaPath = gfcNoteStore::normalisePath(f1);
    {
        gfcRevision& round = review.beginRevision("Supervisor");
        round.id = "round-1";
        auto n = std::make_unique<gfcNoteStroke>();
        n->id = "note-1";
        n->pts = { gfcNotePoint{0.1f, 0.1f} };
        round.addNote(std::move(n));
    }

    ExportInput in;
    in.outPath = (dir / "with.jcreview").string();
    in.includeMedia = true;
    in.appVersion = "1.7.0";
    in.createdIso = "2026-09-14T20:10:00Z";
    in.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates><plate plateID=\"0\" lut=\"look.cube\"><stack><FXS><FX name=\"Grade\"/></FXS></stack></plate></plates>") +
                    "<tracks><track trackID=\"0\" filename=\"" + f1 + "\"/></tracks><playlist/></root>\n";
    ExportMedia em;
    em.mediaPath = review.mediaPath;
    em.frames = {f1};
    em.notesXml = gfcNoteStore::toXmlString(review);
    em.fingerprint = "fp1:0000";
    em.width = 1;
    em.height = 1;
    in.media.push_back(em);
    in.luts.emplace_back("look.cube", lut);
    auto exportAll = [](const ExportInput& input, QString* err) {
        Exporter exporter;
        return exporter.begin(input, err) && runToEnd(exporter, err) == Exporter::State::Done;
    };
    QString err;
    check(exportAll(in, &err), "fixture package exported");

    std::vector<std::string> lutsLoaded;
    std::vector<std::string> reviewsReloaded;
    OpenServices services;
    services.loadLut = [&lutsLoaded](const std::string& path) { lutsLoaded.push_back(path); return true; };
    services.reloadReview = [&reviewsReloaded](const std::string& media) { reviewsReloaded.push_back(media); };
    const std::string cache = (dir / "cache").string();

    OpenResult result;
    check(openPackage(in.outPath, cache, services, result, &err), "the package opens");
    const fs::path extracted = fs::path(result.extractDir) / "media" / "000" / "sh010.0001.exr";
    const std::string extractedMedia = gfcNoteStore::normalisePath(extracted.string());
    check(result.resolved == 1 && result.missing == 0, "packaged media resolves");
    check(fs::exists(fs::path(result.extractDir) / ".complete", ec) && fs::exists(extracted, ec),
          "the package is extracted with a completion marker");
    check(readText(result.sessionPath).find("filename=\"" + extracted.string() + "\"") != std::string::npos,
          "the session points at the extracted media");
    check(lutsLoaded.size() == 1 && lutsLoaded[0] == (fs::path(result.extractDir) / "luts" / "look.cube").string(),
          "the packaged LUT is loaded");
    check(reviewsReloaded.size() == 1 && reviewsReloaded[0] == extractedMedia, "the app is told to reload the review");
    gfcReview placed;
    check(gfcNoteStore::load(extractedMedia, placed) && placed.revisions.size() == 1 &&
          placed.revisions[0].notes.size() == 1 && placed.fingerprint == "fp1:0000",
          "notes are placed beside the extracted media");
    check(result.fxNames == std::vector<std::string>{"Grade"}, "the session's FX names are reported");

    OpenResult again;
    check(openPackage(in.outPath, cache, services, again, &err) && again.extractDir == result.extractDir,
          "reopening reuses the extraction");
    gfcReview placedAgain;
    check(gfcNoteStore::load(extractedMedia, placedAgain) && placedAgain.revisions.size() == 1 &&
          placedAgain.revisions[0].notes.size() == 1,
          "reopening does not duplicate notes");

    ExportInput lean = in;
    lean.outPath = (dir / "lean.jcreview").string();
    lean.includeMedia = false;
    check(exportAll(lean, &err), "lean package exported");
    OpenResult leanResult;
    check(openPackage(lean.outPath, cache, services, leanResult, &err) && leanResult.resolved == 1 &&
          readText(leanResult.sessionPath).find("filename=\"" + f1 + "\"") != std::string::npos,
          "a lean package uses the media at its original path");
    fs::remove(f1, ec);
    OpenResult gone;
    check(openPackage(lean.outPath, cache, services, gone, &err) && gone.resolved == 0 && gone.missing == 1 &&
          gone.missingMedia == std::vector<std::string>{"sh010.####.exr"} &&
          readText(gone.sessionPath).find("filename=\"" + f1 + "\"") != std::string::npos,
          "missing media is counted and keeps its original path");

    const std::string whole = readText(in.outPath);
    writeText((dir / "truncated.jcreview").string(), whole.substr(0, whole.size() / 2));
    OpenResult cut;
    err.clear();
    check(!openPackage((dir / "truncated.jcreview").string(), cache, services, cut, &err) && err.contains("truncated"),
          "a truncated package is refused");
    {
        QJsonObject future = QJsonDocument::fromJson(manifestToJson(Manifest{})).object();
        future["version"] = 2;
        gfcTar::Writer writer;
        std::string terr;
        writer.open((dir / "future.jcreview").string(), &terr);
        writer.addBytes("manifest.json", QJsonDocument(future).toJson().toStdString(), &terr);
        writer.finish(&terr);
    }
    OpenResult future;
    err.clear();
    check(!openPackage((dir / "future.jcreview").string(), cache, services, future, &err) && err.contains("version 2"),
          "an unknown package version is refused, naming it");

    fs::remove_all(dir, ec);
    std::printf("NOTE-PACKAGE-OPEN: pass=%d fail=%d\n", pass, fail);
    return fail;
}
```

Add `jefe::qt::package::packageOpenSelfTest()` to the `--notes-test` block in `src/main_qt.cpp`.

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-PACKAGE-OPEN'`
Expected: `NOTE-PACKAGE-OPEN FAIL:` lines; the two export checks pass: `NOTE-PACKAGE-OPEN: pass=2 fail=14`.

- [ ] **Step 4: Implement**

Replace the two stubs with:

```cpp
std::vector<std::string> findByFingerprint(const ManifestMedia& media, const std::vector<std::string>& roots,
                                           bool recursive) {
    namespace fs = std::filesystem;
    if (media.fingerprint.empty() || media.frames.empty()) return {};
    const std::string wantedName = fs::path(media.originalPath).filename().string();
    std::vector<std::vector<std::string>> sameName;
    std::vector<std::vector<std::string>> others;
    for (const std::string& root : roots) {
        for (const auto& [key, frames] : gfcMediaFingerprint::sequencesIn(root, recursive)) {
            if (frames.size() != media.frames.size()) continue;
            gfcMediaFingerprint::Probe probe;
            if (!gfcMediaFingerprint::probe(frames.front(), probe, nullptr)) continue;
            if (probe.width != media.width || probe.height != media.height) continue;
            (fs::path(key).filename().string() == wantedName ? sameName : others).push_back(frames);
        }
    }
    for (const auto* group : {&sameName, &others}) {
        for (const std::vector<std::string>& frames : *group) {
            if (gfcMediaFingerprint::compute(frames, nullptr) == media.fingerprint) return frames;
        }
    }
    return {};
}

namespace {
// Frames of the media, in order: packaged -> original path -> fingerprint
// search -> interactive locate. Empty when unresolved.
std::vector<std::string> resolveMedia(const ManifestMedia& media, bool mediaIncluded,
                                      const std::filesystem::path& extractDir, const OpenServices& services) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (media.frames.empty()) return {};

    std::vector<std::string> frames;
    if (mediaIncluded && !media.packagedPath.empty()) {
        for (const std::string& f : media.frames) {
            frames.push_back((extractDir / indexDir("media", media.index) / f).string());
        }
        return fs::exists(frames.front(), ec) ? frames : std::vector<std::string>{};
    }

    const fs::path originalDir = fs::path(media.originalPath).parent_path();
    bool allThere = true;
    for (const std::string& f : media.frames) {
        frames.push_back((originalDir / f).string());
        if (!fs::exists(frames.back(), ec)) allThere = false;
    }
    if (allThere) return frames;

    frames = findByFingerprint(media, services.searchPaths, services.searchRecursive);
    if (!frames.empty() || !services.interactive || !services.locate) return frames;

    const std::string chosen = services.locate(media);
    if (chosen.empty()) return {};
    auto sequences = gfcMediaFingerprint::sequencesIn(fs::path(chosen).parent_path().string(), false);
    const auto found = sequences.find(gfcNoteStore::normalisePath(chosen));
    if (found == sequences.end()) return {};
    if (gfcMediaFingerprint::compute(found->second, nullptr) == media.fingerprint ||
        (services.confirmMismatch && services.confirmMismatch(media, chosen))) {
        return found->second;
    }
    return {};
}
}  // namespace

bool openPackage(const std::string& packagePath, const std::string& cacheRoot, const OpenServices& services,
                 OpenResult& result, QString* err) {
    namespace fs = std::filesystem;
    auto fail = [err](const std::string& message) {
        if (err) *err = QString::fromStdString(message);
        return false;
    };
    result = OpenResult{};

    gfcTar::Reader reader;
    std::string e;
    if (!reader.open(packagePath, &e)) return fail("Cannot read the package: " + e);
    if (reader.entries().empty() || reader.entries()[0].name != "manifest.json") {
        return fail("Not a JefeCheck review package: manifest.json is missing");
    }
    std::string manifestBytes;
    if (!reader.readBytes(reader.entries()[0], manifestBytes, &e)) return fail("Cannot read the package: " + e);
    if (!manifestFromJson(QByteArray::fromStdString(manifestBytes), result.manifest, err)) return false;
    const Manifest& manifest = result.manifest;

    std::error_code ec;
    const std::string absolute = fs::absolute(packagePath, ec).string();
    const uint64_t size = fs::file_size(packagePath, ec);
    const long long mtime = static_cast<long long>(fs::last_write_time(packagePath, ec).time_since_epoch().count());
    const fs::path dir = fs::path(cacheRoot) / gfcSha1::hex(absolute + "|" + std::to_string(size) + "|" + std::to_string(mtime));
    result.extractDir = dir.string();
    if (!fs::exists(dir / ".complete", ec)) {
        fs::remove_all(dir, ec);
        for (const gfcTar::Entry& entry : reader.entries()) {
            // Reader::open has already refused absolute and ".." names.
            if (!reader.extractTo(entry, (dir / entry.name).string(), &e)) return fail("Cannot extract the package: " + e);
        }
        std::ofstream marker((dir / ".complete").string());
        marker << "ok\n";
        if (!marker) return fail("Cannot write to " + dir.string());
    }

    for (const ManifestLut& lut : manifest.luts) {
        if (services.loadLut) services.loadLut((dir / lut.file).string());
    }

    const std::string sessionXml = readText((dir / manifest.session).string());
    std::vector<gfcSessionPaths::MediaRef> refs;
    if (!gfcSessionPaths::listMedia(sessionXml, refs, &e)) return fail("The package's session is unreadable: " + e);

    std::map<std::string, std::string> mapping;
    for (const ManifestMedia& media : manifest.media) {
        const std::vector<std::string> frames = resolveMedia(media, manifest.mediaIncluded, dir, services);
        if (frames.empty()) {
            ++result.missing;
            result.missingMedia.push_back(fs::path(media.originalPath).filename().string());
            continue;
        }
        ++result.resolved;

        // Every session reference to this media points at the same frame of the
        // resolved sequence (matched by position, since a relinked sequence may
        // be named differently).
        const std::string packagedPrefix = indexDir("media", media.index) + "/";
        for (const gfcSessionPaths::MediaRef& ref : refs) {
            const bool ours = ref.path.rfind(packagedPrefix, 0) == 0 ||
                              gfcNoteStore::normalisePath(ref.path) == media.originalPath;
            if (!ours) continue;
            const std::string name = fs::path(ref.path).filename().string();
            const auto at = std::find(media.frames.begin(), media.frames.end(), name);
            const size_t index = (at == media.frames.end()) ? 0 : size_t(at - media.frames.begin());
            mapping[ref.path] = frames[std::min(index, frames.size() - 1)];
        }

        // Notes: union into whatever the resolved media already has.
        const std::string resolvedMedia = gfcNoteStore::normalisePath(frames.front());
        gfcReview incoming;
        if (gfcNoteStore::fromXmlString(readText((dir / media.notes).string()), incoming)) {
            gfcReview local;
            local.mediaPath = resolvedMedia;
            gfcNoteStore::load(resolvedMedia, local);
            local.mediaPath = resolvedMedia;   // a loaded sidecar may name an older path
            gfcNoteMerge::mergeInto(local, std::move(incoming));
            if (local.fingerprint.empty()) local.fingerprint = media.fingerprint;
            gfcNoteStore::save(local);
        }
        if (services.reloadReview) services.reloadReview(resolvedMedia);
    }

    std::string rewritten;
    if (!gfcSessionPaths::rewriteMedia(sessionXml, mapping, rewritten, &e)) return fail(e);
    const fs::path sessionOut = dir / "session.opened.jcs";
    writeText(sessionOut.string(), rewritten);
    if (readText(sessionOut.string()) != rewritten) return fail("Cannot write " + sessionOut.string());
    result.sessionPath = sessionOut.string();
    gfcSessionPaths::listFxNames(rewritten, result.fxNames, nullptr);
    return true;
}
```

`readText`, `writeText` and `runToEnd` are the helpers in the anonymous namespace that Task 6 added; move that anonymous namespace above `findByFingerprint` if the compiler reports them undeclared.

- [ ] **Step 5: Build and run — expect pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: `NOTE-PACKAGE-OPEN: pass=16 fail=0`; every other `NOTE-*` line `fail=0`; exit 0.

- [ ] **Step 6: Commit**

`git add src/qt/ReviewPackage_qt.h src/qt/ReviewPackage_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: open a review package and find its media wherever it went`.

---

### Task 9: Open a package in the running app

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp` (after the Task 7 functions)
- Modify: `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp`
- Modify: `src/main_qt.cpp` (flags after `--export-package`)

**Interfaces:**
- Consumes: Task 8 `jefe::qt::package::{OpenServices, OpenResult, ManifestMedia, openPackage}`; Task 7 `PackageStats`, `exportReviewPackage`, `makePackageFixture`, `runHeadlessPackageTest`. Bridge internals: `g_noteReviews` (`std::vector<std::unique_ptr<gfcReview>>`), `syncPlateNotesImpl()`, `sett.searchPaths`, `sett.searchPathsRecursive`, `sett.useSearchPaths`, `fxManager.getFXIndexByName(std::string) -> int` (-1 when not loaded). App API: `jefe::qt::loadLUTFile(const std::string&) -> bool` (needs the GL context current), `jefe::qt::loadSession`, `jefe::qt::startLoadingAllTracks`, `jefe::qt::getTrackParams(int).filename`, `MainWindow_Qt::refreshAfterSessionLoad()`, `MainWindow_Qt::updateSessionTitle()`, member `currentSessionPath_`.
- Produces:
  - bridge: `void reloadReviewFromDisk(const std::string& mediaPath); std::vector<std::string> getSearchPaths(); bool getSearchPathsRecursive(); void setSearchPaths(const std::vector<std::string>& paths, bool recursive, bool enabled); bool isFxLoaded(const std::string& fxName);`
  - `bool MainWindow_Qt::openReviewPackage(const QString& packagePath, bool interactive, PackageStats* stats, QString* message);`
  - `int MainWindow_Qt::runHeadlessRelinkTest(const QString& imagePath);`
  - member `QString packageTitle_;`
  - CLI: `--relink-test <image>`, `--open-package <file>`

- [ ] **Step 1: Bridge functions**

In `src/qt/SequenceLoadBridge_qt.h`, after `isInstallLutPath`, add:

```cpp
/** Drops the in-memory review for @a mediaPath and republishes plate notes, so
    the next use reads its sidecar from disk again (after a package merge). */
void reloadReviewFromDisk(const std::string& mediaPath);

/** Preferences -> Search Paths. */
std::vector<std::string> getSearchPaths();
bool getSearchPathsRecursive();
/** Sets the search paths for this run (not persisted); @a enabled is "use search paths". */
void setSearchPaths(const std::vector<std::string>& paths, bool recursive, bool enabled);

/** Whether an FX with this name is loaded. */
bool isFxLoaded(const std::string& fxName);
```

In `src/qt/SequenceLoadBridge_qt.cpp`, after `isInstallLutPath`, add:

```cpp
void reloadReviewFromDisk(const std::string& mediaPath) {
    // Plates borrow pointers into the review: move it out first, republish
    // (which loads the sidecar afresh for any plate showing this media), and
    // only then let the old copy go.
    std::vector<std::unique_ptr<gfcReview>> stale;
    for (auto it = g_noteReviews.begin(); it != g_noteReviews.end();) {
        if ((*it)->mediaPath == mediaPath) {
            stale.push_back(std::move(*it));
            it = g_noteReviews.erase(it);
        } else {
            ++it;
        }
    }
    syncPlateNotesImpl();
}

std::vector<std::string> getSearchPaths() { return sett.searchPaths; }

bool getSearchPathsRecursive() { return sett.searchPathsRecursive; }

void setSearchPaths(const std::vector<std::string>& paths, bool recursive, bool enabled) {
    sett.searchPaths = paths;
    sett.searchPathsRecursive = recursive;
    sett.useSearchPaths = enabled;
}

bool isFxLoaded(const std::string& fxName) {
    return fxManager.getFXIndexByName(fxName) >= 0;
}
```

- [ ] **Step 2: MainWindow declarations and title**

In `src/qt/MainWindow_qt.h`, after `runHeadlessPackageTest`, add:

```cpp
    /**
     * Opens a review package: extracts it, finds its media, merges its notes and
     * loads its session. @a interactive allows "Locate…" prompts for media it
     * cannot find; without it they count as missing. See
     * docs/superpowers/specs/2026-09-14-review-package-design.md.
     */
    bool openReviewPackage(const QString& packagePath, bool interactive, PackageStats* stats, QString* message);

    /** Headless proof that a lean package relinks moved media (--relink-test <image>). */
    int runHeadlessRelinkTest(const QString& imagePath);
```

and beside `currentSessionPath_` in the private members:

```cpp
    QString packageTitle_;   // the open review package's file name, when the session came from one
```

In `src/qt/MainWindow_qt.cpp`, change `updateSessionTitle()` to:

```cpp
void MainWindow_Qt::updateSessionTitle() {
    if (!currentSessionPath_.isEmpty()) {
        setWindowTitle(QString("JefeCheck — %1").arg(QFileInfo(currentSessionPath_).fileName()));
    } else if (!packageTitle_.isEmpty()) {
        setWindowTitle(QString("JefeCheck — %1").arg(packageTitle_));
    } else {
        setWindowTitle("JefeCheck");
    }
}
```

and in `openSessionPath`, set `packageTitle_.clear();` immediately before its `updateSessionTitle();` call.

- [ ] **Step 3: Tests and flags (failing)**

In `src/qt/MainWindow_qt.cpp`, add includes (skip any present): `<QStandardPaths>`, `<filesystem>`, `"../xmlParser.h"`. Add a stub:

```cpp
bool MainWindow_Qt::openReviewPackage(const QString&, bool, PackageStats*, QString* message) {
    if (message) *message = tr("Not implemented");
    return false;
}
```

In `runHeadlessPackageTest`, replace the line `// (Task 9 inserts the open round trip here.)` with:

```cpp
    // Round trip: open the package with media.
    const QString before = work + "/before.jcs";
    jefe::qt::saveSession(before.toStdString());
    check(openReviewPackage(withMedia, false, &stats, &msg), "the package with media opens");
    printf("PACKAGE-TEST open: %s\n", qPrintable(msg));
    check(stats.resolved == 1 && stats.missing == 0, "media resolved from the package");
    const QString loaded = QString::fromStdString(jefe::qt::getTrackParams(0).filename);
    check(!stats.extractDir.isEmpty() &&
          QFileInfo(loaded).canonicalFilePath().startsWith(QFileInfo(stats.extractDir).canonicalFilePath()),
          "the track loads media from the extraction directory");
    check(readBytes(loaded) == readBytes(media), "the extracted media is byte-identical");
    gfcReview original;
    gfcReview reopened;
    const bool bothLoaded = gfcNoteStore::load(mediaKey, original) &&
                            gfcNoteStore::load(gfcNoteStore::normalisePath(loaded.toStdString()), reopened);
    original.mediaPath.clear();
    reopened.mediaPath.clear();
    check(bothLoaded && gfcNoteStore::toJsonString(original) == gfcNoteStore::toJsonString(reopened),
          "notes are identical after the round trip");
    const QString after = work + "/after.jcs";
    jefe::qt::saveSession(after.toStdString());
    auto plateAttr = [&readBytes](const QString& jcs, const char* name) {
        const QByteArray xml = readBytes(jcs);
        XMLResults results;
        XMLNode top = XMLNode::parseString(xml.constData(), NULL, &results);
        XMLNode plate = top.getChildNode("root").getChildNode("plates").getChildNode("plate", 0);
        XMLCSTR value = plate.isEmpty() ? nullptr : plate.getAttribute(name);
        return QString(value ? value : "");
    };
    check(!plateAttr(before, "exposure").isEmpty() &&
          plateAttr(before, "exposure") == plateAttr(after, "exposure") &&
          plateAttr(before, "gamma") == plateAttr(after, "gamma") &&
          plateAttr(before, "lut") == plateAttr(after, "lut"),
          "plate colour correction and LUT survive the round trip");
    check(openReviewPackage(withMedia, false, &stats, &msg), "opening the same package again works");

    const QByteArray packageBytes = readBytes(withMedia);
    const QString truncated = work + "/truncated.jcreview";
    {
        QFile t(truncated);
        if (t.open(QIODevice::WriteOnly)) t.write(packageBytes.left(packageBytes.size() / 2));
    }
    const std::string trackBefore = jefe::qt::getTrackParams(0).filename;
    check(!openReviewPackage(truncated, false, &stats, &msg) && msg.contains("truncated"),
          "a truncated package is refused");
    check(jefe::qt::getTrackParams(0).filename == trackBefore, "a refused package changes nothing");
    if (!stats.extractDir.isEmpty()) QDir(stats.extractDir).removeRecursively();
```

After `runHeadlessPackageTest`, add:

```cpp
int MainWindow_Qt::runHeadlessRelinkTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("RELINK-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    if (!viewport_) { printf("RELINK-TEST FAIL no viewport\n"); fflush(stdout); return 2; }

    const QString work = QDir::tempPath() + "/jefecheck_relinktest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString media = makePackageFixture(imagePath, work);
    if (media.isEmpty()) { printf("RELINK-TEST FAIL fixture\n"); fflush(stdout); return 2; }
    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);

    PackageStats stats;
    QString msg;
    const QString lean = work + "/lean.jcreview";
    check(exportReviewPackage(lean, false, &stats, &msg), "package without media exported");

    const QString movedDir = work + "/moved/deep";
    QDir().mkpath(movedDir);
    const QString moved = movedDir + "/" + QFileInfo(media).fileName();
    check(QFile::rename(media, moved), "the media is moved away from its recorded path");
    // "use search paths" off: the fingerprint relink must not depend on it.
    jefe::qt::setSearchPaths({(work + "/moved").toStdString()}, true, false);

    check(openReviewPackage(lean, false, &stats, &msg), "the package opens");
    printf("RELINK-TEST open: %s resolved=%d missing=%d\n", qPrintable(msg), stats.resolved, stats.missing);
    check(stats.resolved == 1 && stats.missing == 0, "the media is resolved by fingerprint");
    check(QFileInfo(QString::fromStdString(jefe::qt::getTrackParams(0).filename)).canonicalFilePath() ==
          QFileInfo(moved).canonicalFilePath(),
          "the track loads the moved media");
    gfcReview relinked;
    check(gfcNoteStore::load(gfcNoteStore::normalisePath(moved.toStdString()), relinked) &&
          relinked.revisions.size() == 1,
          "the notes follow the media");
    if (!stats.extractDir.isEmpty()) QDir(stats.extractDir).removeRecursively();

    printf("RELINK-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}
```

In `src/main_qt.cpp`, after the `--export-package` loop, add:

```cpp
    // --relink-test <image>: a lean package finds moved media by fingerprint.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--relink-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessRelinkTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --open-package <file>: open a review package and keep running.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--open-package") != 0) continue;
        const QString file = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(3000, &window, [&window, file]() {
            MainWindow_Qt::PackageStats s;
            QString msg;
            if (window.openReviewPackage(file, false, &s, &msg)) {
                printf("PACKAGE: opened=%s media=%d resolved=%d missing=%d\n", qPrintable(file), s.media,
                       s.resolved, s.missing);
            } else {
                printf("PACKAGE: FAIL %s\n", qPrintable(msg));
            }
            fflush(stdout);
        });
        break;
    }
```

- [ ] **Step 4: Build and run — expect failures**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --package-test /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr 2>&1 | grep PACKAGE-TEST; echo "exit=${pipestatus[1]}"`
Expected: the Task 7 checks still `ok`; `PACKAGE-TEST FAIL the package with media opens` and the dependent checks fail; `exit=2`.

- [ ] **Step 5: Implement `openReviewPackage`**

Replace the stub with:

```cpp
bool MainWindow_Qt::openReviewPackage(const QString& packagePath, bool interactive, PackageStats* stats, QString* message) {
    namespace pkg = jefe::qt::package;
    auto say = [&](const QString& m) { if (message) *message = m; };
    if (!viewport_) {
        say(tr("No viewport"));
        return false;
    }

    pkg::OpenServices services;
    services.loadLut = [this](const std::string& path) {
        viewport_->makeCurrent();   // loading a LUT creates GL textures
        const bool ok = jefe::qt::loadLUTFile(path);
        viewport_->doneCurrent();
        return ok;
    };
    services.reloadReview = [](const std::string& mediaPath) { jefe::qt::reloadReviewFromDisk(mediaPath); };
    services.searchPaths = jefe::qt::getSearchPaths();
    services.searchRecursive = jefe::qt::getSearchPathsRecursive();
    services.interactive = interactive;
    services.locate = [this](const pkg::ManifestMedia& media) {
        const QString name = QString::fromStdString(std::filesystem::path(media.originalPath).filename().string());
        return QFileDialog::getOpenFileName(this, tr("Locate %1").arg(name)).toStdString();
    };
    services.confirmMismatch = [this](const pkg::ManifestMedia&, const std::string& chosen) {
        return QMessageBox::question(
                   this, tr("Media does not match"),
                   tr("%1 does not match the media recorded in the package. Use it anyway?")
                       .arg(QString::fromStdString(chosen))) == QMessageBox::Yes;
    };

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/packages";
    QDir().mkpath(cacheRoot);
    pkg::OpenResult result;
    QString err;
    if (!pkg::openPackage(packagePath.toStdString(), cacheRoot.toStdString(), services, result, &err)) {
        say(err);
        return false;
    }

    viewport_->makeCurrent();   // loadSession uploads preview textures
    const bool loaded = jefe::qt::loadSession(result.sessionPath);
    if (loaded) jefe::qt::startLoadingAllTracks();
    viewport_->doneCurrent();
    if (!loaded) {
        say(tr("Could not load the package's session"));
        return false;
    }

    // The extracted session is not a file the user chose: Save Session asks where.
    currentSessionPath_.clear();
    packageTitle_ = QFileInfo(packagePath).fileName();
    updateSessionTitle();
    refreshAfterSessionLoad();

    PackageStats s;
    s.media = int(result.manifest.media.size());
    s.mediaIncluded = result.manifest.mediaIncluded;
    s.bytes = QFileInfo(packagePath).size();
    s.resolved = result.resolved;
    s.missing = result.missing;
    s.extractDir = QString::fromStdString(result.extractDir);
    for (const std::string& name : result.missingMedia) s.missingMedia << QString::fromStdString(name);
    for (const std::string& fx : result.fxNames) {
        if (!jefe::qt::isFxLoaded(fx)) s.missingFx << QString::fromStdString(fx);
    }
    if (stats) *stats = s;
    say(tr("Opened review package %1: %2 of %3 media found")
            .arg(QFileInfo(packagePath).fileName())
            .arg(s.resolved)
            .arg(s.media));
    return true;
}
```

- [ ] **Step 6: Build and run both tests — expect pass**

Run the command from Step 4, then the same with `--relink-test` in place of `--package-test` (grep `RELINK-TEST`).
Expected: `PACKAGE-TEST: PASS` with `exit=0`; `RELINK-TEST: PASS` with `exit=0`. `--notes-test` still exits 0.
If "the track loads the moved media" fails while `resolved=1`, print both paths: a difference only in symlinks (`/var` vs `/private/var`) means the comparison needs canonical paths on both sides, which it already uses — look instead at whether the session rewrite mapped the track's reference (Task 8 matches references by `normalisePath(ref) == originalPath`).

- [ ] **Step 7: CLI check**

Run: `T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && cp /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr "$T/" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --open-file "$T/Blobbies.exr" --export-package "$T/cli.jcreview" 2>&1 | grep '^PACKAGE:' && (./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --open-package "$T/cli.jcreview" > "$T/open.log" 2>&1 &) ; sleep 8; grep '^PACKAGE:' "$T/open.log"; pkill -f "open-package $T/cli.jcreview"`
Expected: `PACKAGE: wrote=…` then `PACKAGE: opened=<T>/cli.jcreview media=1 resolved=1 missing=0`.

- [ ] **Step 8: Commit**

`git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: open a review package into the app`.

---

### Task 10: Export dialog and File menu actions

**Files:**
- Create: `src/qt/ReviewPackageDialog_qt.h`, `src/qt/ReviewPackageDialog_qt.cpp`
- Modify: `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp` (test + two File menu actions after "Export Review Summary…")
- Modify: `src/main_qt.cpp` (flag after `--open-package`)

**Interfaces:**
- Consumes: Task 6 `jefe::qt::package::{ExportInput, Exporter}`; Task 7 `MainWindow_Qt::{gatherPackageInput, packageMediaBytes, makePackageFixture, PackageStats}`; Task 9 `MainWindow_Qt::openReviewPackage`; `jefe::qticons::folder()` from `src/qt/qticons.h`.
- Produces:
  - `class ReviewPackageDialog_Qt : public QDialog { using Gather = std::function<bool(const QString& outPath, bool includeMedia, jefe::qt::package::ExportInput& input, QString* message)>; ReviewPackageDialog_Qt(Gather, qint64 mediaBytes, QWidget* parent = nullptr); void setOutputPath(const QString&); void setIncludeMedia(bool); void startExport(); void cancelExport(); bool isRunning() const; int progressPercent() const; QString lastMessage() const; static QString formatBytes(qint64); };`
  - `int MainWindow_Qt::runHeadlessPackageDialogTest(const QString& imagePath);`
  - File menu: "Export Review Package…" (`menu.file.exportpackage`), "Open Review Package…" (`menu.file.openpackage`)
  - CLI: `--package-dialog-test <image>`

- [ ] **Step 1: Write the dialog header**

Create `src/qt/ReviewPackageDialog_qt.h`:

```cpp
// Export Review Package dialog: output path, "Include media" with its size,
// progress and cancel (docs/superpowers/specs/2026-09-14-review-package-design.md).
#ifndef JEFECHECK_QT_REVIEW_PACKAGE_DIALOG_H
#define JEFECHECK_QT_REVIEW_PACKAGE_DIALOG_H

#include <QDialog>
#include <QString>

#include <functional>

#include "ReviewPackage_qt.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class ReviewPackageDialog_Qt : public QDialog {
    Q_OBJECT
public:
    /** Fills the export input for @a outPath (MainWindow_Qt::gatherPackageInput). */
    using Gather = std::function<bool(const QString& outPath, bool includeMedia,
                                      jefe::qt::package::ExportInput& input, QString* message)>;

    ReviewPackageDialog_Qt(Gather gather, qint64 mediaBytes, QWidget* parent = nullptr);
    ~ReviewPackageDialog_Qt() override;

    void setOutputPath(const QString& path);
    void setIncludeMedia(bool on);
    /** Starts exporting, as the Export button does. */
    void startExport();
    /** Stops a running export, as Cancel does while exporting. */
    void cancelExport();
    bool isRunning() const { return running_; }
    int progressPercent() const;
    QString lastMessage() const { return lastMessage_; }

    /** "0 B", "1.5 KB", "5.0 GB". */
    static QString formatBytes(qint64 bytes);

protected:
    void reject() override;

private:
    void stepOnce();
    void finishWith(bool ok, const QString& message);
    void setInputsEnabled(bool enabled);
    void updateSizeLabel();

    Gather gather_;
    qint64 mediaBytes_ = 0;
    QLineEdit* pathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QCheckBox* includeMediaCheck_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    jefe::qt::package::Exporter exporter_;
    QString outPath_;
    bool running_ = false;
    QString lastMessage_;
};

#endif
```

- [ ] **Step 2: Write the headless test, the flag and a stub dialog (failing)**

Create `src/qt/ReviewPackageDialog_qt.cpp` with stubs:

```cpp
#include "ReviewPackageDialog_qt.h"

ReviewPackageDialog_Qt::ReviewPackageDialog_Qt(Gather gather, qint64 mediaBytes, QWidget* parent)
    : QDialog(parent), gather_(std::move(gather)), mediaBytes_(mediaBytes) {}
ReviewPackageDialog_Qt::~ReviewPackageDialog_Qt() = default;
void ReviewPackageDialog_Qt::setOutputPath(const QString&) {}
void ReviewPackageDialog_Qt::setIncludeMedia(bool) {}
void ReviewPackageDialog_Qt::startExport() {}
void ReviewPackageDialog_Qt::cancelExport() {}
int ReviewPackageDialog_Qt::progressPercent() const { return 0; }
QString ReviewPackageDialog_Qt::formatBytes(qint64) { return {}; }
void ReviewPackageDialog_Qt::reject() { QDialog::reject(); }
void ReviewPackageDialog_Qt::stepOnce() {}
void ReviewPackageDialog_Qt::finishWith(bool, const QString&) {}
void ReviewPackageDialog_Qt::setInputsEnabled(bool) {}
void ReviewPackageDialog_Qt::updateSizeLabel() {}
```

In `src/qt/MainWindow_qt.h`, after `runHeadlessRelinkTest`, add:

```cpp
    /** Headless proof of the export dialog (--package-dialog-test <image>). */
    int runHeadlessPackageDialogTest(const QString& imagePath);
```

In `src/qt/MainWindow_qt.cpp`, add `#include "ReviewPackageDialog_qt.h"`, `<QElapsedTimer>` and `<QCoreApplication>` (skip any present), and after `runHeadlessRelinkTest` add:

```cpp
int MainWindow_Qt::runHeadlessPackageDialogTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("PACKAGE-DIALOG-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    const QString work = QDir::tempPath() + "/jefecheck_packagedialogtest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString media = makePackageFixture(imagePath, work);
    if (media.isEmpty()) { printf("PACKAGE-DIALOG-TEST FAIL fixture\n"); fflush(stdout); return 2; }
    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);

    check(ReviewPackageDialog_Qt::formatBytes(0) == "0 B" &&
          ReviewPackageDialog_Qt::formatBytes(1536) == "1.5 KB" &&
          ReviewPackageDialog_Qt::formatBytes(5LL * 1024 * 1024 * 1024) == "5.0 GB",
          "sizes are formatted for people");

    ReviewPackageDialog_Qt dialog(
        [this](const QString& out, bool includeMedia, jefe::qt::package::ExportInput& input, QString* message) {
            return gatherPackageInput(out, includeMedia, input, message);
        },
        packageMediaBytes(), this);
    auto waitUntilIdle = [&dialog]() {
        QElapsedTimer clock;
        clock.start();
        while (dialog.isRunning() && clock.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };

    const QString out = work + "/dialog.jcreview";
    dialog.setOutputPath(out);
    dialog.setIncludeMedia(true);
    dialog.startExport();
    check(dialog.isRunning(), "the export starts");
    waitUntilIdle();
    printf("PACKAGE-DIALOG-TEST message: %s\n", qPrintable(dialog.lastMessage()));
    check(!dialog.isRunning() && QFileInfo::exists(out) && dialog.progressPercent() == 100,
          "the export finishes with progress at 100%");

    const QString cancelled = work + "/cancelled.jcreview";
    dialog.setOutputPath(cancelled);
    dialog.startExport();
    dialog.cancelExport();
    QCoreApplication::processEvents();
    check(!dialog.isRunning() && !QFileInfo::exists(cancelled) && !QFileInfo::exists(cancelled + ".partial"),
          "cancel leaves neither package nor partial file");

    dialog.setOutputPath(QString());
    dialog.startExport();
    check(!dialog.isRunning() && !dialog.lastMessage().isEmpty(), "an empty path is refused with a message");

    printf("PACKAGE-DIALOG-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}
```

In `src/main_qt.cpp`, after the `--open-package` loop, add:

```cpp
    // --package-dialog-test <image>: the export dialog writes, cancels and refuses.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--package-dialog-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessPackageDialogTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }
```

- [ ] **Step 3: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --package-dialog-test /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr 2>&1 | grep PACKAGE-DIALOG-TEST; echo "exit=${pipestatus[1]}"`
Expected: `PACKAGE-DIALOG-TEST FAIL` lines, `PACKAGE-DIALOG-TEST: FAIL`, `exit=2`. If the link fails with missing `staticMetaObject`/vtable symbols for `ReviewPackageDialog_Qt`, run `rm -rf build_qt/jefecheck_autogen` and rebuild.

- [ ] **Step 4: Implement the dialog**

Replace the contents of `src/qt/ReviewPackageDialog_qt.cpp` with:

```cpp
#include "ReviewPackageDialog_qt.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "qticons.h"

ReviewPackageDialog_Qt::ReviewPackageDialog_Qt(Gather gather, qint64 mediaBytes, QWidget* parent)
    : QDialog(parent), gather_(std::move(gather)), mediaBytes_(mediaBytes) {
    setWindowTitle(tr("Export Review Package"));
    setObjectName("dialog.package");

    pathEdit_ = new QLineEdit(this);
    pathEdit_->setObjectName("dialog.package.path.edit");
    pathEdit_->setPlaceholderText(tr("Package file (.jcreview)"));
    browseButton_ = new QPushButton(jefe::qticons::folder(), tr("Browse…"), this);
    browseButton_->setObjectName("dialog.package.browse.button");
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit_, 1);
    pathRow->addWidget(browseButton_);

    includeMediaCheck_ = new QCheckBox(tr("Include media"), this);
    includeMediaCheck_->setObjectName("dialog.package.includemedia.check");
    includeMediaCheck_->setChecked(true);
    includeMediaCheck_->setToolTip(tr("Copy the footage into the package so it opens anywhere. "
                                      "Untick for shared storage: the package then finds the media by its fingerprint."));
    sizeLabel_ = new QLabel(this);
    sizeLabel_->setObjectName("dialog.package.size.label");
    auto* mediaRow = new QHBoxLayout;
    mediaRow->addWidget(includeMediaCheck_);
    mediaRow->addWidget(sizeLabel_, 1);

    progress_ = new QProgressBar(this);
    progress_->setObjectName("dialog.package.progress");
    progress_->setRange(0, 100);
    progress_->setValue(0);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("dialog.package.status.label");
    statusLabel_->setWordWrap(true);

    exportButton_ = new QPushButton(tr("Export"), this);
    exportButton_->setObjectName("dialog.package.export.button");
    exportButton_->setDefault(true);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setObjectName("dialog.package.cancel.button");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(exportButton_);

    auto* form = new QFormLayout;
    form->addRow(tr("File:"), pathRow);
    form->addRow(tr("Media:"), mediaRow);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(progress_);
    layout->addWidget(statusLabel_);
    layout->addLayout(buttons);

    connect(browseButton_, &QPushButton::clicked, this, [this]() {
        QString out = QFileDialog::getSaveFileName(this, tr("Export Review Package"), pathEdit_->text(),
                                                   tr("JefeCheck Review Package (*.jcreview)"));
        if (out.isEmpty()) return;
        if (QFileInfo(out).suffix().isEmpty()) out += ".jcreview";
        pathEdit_->setText(out);
    });
    connect(includeMediaCheck_, &QCheckBox::toggled, this, [this]() { updateSizeLabel(); });
    connect(exportButton_, &QPushButton::clicked, this, [this]() { startExport(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        if (running_) cancelExport();
        else reject();
    });
    updateSizeLabel();
}

ReviewPackageDialog_Qt::~ReviewPackageDialog_Qt() {
    if (running_) exporter_.cancel();
}

void ReviewPackageDialog_Qt::setOutputPath(const QString& path) { pathEdit_->setText(path); }

void ReviewPackageDialog_Qt::setIncludeMedia(bool on) { includeMediaCheck_->setChecked(on); }

QString ReviewPackageDialog_Qt::formatBytes(qint64 bytes) {
    static const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) return QString("%1 B").arg(bytes);
    return QString("%1 %2").arg(value, 0, 'f', 1).arg(units[unit]);
}

int ReviewPackageDialog_Qt::progressPercent() const {
    const qint64 total = exporter_.bytesTotal();
    return total > 0 ? int((exporter_.bytesDone() * 100) / total) : 0;
}

void ReviewPackageDialog_Qt::startExport() {
    if (running_) return;
    QString out = pathEdit_->text().trimmed();
    if (out.isEmpty()) {
        finishWith(false, tr("Choose where to write the package"));
        return;
    }
    if (QFileInfo(out).suffix().isEmpty()) out += ".jcreview";

    jefe::qt::package::ExportInput input;
    QString message;
    if (!gather_(out, includeMediaCheck_->isChecked(), input, &message)) {
        finishWith(false, message);
        return;
    }
    if (!exporter_.begin(input, &message)) {
        finishWith(false, message);
        return;
    }
    outPath_ = out;
    running_ = true;
    progress_->setValue(0);
    statusLabel_->setText(tr("Writing %1…").arg(QFileInfo(out).fileName()));
    setInputsEnabled(false);
    QTimer::singleShot(0, this, [this]() { stepOnce(); });
}

void ReviewPackageDialog_Qt::stepOnce() {
    if (!running_) return;
    using State = jefe::qt::package::Exporter::State;
    QString err;
    const State state = exporter_.step(&err);
    progress_->setValue(progressPercent());
    if (state == State::Running) {
        QTimer::singleShot(0, this, [this]() { stepOnce(); });
        return;
    }
    running_ = false;
    if (state == State::Done) {
        finishWith(true, tr("Review package written: %1").arg(outPath_));
    } else {
        finishWith(false, err.isEmpty() ? tr("Export failed") : err);
    }
}

void ReviewPackageDialog_Qt::cancelExport() {
    if (!running_) return;
    exporter_.cancel();
    running_ = false;
    finishWith(false, tr("Export cancelled"));
}

void ReviewPackageDialog_Qt::reject() {
    if (running_) cancelExport();
    QDialog::reject();
}

void ReviewPackageDialog_Qt::finishWith(bool ok, const QString& message) {
    lastMessage_ = message;
    statusLabel_->setText(message);
    setInputsEnabled(true);
    if (ok) {
        progress_->setValue(100);
        cancelButton_->setText(tr("Close"));
    }
}

void ReviewPackageDialog_Qt::setInputsEnabled(bool enabled) {
    exportButton_->setEnabled(enabled);
    pathEdit_->setEnabled(enabled);
    browseButton_->setEnabled(enabled);
    includeMediaCheck_->setEnabled(enabled);
}

void ReviewPackageDialog_Qt::updateSizeLabel() {
    sizeLabel_->setText(includeMediaCheck_->isChecked()
                            ? tr("adds %1 of media").arg(formatBytes(mediaBytes_))
                            : tr("media is referenced, not copied"));
}
```

- [ ] **Step 5: Build and run — expect pass**

Run the command from Step 3.
Expected: every `PACKAGE-DIALOG-TEST ok`, `PACKAGE-DIALOG-TEST: PASS`, `exit=0`.

- [ ] **Step 6: Add the File menu actions**

In `src/qt/MainWindow_qt.cpp`, directly after the "Export Review Summary…" action (`->setObjectName("menu.file.exportsummary");`), add:

```cpp
    fileMenu->addAction(tr("Export Review Package…"), this, [this]() {
        ReviewPackageDialog_Qt dialog(
            [this](const QString& out, bool includeMedia, jefe::qt::package::ExportInput& input, QString* message) {
                return gatherPackageInput(out, includeMedia, input, message);
            },
            packageMediaBytes(), this);
        dialog.exec();
        if (!dialog.lastMessage().isEmpty()) statusBar()->showMessage(dialog.lastMessage(), 8000);
    })->setObjectName("menu.file.exportpackage");

    fileMenu->addAction(tr("Open Review Package…"), this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Review Package"), QString(),
                                                          tr("JefeCheck Review Package (*.jcreview)"));
        if (path.isEmpty()) return;
        PackageStats stats;
        QString message;
        if (!openReviewPackage(path, true, &stats, &message)) {
            QMessageBox::warning(this, tr("Open Review Package"), message);
            return;
        }
        statusBar()->showMessage(message, 8000);
        QStringList problems;
        if (!stats.missingMedia.isEmpty()) problems << tr("Media not found: %1").arg(stats.missingMedia.join(", "));
        if (!stats.missingFx.isEmpty()) problems << tr("FX not installed: %1").arg(stats.missingFx.join(", "));
        if (!problems.isEmpty()) {
            QMessageBox::information(this, tr("Open Review Package"), problems.join("\n"));
        }
    })->setObjectName("menu.file.openpackage");
```

- [ ] **Step 7: Full regression**

Run, one at a time, with a fresh settings directory as in Step 3: `--notes-test`, `--summary-test <image>`, `--package-test <image>`, `--relink-test <image>`, `--package-dialog-test <image>`.
Expected: `--notes-test` exits 0 with every `NOTE-*` line `fail=0`; each GL test prints its `: PASS` line and exits 0. Record all five results in the report.

- [ ] **Step 8: Commit**

`git add src/qt/ReviewPackageDialog_qt.h src/qt/ReviewPackageDialog_qt.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: export and open review packages from the File menu`.

---

## Self-Review

- **Spec coverage:** container format, entry order and limits (Tasks 2, 6); manifest fields including width/height and safe-name rules (Task 6); fingerprint definition, SHA-1 lifted from the notes store (Tasks 1, 3); export steps — media set, temp session, fingerprint filled and saved back, notes copies, session path rewrite, non-install LUTs, incremental write to a partial with cancel (Tasks 6, 7, 10); open steps — manifest validation, cache extraction with `.complete` reuse, LUTs before the session, resolve (packaged → original → fingerprint search regardless of "use search paths" → Locate with mismatch confirmation), notes union merge beside the resolved media, session rewrite, open through the normal session path, package title, missing media and FX reported (Tasks 8, 9, 10); `--export-package`, `--open-package` (keeps running) (Tasks 7, 9); tests `NOTE-SHA1`, `NOTE-TAR`, `NOTE-FINGERPRINT`, `NOTE-SESSIONPATHS`, `NOTE-MERGE`, `NOTE-PACKAGE`, `NOTE-PACKAGE-OPEN`, `--package-test`, `--relink-test`, `--package-dialog-test` (every task); error handling — unwritable output, long names, oversize entries, I/O failure with partial removal, unreadable/truncated/unknown-version packages, unsafe entry names (Tasks 2, 6, 8).
- **Type consistency:** `PackageStats`, `ExportInput`/`ExportMedia`, `Manifest`/`ManifestMedia`, `OpenServices`/`OpenResult`, `Exporter::State` are used with the same names and fields in Tasks 6–10.
