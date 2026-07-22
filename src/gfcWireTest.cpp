// Self-test for jefe::wire (JEF-23). No framework — simple assert-count
// pattern, mirrors the style of the other --*-test harnesses in main_qt.cpp.
// Invoked via --wire-test, before any GUI/GL setup. Primitives are pure
// data; the message-codec tests (Task 2) additionally pull in the network
// structs and the FX/LUT serialize helpers — still no Qt/GL at runtime
// (the serialize fixtures are hand-built structs + temp files; the
// side-effecting unserialize paths are only exercised on truncated input,
// which returns false BEFORE any file write / manager load).

#include "gfcWire.h"
#include "gfcWireMessages.h"
#include "gfcNetworkStructures.h"
#include "gfcStructures.h"   // jefe::contentHash* (JEF-28 Task 1)

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
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

// ---- golden bytes (endianness pinned, not just symmetric) --------------

bool bytesEqual(const Writer& w, const std::vector<unsigned char>& expect) {
    return w.size() == expect.size() &&
           (expect.empty() ||
            std::memcmp(w.data(), expect.data(), expect.size()) == 0);
}

void testGoldenEncodings() {
    // Exact-byte assertions: a symmetric big-endian implementation would
    // pass every round-trip test, so pin little-endian on the wire here.
    {
        Writer w;
        w.writeU16(0x1234);
        check(bytesEqual(w, {0x34, 0x12}), "golden u16 LE bytes");
    }
    {
        Writer w;
        w.writeU32(0x11223344u);
        check(bytesEqual(w, {0x44, 0x33, 0x22, 0x11}), "golden u32 LE bytes");
    }
    {
        Writer w;
        w.writeU64(0x1122334455667788ULL);
        check(bytesEqual(w, {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}),
              "golden u64 LE bytes");
    }
    {
        Writer w;
        w.writeF32(1.0f);  // IEEE-754 bits 0x3F800000
        check(bytesEqual(w, {0x00, 0x00, 0x80, 0x3F}), "golden f32 1.0f LE bytes");
    }
}

void testGoldenFrameDecode() {
    // Positive decode of a hand-crafted frame: version=1, msgType=0x1234 LE.
    const unsigned char raw[] = {0x01, 0x34, 0x12};
    Reader r(raw, sizeof(raw));
    uint16_t msgType = 0;
    check(readFrameHeader(r, msgType) && msgType == 0x1234,
          "golden frame decode: version=1 msgType=0x1234");
    check(r.ok() && r.remaining() == 0, "golden frame decode consumed exactly");
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
    // Mismatch must sticky-fail the reader, same as truncation, so an
    // ok()-checking caller can't go on to misparse the remaining bytes.
    check(!r.ok(), "version mismatch sticky-fails reader (ok() false)");
    uint8_t after = 0;
    check(!r.readU8(after) && r.remaining() == 0,
          "no reads succeed after version mismatch");
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

// ---- message codecs (Task 2) -------------------------------------------

// Decoding any strict prefix of a well-formed encode must return false
// (clean truncation failure, no crash, no UB). One call per codec family.
void checkAllPrefixesFail(const Writer& w,
                          const std::function<bool(Reader&)>& decode,
                          const char* what) {
    bool allFail = true;
    for (size_t prefixLen = 0; prefixLen < w.size(); ++prefixLen) {
        Reader r(w.data(), prefixLen);
        if (decode(r)) allFail = false;
    }
    check(allFail, what);
}

void testMsgPlayPause() {
    gfcNetPlayPauseInfo in;
    in.play = true;
    in.frame = std::numeric_limits<int>::max();
    in.direction = -1;
    Writer w;
    encodePlayPause(w, in);
    Reader r(w.data(), w.size());
    gfcNetPlayPauseInfo out{};
    check(decodePlayPause(r, out) && out.play == in.play &&
              out.frame == in.frame && out.direction == in.direction,
          "playpause round-trip");
    check(r.remaining() == 0, "playpause consumes exactly");

    gfcNetPlayPauseInfo in2;
    in2.play = false;
    in2.frame = 0;
    in2.direction = std::numeric_limits<int>::min();
    Writer w2;
    encodePlayPause(w2, in2);
    Reader r2(w2.data(), w2.size());
    gfcNetPlayPauseInfo out2{};
    check(decodePlayPause(r2, out2) && out2.play == in2.play &&
              out2.frame == in2.frame && out2.direction == in2.direction,
          "playpause round-trip (zeros/min)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetPlayPauseInfo t{};
        return decodePlayPause(rr, t);
    }, "playpause truncation fails cleanly");
}

