// Self-test for jefe::wire (JEF-23). No framework — simple assert-count
// pattern, mirrors the style of the other --*-test harnesses in main_qt.cpp.
// Invoked via --wire-test, before any GUI/GL setup (pure data, no Qt/GL
// dependency at all — this TU only includes gfcWire.h and the standard
// library).

#include "gfcWire.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace jefe {
namespace wire {

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("WIRE-TEST: FAIL %s\n", what);
    }
}

// ---- primitive round-trips -------------------------------------------

void testU8() {
    for (uint8_t v : {uint8_t(0), uint8_t(1), uint8_t(0xFF), uint8_t(0x7F)}) {
        Writer w;
        w.writeU8(v);
        Reader r(w.data(), w.size());
        uint8_t out = 0;
        check(r.readU8(out) && out == v, "u8 round-trip");
    }
}

void testU16() {
    for (uint16_t v : {uint16_t(0), uint16_t(1), uint16_t(0xFF), uint16_t(0xFFFF)}) {
        Writer w;
        w.writeU16(v);
        Reader r(w.data(), w.size());
        uint16_t out = 0;
        check(r.readU16(out) && out == v, "u16 round-trip");
    }
}

void testU32() {
    for (uint32_t v : {uint32_t(0), uint32_t(1), uint32_t(0xFFFFFFFFu)}) {
        Writer w;
        w.writeU32(v);
        Reader r(w.data(), w.size());
        uint32_t out = 0;
        check(r.readU32(out) && out == v, "u32 round-trip");
    }
}

void testU64() {
    for (uint64_t v : {uint64_t(0), uint64_t(1), std::numeric_limits<uint64_t>::max()}) {
        Writer w;
        w.writeU64(v);
        Reader r(w.data(), w.size());
        uint64_t out = 0;
        check(r.readU64(out) && out == v, "u64 round-trip");
    }
}

void testI32() {
    for (int32_t v : {int32_t(0), std::numeric_limits<int32_t>::min(), int32_t(-1),
                       std::numeric_limits<int32_t>::max()}) {
        Writer w;
        w.writeI32(v);
        Reader r(w.data(), w.size());
        int32_t out = 0;
        check(r.readI32(out) && out == v, "i32 round-trip");
    }
}

void testF32() {
    const float values[] = {0.0f, -0.0f, 1.0f, -1.0f, 3.14159f, -123456.789f,
                             std::numeric_limits<float>::min(),
                             std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::lowest()};
    for (float v : values) {
        Writer w;
        w.writeF32(v);
        Reader r(w.data(), w.size());
        float out = 0.0f;
        const bool okRead = r.readF32(out);
        // Bit-exact comparison (handles -0.0f correctly via memcmp semantics,
        // ordinary == also treats -0.0f == 0.0f as true, but we want the
        // actual bit pattern preserved).
        bool bitsMatch = false;
        if (okRead) {
            uint32_t a = 0, b = 0;
            std::memcpy(&a, &v, sizeof(a));
            std::memcpy(&b, &out, sizeof(b));
            bitsMatch = (a == b);
        }
        check(okRead && bitsMatch, "f32 round-trip");
    }
}

void testF64() {
    const double values[] = {0.0, -0.0, 1.0, -1.0, 1e300, -1e300, 3.14159265358979,
                              std::numeric_limits<double>::min(),
                              std::numeric_limits<double>::max()};
    for (double v : values) {
        Writer w;
        w.writeF64(v);
        Reader r(w.data(), w.size());
        double out = 0.0;
        const bool okRead = r.readF64(out);
        bool bitsMatch = false;
        if (okRead) {
            uint64_t a = 0, b = 0;
            std::memcpy(&a, &v, sizeof(a));
            std::memcpy(&b, &out, sizeof(b));
            bitsMatch = (a == b);
        }
        check(okRead && bitsMatch, "f64 round-trip");
    }
}

void testBool() {
    for (bool v : {true, false}) {
        Writer w;
        w.writeBool(v);
        Reader r(w.data(), w.size());
        bool out = !v;
        check(r.readBool(out) && out == v, "bool round-trip");
    }
}

void testString() {
    const std::vector<std::string> values = {
        std::string(),
        std::string("née 東京"),
        std::string(1024 * 1024, 'x'),  // 1MB string
    };
    for (const auto& v : values) {
        Writer w;
        w.writeString(v);
        Reader r(w.data(), w.size());
        std::string out;
        check(r.readString(out) && out == v, "string round-trip");
    }
}

void testBytes() {
    const std::vector<unsigned char> values = {0x00, 0x01, 0xFF, 0x7F, 0x80};
    Writer w;
    w.writeBytes(values);
    Reader r(w.data(), w.size());
    std::vector<unsigned char> out;
    check(r.readBytes(out) && out == values, "bytes round-trip");

    // Empty bytes.
    Writer w2;
    w2.writeBytes(std::vector<unsigned char>{});
    Reader r2(w2.data(), w2.size());
    std::vector<unsigned char> out2;
    check(r2.readBytes(out2) && out2.empty(), "empty bytes round-trip");
}

// ---- frame helpers -----------------------------------------------------

