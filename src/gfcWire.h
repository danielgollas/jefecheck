// jefe::wire — small, versioned, explicitly little-endian wire format.
//
// Replaces RakNet::BitStream/StringCompressor at message read/write sites
// (JEF-23). Header-only, no third-party or RakNet dependency so it can be
// used from both the RakNet transport TU and (later, JEF-24) a WebRTC
// transport TU without linking RakNet.
//
// Frame layout: [u8 version][u16 msgType, little-endian][payload...].
// `msgType` reuses the existing GFCNETID_* enum values. Version starts at 1;
// `readFrameHeader` rejects anything else so a version mismatch is a clean,
// silent drop rather than a misparse.
//
// All multi-byte integers are written/read explicitly byte-by-byte (little-
// endian) — never via memcpy/reinterpret_cast of the multi-byte value itself,
// so this is safe regardless of host endianness or alignment. Floats/doubles
// are transmitted as their IEEE-754 bit pattern: a single `memcpy` converts
// the scalar to same-width unsigned integer (this is the one sanctioned use
// of memcpy here — converting one scalar's bit pattern, not reading/writing
// multi-byte values directly off the wire), and that integer is then written
// with the same explicit little-endian byte routine as everything else.
//
// Reader is fully bounds-checked: every read that would run past the end of
// the buffer fails cleanly (returns false, sets an internal "bad" flag) and
// never touches memory outside the buffer. Once bad, all further reads
// short-circuit to false without moving the cursor.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace jefe {
namespace wire {

// Wire format version. Bump only with a coordinated protocol change; readers
// reject any frame whose version byte doesn't match.
constexpr uint8_t kWireVersion = 1;

// ---------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------
class Writer {
public:
    Writer() = default;

    void writeU8(uint8_t v) {
        buf_.push_back(v);
    }

    void writeU16(uint16_t v) {
        buf_.push_back(static_cast<unsigned char>(v & 0xFF));
        buf_.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    }

    void writeU32(uint32_t v) {
        buf_.push_back(static_cast<unsigned char>(v & 0xFF));
        buf_.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
    }

    void writeU64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf_.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
        }
    }

    void writeI32(int32_t v) {
        writeU32(static_cast<uint32_t>(v));
    }

    void writeF32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        writeU32(bits);
    }

    void writeF64(double v) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        writeU64(bits);
    }

    void writeBool(bool v) {
        writeU8(v ? 1 : 0);
    }

    // u32 length prefix + raw bytes (UTF-8 or otherwise — this layer is
    // encoding-agnostic, it just moves bytes).
    void writeString(const std::string& s) {
        writeU32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void writeBytes(const unsigned char* data, size_t len) {
        writeU32(static_cast<uint32_t>(len));
        if (len > 0) {
            buf_.insert(buf_.end(), data, data + len);
        }
    }
    void writeBytes(const std::vector<unsigned char>& bytes) {
        writeBytes(bytes.data(), bytes.size());
    }

    const unsigned char* data() const { return buf_.data(); }
    size_t size() const { return buf_.size(); }

private:
    std::vector<unsigned char> buf_;
};

// ---------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------
class Reader {
public:
    Reader(const unsigned char* data, size_t len)
        : data_(data), len_(len), pos_(0), ok_(true) {}

    bool ok() const { return ok_; }
    size_t remaining() const { return ok_ ? (len_ - pos_) : 0; }

    bool readU8(uint8_t& out) {
        if (!ok_ || remaining() < 1) return fail();
        out = data_[pos_];
        pos_ += 1;
        return true;
    }

    bool readU16(uint16_t& out) {
        if (!ok_ || remaining() < 2) return fail();
        uint16_t v = 0;
        v |= static_cast<uint16_t>(data_[pos_ + 0]) << 0;
        v |= static_cast<uint16_t>(data_[pos_ + 1]) << 8;
        pos_ += 2;
        out = v;
        return true;
    }

    bool readU32(uint32_t& out) {
        if (!ok_ || remaining() < 4) return fail();
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(data_[pos_ + i]) << (8 * i);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool readU64(uint64_t& out) {
        if (!ok_ || remaining() < 8) return fail();
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
        }
        pos_ += 8;
        out = v;
        return true;
    }

    bool readI32(int32_t& out) {
        uint32_t v = 0;
        if (!readU32(v)) return false;
        out = static_cast<int32_t>(v);
        return true;
    }

    bool readF32(float& out) {
        uint32_t bits = 0;
        if (!readU32(bits)) return false;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool readF64(double& out) {
        uint64_t bits = 0;
        if (!readU64(bits)) return false;
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool readBool(bool& out) {
        uint8_t v = 0;
        if (!readU8(v)) return false;
        out = (v != 0);
        return true;
    }

    // u32 length prefix + raw bytes. Rejects a length claiming more bytes
    // than actually remain in the buffer (malformed/truncated frame) before
    // touching memory.
    bool readString(std::string& out) {
        uint32_t n = 0;
        if (!readU32(n)) return false;
        if (!ok_ || static_cast<size_t>(n) > remaining()) return fail();
        out.assign(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return true;
    }

    bool readBytes(std::vector<unsigned char>& out) {
        uint32_t n = 0;
        if (!readU32(n)) return false;
        if (!ok_ || static_cast<size_t>(n) > remaining()) return fail();
        out.assign(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return true;
    }

private:
    bool fail() {
        ok_ = false;
        return false;
    }

    const unsigned char* data_;
    size_t len_;
    size_t pos_;
    bool ok_;
};

// ---------------------------------------------------------------------
// Frame helpers: [u8 version][u16 msgType LE]
// ---------------------------------------------------------------------
inline void beginFrame(Writer& w, uint16_t msgType) {
    w.writeU8(kWireVersion);
    w.writeU16(msgType);
}

// Reads and validates the frame header, yielding msgType. Returns false
// (without touching msgType) if the buffer is truncated or the version
// byte doesn't match kWireVersion.
inline bool readFrameHeader(Reader& r, uint16_t& msgType) {
    uint8_t version = 0;
    if (!r.readU8(version)) return false;
    if (version != kWireVersion) return false;
    uint16_t type = 0;
    if (!r.readU16(type)) return false;
    msgType = type;
    return true;
}

}  // namespace wire
}  // namespace jefe
