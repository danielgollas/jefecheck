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
