// jefe::wire: small, versioned, little-endian wire format, header-only.
// Frame: [u8 version][u16 msgType LE][payload]; the Reader is bounds-checked and sticky-fails.

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

    // True unless a writeString/writeBytes was refused because its length
    // exceeded the u32 length-prefix range (>= 4GB). Integer/float/bool
    // writes cannot fail. Callers building outgoing frames should check
    // this before handing data()/size() to a transport.
    bool ok() const { return ok_; }

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
    //
    // Contract: payloads must fit the u32 length prefix (< 4GB). A longer
    // payload is REFUSED atomically — neither the prefix nor any data is
    // appended, and ok() goes (and stays) false — so the stream can never
    // desync via a truncated prefix with full data appended. Message
    // payloads in this app are nowhere near 4GB; the guard exists so the
    // failure mode is a clean refusal rather than silent corruption.
    void writeString(const std::string& s) {
        writeBytes(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    }

    void writeBytes(const unsigned char* data, size_t len) {
        if (len > 0xFFFFFFFFull) {
            ok_ = false;
            return;
        }
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
    bool ok_ = true;
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

    // Sticky-fails the reader from the outside — used by frame-level
    // validation (e.g. a version-byte mismatch in readFrameHeader) so a
    // caller that only checks ok() can never go on to misparse the rest
    // of the buffer. Same effect as an out-of-bounds read: all further
    // reads return false, remaining() reports 0.
    void markBad() { ok_ = false; }

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
// byte doesn't match kWireVersion. A version mismatch sticky-fails the
// reader (ok() goes false, same as a truncation) so callers that only
// check ok() cannot go on to misparse the remaining bytes.
inline bool readFrameHeader(Reader& r, uint16_t& msgType) {
    uint8_t version = 0;
    if (!r.readU8(version)) return false;
    if (version != kWireVersion) {
        r.markBad();
        return false;
    }
    uint16_t type = 0;
    if (!r.readU16(type)) return false;
    msgType = type;
    return true;
}

}  // namespace wire
}  // namespace jefe