void testFrameRoundTrip() {
    Writer w;
    beginFrame(w, 0x1234);
    w.writeU8(42);
    Reader r(w.data(), w.size());
    uint16_t msgType = 0;
    check(readFrameHeader(r, msgType) && msgType == 0x1234, "frame header round-trip");
    uint8_t payload = 0;
    check(r.readU8(payload) && payload == 42, "frame payload after header");
}

void testFrameVersionMismatch() {
    // Hand-craft a frame with version=2 (not kWireVersion=1).
    std::vector<unsigned char> raw = {2, 0x34, 0x12};
    Reader r(raw.data(), raw.size());
    uint16_t msgType = 0xFFFF;
    check(!readFrameHeader(r, msgType), "version mismatch rejected");
}

// ---- truncation / bounds-safety ----------------------------------------

void testTruncatedPrimitives() {
    // A frame carrying every primitive type. Every prefix strictly shorter
    // than the full buffer must fail cleanly (no UB, readX returns false).
    Writer w;
    w.writeU8(7);
    w.writeU16(1234);
    w.writeU32(999999);
    w.writeU64(123456789012345ULL);
    w.writeI32(-42);
    w.writeF32(3.5f);
    w.writeF64(2.71828);
    w.writeBool(true);
    w.writeString("hello");
    w.writeBytes(std::vector<unsigned char>{1, 2, 3});

    const size_t n = w.size();
    bool allPrefixesFailCleanly = true;
    for (size_t prefixLen = 0; prefixLen < n; ++prefixLen) {
        Reader r(w.data(), prefixLen);
        uint8_t u8v; uint16_t u16v; uint32_t u32v; uint64_t u64v;
        int32_t i32v; float f32v; double f64v; bool boolv;
        std::string strv; std::vector<unsigned char> bytesv;

        // Read the exact same sequence as written. At some point this will
        // run past the truncated buffer's end; from then on every read must
        // return false and ok() must go false, never touch OOB memory.
        bool sequenceOk = true;
        sequenceOk = sequenceOk && r.readU8(u8v);
        sequenceOk = sequenceOk && r.readU16(u16v);
        sequenceOk = sequenceOk && r.readU32(u32v);
        sequenceOk = sequenceOk && r.readU64(u64v);
        sequenceOk = sequenceOk && r.readI32(i32v);
        sequenceOk = sequenceOk && r.readF32(f32v);
        sequenceOk = sequenceOk && r.readF64(f64v);
        sequenceOk = sequenceOk && r.readBool(boolv);
        sequenceOk = sequenceOk && r.readString(strv);
        sequenceOk = sequenceOk && r.readBytes(bytesv);

        if (sequenceOk) {
            // A strictly-shorter prefix must not be able to satisfy the
            // full read sequence.
            allPrefixesFailCleanly = false;
        }
        if (!r.ok() && r.remaining() != 0) {
            allPrefixesFailCleanly = false;
        }
    }
    check(allPrefixesFailCleanly, "truncated buffer: every prefix < N fails cleanly");

    // Full-length buffer must succeed completely.
    {
        Reader r(w.data(), w.size());
        uint8_t u8v; uint16_t u16v; uint32_t u32v; uint64_t u64v;
        int32_t i32v; float f32v; double f64v; bool boolv;
        std::string strv; std::vector<unsigned char> bytesv;
        bool ok = r.readU8(u8v) && r.readU16(u16v) && r.readU32(u32v) &&
                  r.readU64(u64v) && r.readI32(i32v) && r.readF32(f32v) &&
                  r.readF64(f64v) && r.readBool(boolv) && r.readString(strv) &&
                  r.readBytes(bytesv);
        check(ok && r.remaining() == 0, "full-length buffer reads everything");
    }
}

void testReadPastEndReturnsFalse() {
    Writer w;
    w.writeU8(1);
    Reader r(w.data(), w.size());
    uint8_t v = 0;
    check(r.readU8(v) && v == 1, "read first byte ok");
    check(r.ok(), "ok() true before over-read");
    uint8_t v2 = 0xAA;
    check(!r.readU8(v2), "read past end returns false");
    check(!r.ok(), "ok() false after over-read");
    // Further reads keep failing without touching memory.
    check(!r.readU8(v2), "subsequent read after bad also false");
    check(r.remaining() == 0, "remaining() is 0 once bad");
}

void testStringLengthExceedsRemaining() {
    // Hand-craft a length prefix (u32) claiming more bytes than actually
    // follow in the buffer.
    Writer w;
    w.writeU32(1000);  // claims 1000 bytes of string data
    w.writeU8('x');     // but only 1 byte actually follows
    Reader r(w.data(), w.size());
    std::string out;
    check(!r.readString(out), "string length exceeding remaining() rejected");
    check(!r.ok(), "ok() false after malformed string length");
}

}  // namespace

int selfTest() {
    g_pass = 0;
    g_fail = 0;

    testU8();
    testU16();
    testU32();
    testU64();
    testI32();
    testF32();
    testF64();
    testBool();
    testString();
    testBytes();
    testFrameRoundTrip();
    testFrameVersionMismatch();
    testTruncatedPrimitives();
    testReadPastEndReturnsFalse();
    testStringLengthExceedsRemaining();

    std::printf("WIRE-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    std::fflush(stdout);
    return (g_fail == 0) ? 0 : 2;
}

}  // namespace wire
}  // namespace jefe