void testMsgPointerInfo() {
    gfcNetPointerInfo in;
    in.quadID = 3;
    in.x = -1920;
    in.y = -1080;
    in.scale = -0.25f;
    in.color = 0;  // not on the wire (legacy never sent it)
    Writer w;
    encodePointerInfo(w, in);
    Reader r(w.data(), w.size());
    gfcNetPointerInfo out{};
    check(decodePointerInfo(r, out) && out.quadID == in.quadID &&
              out.x == in.x && out.y == in.y && out.scale == in.scale,
          "pointerinfo round-trip");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetPointerInfo t{};
        return decodePointerInfo(rr, t);
    }, "pointerinfo truncation fails cleanly");
}

void testMsgRemotePointerInfo() {
    gfcNetRemotePointerInfo in;
    in.quadID = 2;
    in.x = 0;
    in.y = std::numeric_limits<int>::max();
    in.scale = 1.5f;
    in.name = "José 東京 🎬";
    in.color = -123456;
    Writer w;
    encodeRemotePointerInfo(w, in);
    Reader r(w.data(), w.size());
    gfcNetRemotePointerInfo out{};
    check(decodeRemotePointerInfo(r, out) && out.quadID == in.quadID &&
              out.x == in.x && out.y == in.y && out.scale == in.scale &&
              out.name == in.name && out.color == in.color,
          "remotepointerinfo round-trip (UTF-8 name)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetRemotePointerInfo t{};
        return decodeRemotePointerInfo(rr, t);
    }, "remotepointerinfo truncation fails cleanly");
}

void testMsgTransformations() {
    std::vector<gfcNetTransformationInfo> in(4);
    in[0].tX = -1.25f;  in[0].tY = 2.5f;   in[0].scale = 0.5f;  in[0].rZ = -90.0f;
    in[1].tX = 0.0f;    in[1].tY = 0.0f;   in[1].scale = 1.0f;  in[1].rZ = 0.0f;
    in[2].tX = 1e6f;    in[2].tY = -1e6f;  in[2].scale = -3.0f; in[2].rZ = 359.9f;
    in[3].tX = -0.001f; in[3].tY = 0.001f; in[3].scale = 100.f; in[3].rZ = 45.0f;
    Writer w;
    encodeTransformations(w, in);
    Reader r(w.data(), w.size());
    std::vector<gfcNetTransformationInfo> out;
    bool ok = decodeTransformations(r, out) && out.size() == in.size();
    if (ok) {
        for (size_t i = 0; i < in.size(); ++i)
            ok = ok && out[i].tX == in[i].tX && out[i].tY == in[i].tY &&
                 out[i].scale == in[i].scale && out[i].rZ == in[i].rZ;
    }
    check(ok, "transformations round-trip (4 elements)");

    // Empty vector.
    Writer w2;
    encodeTransformations(w2, {});
    Reader r2(w2.data(), w2.size());
    std::vector<gfcNetTransformationInfo> out2;
    check(decodeTransformations(r2, out2) && out2.empty(),
          "transformations round-trip (empty)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        std::vector<gfcNetTransformationInfo> t;
        return decodeTransformations(rr, t);
    }, "transformations truncation fails cleanly");
}

