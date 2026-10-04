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

	check(gfcSha1::hex("/j/sh010.####.exr") == "4fc2c68eada5520ba61ac366d136ed48d2cf2faf", "a sidecar-style path hashes to the standard SHA-1");

	std::printf("NOTE-SHA1: pass=%d fail=%d\n", pass, fail);
	return fail;
}
