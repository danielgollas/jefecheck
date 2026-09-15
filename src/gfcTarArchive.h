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

	/** Relative, no leading '/', no backslash, no ':' (drive or stream), no root
	    name or root directory, no empty, "." or ".." segment. */
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