void testMsgColorCorrections() {
    std::vector<gfcNetPlateColorCorrectionInfo> in(3);
    in[0].quadID = 0; in[0].lutName = "Kodak_2383_日本.cube";
    in[0].gamma = -2.2f; in[0].exposure = -1.5f; in[0].brightness = 0.0f;
    in[0].contrast = 1.1f; in[0].saturation = 0.9f;
    in[1].quadID = -1; in[1].lutName = "";
    in[1].gamma = 1.0f; in[1].exposure = 0.0f; in[1].brightness = 1.0f;
    in[1].contrast = 1.0f; in[1].saturation = 1.0f;
    in[2].quadID = std::numeric_limits<int>::max(); in[2].lutName = "a";
    in[2].gamma = 1e-6f; in[2].exposure = 1e6f; in[2].brightness = -1e3f;
    in[2].contrast = 0.5f; in[2].saturation = 2.0f;
    Writer w;
    encodeColorCorrections(w, in);
    Reader r(w.data(), w.size());
    std::vector<gfcNetPlateColorCorrectionInfo> out;
    bool ok = decodeColorCorrections(r, out) && out.size() == in.size();
    if (ok) {
        for (size_t i = 0; i < in.size(); ++i)
            ok = ok && out[i].quadID == in[i].quadID &&
                 out[i].lutName == in[i].lutName &&
                 out[i].gamma == in[i].gamma &&
                 out[i].exposure == in[i].exposure &&
                 out[i].brightness == in[i].brightness &&
                 out[i].contrast == in[i].contrast &&
                 out[i].saturation == in[i].saturation;
    }
    check(ok, "colorcorrections round-trip (3 elements, UTF-8/empty lut)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        std::vector<gfcNetPlateColorCorrectionInfo> t;
        return decodeColorCorrections(rr, t);
    }, "colorcorrections truncation fails cleanly");
}

void testMsgOtherStates() {
    gfcNetOtherStatesInfo in;
    in.playbackInfo.from = -100;
    in.playbackInfo.to = std::numeric_limits<int>::max();
    in.playbackInfo.targetFPS = 23.976f;
    in.playbackInfo.playbackMode = 2;
    in.playbackInfo.loopPriority = -1;
    in.playbackInfo.inPoint = 0;
    in.playbackInfo.outPoint = 86399;
    in.layout = 3;
    for (int i = 0; i < 4; ++i) {
        gfcNetPlateStateInfo p;
        p.quadID = i;
        p.flip = (i % 2) == 0;
        p.flop = (i % 2) == 1;
        p.rz = 0.0f;  // not on the wire (legacy never sent it)
        p.aspect = (i == 0) ? "2.39:1 «scope»" : (i == 1 ? "" : "1.85");
        p.crop = i == 2;
        p.r = true; p.g = false; p.b = true; p.a = false;
        p.track = (unsigned char)(3 - i);
        in.plateStateInfo.push_back(p);
    }
    for (int i = 0; i < 3; ++i) {
        gfcNetTrackStateInfo t;
        t.frameOffset = -24 * i;
        t.holdMode = i;
        t.holdFrame = 1000000 + i;
        in.trackStateInfo.push_back(t);
    }
    Writer w;
    encodeOtherStates(w, in);
    Reader r(w.data(), w.size());
    gfcNetOtherStatesInfo out;
    bool ok = decodeOtherStates(r, out);
    ok = ok && out.playbackInfo.from == in.playbackInfo.from &&
         out.playbackInfo.to == in.playbackInfo.to &&
         out.playbackInfo.targetFPS == in.playbackInfo.targetFPS &&
         out.playbackInfo.playbackMode == in.playbackInfo.playbackMode &&
         out.playbackInfo.loopPriority == in.playbackInfo.loopPriority &&
         out.playbackInfo.inPoint == in.playbackInfo.inPoint &&
         out.playbackInfo.outPoint == in.playbackInfo.outPoint &&
         out.layout == in.layout &&
         out.plateStateInfo.size() == in.plateStateInfo.size() &&
         out.trackStateInfo.size() == in.trackStateInfo.size();
    if (ok) {
        for (size_t i = 0; i < in.plateStateInfo.size(); ++i) {
            const auto& a = in.plateStateInfo[i];
            const auto& b = out.plateStateInfo[i];
            ok = ok && a.track == b.track && a.quadID == b.quadID &&
                 a.flip == b.flip && a.flop == b.flop && a.a == b.a &&
                 a.r == b.r && a.g == b.g && a.b == b.b &&
                 a.aspect == b.aspect && a.crop == b.crop;
        }
        for (size_t i = 0; i < in.trackStateInfo.size(); ++i) {
            const auto& a = in.trackStateInfo[i];
            const auto& b = out.trackStateInfo[i];
            ok = ok && a.frameOffset == b.frameOffset &&
                 a.holdMode == b.holdMode && a.holdFrame == b.holdFrame;
        }
    }
    check(ok && r.remaining() == 0, "otherstates round-trip (4 plates, 3 tracks)");

    // Empty vectors variant.
    gfcNetOtherStatesInfo inEmpty = in;
    inEmpty.plateStateInfo.clear();
    inEmpty.trackStateInfo.clear();
    Writer w2;
    encodeOtherStates(w2, inEmpty);
    Reader r2(w2.data(), w2.size());
    gfcNetOtherStatesInfo out2;
    check(decodeOtherStates(r2, out2) && out2.plateStateInfo.empty() &&
              out2.trackStateInfo.empty(),
          "otherstates round-trip (empty vectors)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetOtherStatesInfo t;
        return decodeOtherStates(rr, t);
    }, "otherstates truncation fails cleanly");
}

void testMsgFXAdd() {
    gfcNetFXAddInfo in;
    in.id.quadID = 0;
    in.id.hash = "d41d8cd98f00b204e9800998ecf8427e";
    Writer w;
    encodeFXAdd(w, in);
    Reader r(w.data(), w.size());
    gfcNetFXAddInfo out;
    check(decodeFXAdd(r, out) && out.id.quadID == in.id.quadID &&
              out.id.hash == in.id.hash,
          "fxadd round-trip");

    // Empty hash.
    gfcNetFXAddInfo in2;
    in2.id.quadID = -3;
    in2.id.hash = "";
    Writer w2;
    encodeFXAdd(w2, in2);
    Reader r2(w2.data(), w2.size());
    gfcNetFXAddInfo out2;
    check(decodeFXAdd(r2, out2) && out2.id.quadID == -3 && out2.id.hash.empty(),
          "fxadd round-trip (empty hash)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetFXAddInfo t;
        return decodeFXAdd(rr, t);
    }, "fxadd truncation fails cleanly");
}

void testMsgFXCommon() {
    gfcNetFXCommonInfo in;
    in.id.index = 7;
    in.id.quadID = 1;
    in.onOff = 2;
    in.upDown = 1;
    in.reset = true;
    in.remove = false;
    Writer w;
    encodeFXCommon(w, in);
    Reader r(w.data(), w.size());
    gfcNetFXCommonInfo out{};
    check(decodeFXCommon(r, out) && out.id.index == in.id.index &&
              out.id.quadID == in.id.quadID && out.onOff == in.onOff &&
              out.upDown == in.upDown && out.reset == in.reset &&
              out.remove == in.remove,
          "fxcommon round-trip");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetFXCommonInfo t{};
        return decodeFXCommon(rr, t);
    }, "fxcommon truncation fails cleanly");
}

void testMsgFXAttrib() {
    gfcNetFXAttribInfo in;
    in.id.index = 0;
    in.id.quadID = 3;
    in.attribType = 255;
    in.theInt = -42;
    in.theFloat = -3.14159f;
    in.lutOrCube = "Füji_ETERNA.cube";
    in.groupName = "Grüppe";
    in.variableName = "変数";
    Writer w;
    encodeFXAttrib(w, in);
    Reader r(w.data(), w.size());
    gfcNetFXAttribInfo out;
    check(decodeFXAttrib(r, out) && out.id.index == in.id.index &&
              out.id.quadID == in.id.quadID && out.attribType == in.attribType &&
              out.theInt == in.theInt && out.theFloat == in.theFloat &&
              out.lutOrCube == in.lutOrCube && out.groupName == in.groupName &&
              out.variableName == in.variableName,
          "fxattrib round-trip (UTF-8 strings, legacy field order)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetFXAttribInfo t;
        return decodeFXAttrib(rr, t);
    }, "fxattrib truncation fails cleanly");
}

void testMsgFXStack() {
    gfcNetFXStackMessage in;
    in.quadID = -2;
    in.theStack =
        "<stack><fx name=\"Sharpen\" active=\"1\"/><fx name=\"Blur — ぼかし\" "
        "active=\"0\"/></stack>\nsecond line\r\nthird";
    Writer w;
    encodeFXStack(w, in);
    Reader r(w.data(), w.size());
    gfcNetFXStackMessage out;
    check(decodeFXStack(r, out) && out.quadID == in.quadID &&
              out.theStack == in.theStack,
          "fxstack round-trip (XML with newlines)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetFXStackMessage t;
        return decodeFXStack(rr, t);
    }, "fxstack truncation fails cleanly");
}

void testMsgLayerChange() {
    Writer w;
    encodeLayerChange(w, 2, "right.diffuse.R");
    Reader r(w.data(), w.size());
    int quadID = -1;
    std::string layer;
    check(decodeLayerChange(r, quadID, layer) && quadID == 2 &&
              layer == "right.diffuse.R",
          "layerchange round-trip");

    // Empty layer name.
    Writer w2;
    encodeLayerChange(w2, 0, "");
    Reader r2(w2.data(), w2.size());
    int q2 = -1;
    std::string layer2 = "sentinel";
    check(decodeLayerChange(r2, q2, layer2) && q2 == 0 && layer2.empty(),
          "layerchange round-trip (empty layer)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        int q;
        std::string l;
        return decodeLayerChange(rr, q, l);
    }, "layerchange truncation fails cleanly");
}

void testMsgChatEntry() {
    gfcChatLogEntry in;
    in.type = GFCNETMESSAGETYPE_LOAD;
    in.time = "14:32";
    in.sender = "Renée";
    in.message = "loaded /shots/sq010/plate_v002.####.exr — よろしく 🎥";
    in.color = -2023406815;  // large negative packed RGB
    Writer w;
    encodeChatEntry(w, in);
    Reader r(w.data(), w.size());
    gfcChatLogEntry out;
    check(decodeChatEntry(r, out) && out.type == in.type &&
              out.time == in.time && out.sender == in.sender &&
              out.message == in.message && out.color == in.color,
          "chatentry round-trip (type,time,sender,message,color)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcChatLogEntry t;
        return decodeChatEntry(rr, t);
    }, "chatentry truncation fails cleanly");
}

void testMsgPlaylist() {
    // Playlist message (full playlist XML).
    gfcNetPlaylistMessage in;
    in.thePlaylist =
        "<playlist>\n  <item name=\"shot_010 – テスト\"/>\n  "
        "<item name=\"shot_020\"/>\n</playlist>\n";
    Writer w;
    encodePlaylistMessage(w, in);
    Reader r(w.data(), w.size());
    gfcNetPlaylistMessage out;
    check(decodePlaylistMessage(r, out) && out.thePlaylist == in.thePlaylist,
          "playlistmessage round-trip");

    // Playlist item (single item XML string).
    const std::string itemXml =
        "<PlaylistItem><Track file=\"/média/clip.mov\" from=\"1\" "
        "to=\"240\"/></PlaylistItem>";
    Writer wi;
    encodePlaylistItem(wi, itemXml);
    Reader ri(wi.data(), wi.size());
    std::string itemOut;
    check(decodePlaylistItem(ri, itemOut) && itemOut == itemXml,
          "playlistitem round-trip");

    // Playlist event.
    gfcNetPlaylistEvent evIn;
    evIn.selectedItem = std::numeric_limits<int>::min();
    Writer we;
    encodePlaylistEvent(we, evIn);
    Reader re(we.data(), we.size());
    gfcNetPlaylistEvent evOut{};
    check(decodePlaylistEvent(re, evOut) &&
              evOut.selectedItem == evIn.selectedItem,
          "playlistevent round-trip (INT_MIN)");

    checkAllPrefixesFail(w, [](Reader& rr) {
        gfcNetPlaylistMessage t;
        return decodePlaylistMessage(rr, t);
    }, "playlist truncation fails cleanly");
}

void testFramedCodec() {
    // Full frame composition: header + codec payload, as Tasks 3/4 will
    // build them.
    gfcNetPlayPauseInfo in;
    in.play = true;
    in.frame = 1234;
    in.direction = 1;
    Writer w;
    beginFrame(w, (uint16_t)GFCNETID_PLAYPAUSEMESSAGE);
    encodePlayPause(w, in);
    check(w.ok(), "framed codec writer ok");

    Reader r(w.data(), w.size());
    uint16_t msgType = 0;
    gfcNetPlayPauseInfo out{};
    check(readFrameHeader(r, msgType) &&
              msgType == (uint16_t)GFCNETID_PLAYPAUSEMESSAGE &&
              decodePlayPause(r, out) && r.remaining() == 0 &&
              out.play == in.play && out.frame == in.frame &&
              out.direction == in.direction,
          "framed playpause: header + payload round-trip");
}

// ---- FX / LUT serialize helpers ----------------------------------------
//
// serializeLUT/serializeFX only need a filename-bearing fixture plus real
// files on disk (no GL, no managers), so they are exercised for real
// against temp files and their wire output is verified field-by-field
// (which doubles as the decode-sequence spec: unserializeLUT reads
// string+bytes, unserializeFX reads six strings, in exactly this order).
// The side-effecting full unserialize paths (file writes + fxManager/
// lutManager loads — manager/GL territory) are exercised ONLY on
// truncated input, which must return false before any side effect.

std::filesystem::path wireTestDir() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "jefecheck-wire-test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeFileBytes(const std::filesystem::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary);
    f.write(bytes.data(), (std::streamsize)bytes.size());
}

void testSerializeLUT() {
    const std::filesystem::path dir = wireTestDir();
    const std::filesystem::path lutPath = dir / "wiretest_lut.cube";
    // Binary-ish content including NUL and 0xFF bytes: the new wire format
    // carries the LUT body as raw length-prefixed bytes.
    std::string body("LUT_3D_SIZE 2\n");
    body.push_back('\x00');
    body.push_back('\x01');
    body.push_back('\xFF');
    body.push_back('\x7F');
    body += "binary tail";
    writeFileBytes(lutPath, body);

    CubeLUT lut;
    std::snprintf(lut.filename, sizeof(lut.filename), "%s",
                  lutPath.string().c_str());

    Writer w;
    serializeLUT(&lut, w);
    check(w.ok(), "serializeLUT writer ok");

    // Field-by-field: filename (no path), then raw file bytes.
    Reader r(w.data(), w.size());
    std::string nameOut;
    std::vector<unsigned char> bytesOut;
    bool ok = r.readString(nameOut) && r.readBytes(bytesOut);
    ok = ok && nameOut == "wiretest_lut.cube" &&
         bytesOut.size() == body.size() &&
         std::memcmp(bytesOut.data(), body.data(), body.size()) == 0;
    check(ok && r.remaining() == 0,
          "serializeLUT wire fields: filename + raw body bytes");

    // Truncation: unserializeLUT on every strict prefix returns false
    // (and performs no side effect — it must not reach the file write).
    bool allFail = true;
    for (size_t prefixLen = 0; prefixLen < w.size(); ++prefixLen) {
        Reader rr(w.data(), prefixLen);
        if (unserializeLUT(rr)) allFail = false;
    }
    check(allFail, "unserializeLUT truncation fails cleanly (no side effects)");

    std::error_code ec;
    std::filesystem::remove(lutPath, ec);
}

void testSerializeFX() {
    const std::filesystem::path dir = wireTestDir();
    const std::filesystem::path jfxPath = dir / "wiretest_fx.jfx";
    const std::filesystem::path vertPath = dir / "wiretest_fx.vert";
    const std::filesystem::path fragPath = dir / "wiretest_fx.frag";

    // Mirrors the real .jfx structure (src/FX/*.jfx). The XML declaration
    // matters: without it xmlParser's top-node semantics change,
    // getChildNode("root") comes back empty, and getAttribute returns NULL
    // (legacy serializeFX would crash identically on such a file).
    const std::string jfxBody =
        "<?xml version = '1.0' encoding = 'UTF-8'?>\n"
        "<root><shaders vertex=\"wiretest_fx.vert\" "
        "fragment=\"wiretest_fx.frag\"/></root>\n";
    const std::string vertBody =
        "void main() { gl_Position = ftransform(); } // vért\n";
    const std::string fragBody =
        "void main() { gl_FragColor = vec4(1.0); } // フラグ\n";
    writeFileBytes(jfxPath, jfxBody);
    writeFileBytes(vertPath, vertBody);
    writeFileBytes(fragPath, fragBody);

    gfcFX fx;  // plain ctor: no GL, no file IO
    fx.filename = jfxPath.string();

    Writer w;
    serializeFX(&fx, w);
    check(w.ok(), "serializeFX writer ok");

    // Field-by-field: jfx filename, jfx body, vert filename, vert body,
    // frag filename, frag body — the exact sequence unserializeFX reads.
    Reader r(w.data(), w.size());
    std::string jfxName, jfx, vertName, vert, fragName, frag;
    bool ok = r.readString(jfxName) && r.readString(jfx) &&
              r.readString(vertName) && r.readString(vert) &&
              r.readString(fragName) && r.readString(frag);
    ok = ok && jfxName == "wiretest_fx.jfx" && jfx == jfxBody &&
         vertName == "wiretest_fx.vert" && vert == vertBody &&
         fragName == "wiretest_fx.frag" && frag == fragBody;
    check(ok && r.remaining() == 0,
          "serializeFX wire fields: 6 strings in legacy order");

    // Truncation: unserializeFX on every strict prefix returns false
    // before any side effect (file writes / fxManager.loadFX).
    bool allFail = true;
    for (size_t prefixLen = 0; prefixLen < w.size(); ++prefixLen) {
        Reader rr(w.data(), prefixLen);
        if (unserializeFX(rr)) allFail = false;
    }
    check(allFail, "unserializeFX truncation fails cleanly (no side effects)");

    std::error_code ec;
    std::filesystem::remove(jfxPath, ec);
    std::filesystem::remove(vertPath, ec);
    std::filesystem::remove(fragPath, ec);
}

// ---- content digest (JEF-28 Task 1) ------------------------------------
//
// The P2P LUT/FX sync dedup depends on every peer computing the SAME digest
// for the same bytes, on any build/platform. These pin the algorithm (FNV-1a
// 64-bit over bytes), prove same-bytes→same / diff-bytes→diff, prove the FX
// digest now COVERS THE SHADER SOURCE (the old metadata-only hash didn't), and
// exercise the filename sanitization that closes the path-traversal gap.

void testContentDigestGolden() {
    // Pinned FNV-1a 64-bit vector (standard published test vector). If this
    // ever changes, the wire dedup silently breaks across peer versions.
    check(jefe::contentHashString("foobar") == "85944171f73967e8",
          "content digest golden: FNV-1a-64(\"foobar\") == 85944171f73967e8");
    // Empty input is the FNV offset basis.
    check(jefe::contentHashString("") == "cbf29ce484222325",
          "content digest golden: FNV-1a-64(\"\") == cbf29ce484222325");
    // Byte-range API must agree with the string API.
    const unsigned char foobar[] = {'f','o','o','b','a','r'};
    check(jefe::contentHash(foobar, sizeof(foobar)) == "85944171f73967e8",
          "content digest byte-range API matches string API");
}

void testContentDigestSameVsDifferent() {
    const std::string a = "LUT_3D_SIZE 2\nidentity data";
    const std::string b = a;                 // identical bytes
    std::string c = a; c.back() = '!';       // one byte different
    check(jefe::contentHashString(a) == jefe::contentHashString(b),
          "content digest: same bytes -> same hash");
    check(jefe::contentHashString(a) != jefe::contentHashString(c),
          "content digest: different bytes -> different hash");
}

void testContentDigestFXShaderSource() {
    // Prove the FX content hash (contentHashFiles over .jfx+.vert+.frag) now
    // covers the shader body: two FX with IDENTICAL metadata (.jfx) and vertex
    // shader but a DIFFERENT fragment shader must hash DIFFERENTLY. Under the
    // old metadata-only hash these collided.
    const std::filesystem::path dir = wireTestDir();
    const std::filesystem::path jfx  = dir / "cd_fx.jfx";
    const std::filesystem::path vert = dir / "cd_fx.vert";
    const std::filesystem::path fragA = dir / "cd_fxA.frag";
    const std::filesystem::path fragB = dir / "cd_fxB.frag";
    writeFileBytes(jfx,  "<root><info name=\"Same\" author=\"x\"/></root>");
    writeFileBytes(vert, "void main(){ gl_Position = ftransform(); }");
    writeFileBytes(fragA, "void main(){ gl_FragColor = vec4(1.0); }");
    writeFileBytes(fragB, "void main(){ gl_FragColor = vec4(0.0); }"); // 1.0 -> 0.0

    const std::string hashA =
        jefe::contentHashFiles({jfx.string(), vert.string(), fragA.string()});
    const std::string hashB =
        jefe::contentHashFiles({jfx.string(), vert.string(), fragB.string()});
    check(!hashA.empty() && !hashB.empty(),
          "FX content hash: files hashed (non-empty)");
    check(hashA != hashB,
          "FX content hash: same metadata, different fragment shader -> different hash");

    // Same three files hashed again must reproduce the same digest.
    const std::string hashA2 =
        jefe::contentHashFiles({jfx.string(), vert.string(), fragA.string()});
    check(hashA == hashA2, "FX content hash: deterministic for same files");

    // Unreadable-only set yields empty (caller-detectable failure).
    check(jefe::contentHashFiles({(dir / "does_not_exist.frag").string()}).empty(),
          "FX content hash: all-missing files -> empty digest");

    std::error_code ec;
    std::filesystem::remove(jfx, ec);   std::filesystem::remove(vert, ec);
    std::filesystem::remove(fragA, ec); std::filesystem::remove(fragB, ec);
}

void testFilenameSanitization() {
    // The sanitization unserializeFX/unserializeLUT apply before writing under
    // receivedPath is exactly std::filesystem::path(name).filename(). Verify it
    // defuses traversal + absolute paths and flags empty basenames.
    using std::filesystem::path;
    check(path("../../evil").filename().string() == "evil",
          "sanitize: '../../evil' -> 'evil'");
    check(path("/etc/passwd").filename().string() == "passwd",
          "sanitize: '/etc/passwd' -> 'passwd'");
    check(path("..\\..\\evil.cube").filename().string() == "..\\..\\evil.cube" ||
          path("..\\..\\evil.cube").filename().string() == "evil.cube",
          "sanitize: backslash form (platform-dependent, never escapes on POSIX write)");
    check(path("plain.cube").filename().string() == "plain.cube",
          "sanitize: bare name passes through unchanged");
    // Empty-basename cases the unserializers must skip (return false, no write).
    check(path("/tmp/somedir/").filename().string().empty(),
          "sanitize: trailing-separator path -> empty basename (skipped)");
    check(path("").filename().string().empty(),
          "sanitize: empty name -> empty basename (skipped)");
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
    testGoldenEncodings();
    testGoldenFrameDecode();
    testFrameRoundTrip();
    testFrameVersionMismatch();
    testTruncatedPrimitives();
    testReadPastEndReturnsFalse();
    testStringLengthExceedsRemaining();

    // Message codecs (Task 2).
    testMsgPlayPause();
    testMsgPointerInfo();
    testMsgRemotePointerInfo();
    testMsgTransformations();
    testMsgColorCorrections();
    testMsgOtherStates();
    testMsgFXAdd();
    testMsgFXCommon();
    testMsgFXAttrib();
    testMsgFXStack();
    testMsgLayerChange();
    testMsgChatEntry();
    testMsgPlaylist();
    testFramedCodec();
    testSerializeLUT();
    testSerializeFX();

    // Content digest (JEF-28 Task 1).
    testContentDigestGolden();
    testContentDigestSameVsDifferent();
    testContentDigestFXShaderSource();
    testFilenameSanitization();

    std::printf("WIRE-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    std::fflush(stdout);
    return (g_fail == 0) ? 0 : 2;
}

}  // namespace wire
}  // namespace jefe
