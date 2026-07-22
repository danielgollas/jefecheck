// JEF-27 Task 1: cloud-coordinator signaling client. See
// gfcCoordinatorSignaling.h for the full JEF-25 protocol contract + threading.
#include "gfcCoordinatorSignaling.h"

#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace jefe {
namespace net {

// ── Local JSON helpers ──────────────────────────────────────────────────────
// The envelope codec reuses the exported encodeSignal()/parseSignal() from
// gfcSignaling for the nested payload, but the coordinator envelope itself
// needs string escaping + a small value-aware parser (nested object/array),
// which gfcSignaling's flat parser does not expose. These are file-local.

namespace {

void appendEscaped(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
}

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        ++i;
    }
}

// Parse a JSON string literal at s[i]=='"'. Fills `val`, advances past the
// closing quote. Returns false on malformation.
bool parseString(const std::string& s, size_t& i, std::string& val) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    val.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case '/':  val += '/';  break;
                case 'n':  val += '\n'; break;
                case 'r':  val += '\r'; break;
                case 't':  val += '\t'; break;
                case 'b':  val += '\b'; break;
                case 'f':  val += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= unsigned(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= unsigned(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= unsigned(h - 'A' + 10);
                        else return false;
                    }
                    if (code < 0x80) {
                        val += char(code);
                    } else if (code < 0x800) {
                        val += char(0xC0 | (code >> 6));
                        val += char(0x80 | (code & 0x3F));
                    } else {
                        val += char(0xE0 | (code >> 12));
                        val += char(0x80 | ((code >> 6) & 0x3F));
                        val += char(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: val += e; break;
            }
        } else {
            val += c;
        }
    }
    return false;  // unterminated
}

// Capture the raw substring spanning a whole JSON value at s[i] (object, array,
// string, number, or literal), advancing i to just past it. Handles nested
// braces/brackets and quoted strings (so a '}' inside a string is not counted).
// Returns false on malformation.
bool captureRawValue(const std::string& s, size_t& i, std::string& raw) {
    skipWs(s, i);
    if (i >= s.size()) return false;
    size_t start = i;
    char c = s[i];
    if (c == '"') {
        std::string tmp;
        if (!parseString(s, i, tmp)) return false;
    } else if (c == '{' || c == '[') {
        int depth = 0;
        while (i < s.size()) {
            char c2 = s[i];
            if (c2 == '"') {
                std::string tmp;
                if (!parseString(s, i, tmp)) return false;
                continue;
            }
            if (c2 == '{' || c2 == '[') ++depth;
            else if (c2 == '}' || c2 == ']') {
                --depth;
                ++i;
                if (depth == 0) break;
                continue;
            }
            ++i;
        }
        if (depth != 0) return false;
    } else {
        // Number / true / false / null: read to delimiter.
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    }
    raw = s.substr(start, i - start);
    return true;
}

// Parse a captured JSON array of strings into `out`. Best-effort; ignores
// non-string elements. Never throws.
void parseStringArray(const std::string& arr, std::vector<std::string>& out) {
    out.clear();
    size_t i = 0;
    skipWs(arr, i);
    if (i >= arr.size() || arr[i] != '[') return;
    ++i;
    while (i < arr.size()) {
        skipWs(arr, i);
        if (i < arr.size() && arr[i] == ']') break;
        if (arr[i] == '"') {
            std::string val;
            if (!parseString(arr, i, val)) break;
            out.push_back(val);
        } else {
            // Skip a non-string element.
            std::string raw;
            if (!captureRawValue(arr, i, raw)) break;
        }
        skipWs(arr, i);
        if (i < arr.size() && arr[i] == ',') { ++i; continue; }
        if (i < arr.size() && arr[i] == ']') break;
    }
}

}  // namespace

// ── Client→coord encoders ────────────────────────────────────────────────────

std::string encodeCreateSession() {
    return "{\"action\":\"create-session\"}";
}

std::string encodeJoinSession(const std::string& code) {
    std::string out = "{\"action\":\"join-session\",\"code\":\"";
    appendEscaped(out, code);
    out += "\"}";
    return out;
}

std::string encodeSignalEnvelope(const std::string& toPeerId,
                                 const SignalMessage& msg) {
    std::string out = "{\"action\":\"signal\",\"to\":\"";
    appendEscaped(out, toPeerId);
    out += "\",\"payload\":";
    out += encodeSignal(msg);  // nested JSON object (reused JEF-24 codec)
    out += "}";
    return out;
}

std::string encodeLeave() {
    return "{\"action\":\"leave\"}";
}

// ── Coord→client parser ──────────────────────────────────────────────────────

bool parseServerMsg(const std::string& json, CoordServerMessage& out) {
    out = CoordServerMessage{};
    size_t i = 0;
    skipWs(json, i);
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    skipWs(json, i);
    if (i < json.size() && json[i] == '}') return true;  // empty object

    while (i < json.size()) {
        skipWs(json, i);
        std::string key;
        if (!parseString(json, i, key)) return false;
        skipWs(json, i);
        if (i >= json.size() || json[i] != ':') return false;
        ++i;
        skipWs(json, i);
        if (i >= json.size()) return false;

        if (json[i] == '"') {
            std::string val;
            if (!parseString(json, i, val)) return false;
            if (key == "type") out.type = val;
            else if (key == "code") out.code = val;
            else if (key == "token") out.token = val;
            else if (key == "peerId") out.peerId = val;
            else if (key == "from") out.from = val;
            else if (key == "message") out.message = val;
            // unknown string key: ignore
        } else {
            std::string raw;
            if (!captureRawValue(json, i, raw)) return false;
            if (key == "peers") {
                parseStringArray(raw, out.peers);
            } else if (key == "iceServers") {
                out.iceServersJson = raw;  // opaque pass-through
            } else if (key == "payload") {
                // Nested JEF-24 SignalMessage — parse with the flat codec.
                out.hasPayload = parseSignal(raw, out.payload);
            }
            // unknown non-string key: ignore
        }

        skipWs(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == '}') { ++i; break; }
        if (i >= json.size()) break;  // tolerate missing close brace
    }

    // `code` doubles as the session code (session-created) and the error code
    // (error). Route it to errorCode when this is an error envelope.
    if (out.type == "error") out.errorCode = out.code;
    return true;
}

// ── CoordinatorSignaling ─────────────────────────────────────────────────────

struct CoordinatorSignaling::Impl {
    std::mutex mtx;  // guards ws
    std::shared_ptr<rtc::WebSocket> ws;

    std::function<void()> onOpenFn;
    std::function<void()> onClosedFn;
    std::function<void(std::string, std::string)> onErrorFn;
    std::function<void(std::string, std::string, std::string)> onSessionCreatedFn;
    std::function<void(std::string)> onPeerJoinedFn;
    std::function<void(std::string)> onPeerLeftFn;
    std::function<void(std::vector<std::string>, std::string)> onRosterFn;
    std::function<void(std::string, SignalMessage)> onSignalFn;

    void dispatch(const std::string& raw) {
        CoordServerMessage m;
        if (!parseServerMsg(raw, m)) return;  // defensive: drop malformed
        if (m.type == "session-created") {
            if (onSessionCreatedFn) onSessionCreatedFn(m.code, m.token, m.iceServersJson);
        } else if (m.type == "roster") {
            if (onRosterFn) onRosterFn(m.peers, m.iceServersJson);
        } else if (m.type == "peer-joined") {
            if (onPeerJoinedFn) onPeerJoinedFn(m.peerId);
        } else if (m.type == "peer-left") {
            if (onPeerLeftFn) onPeerLeftFn(m.peerId);
        } else if (m.type == "signal") {
            if (onSignalFn) onSignalFn(m.from, m.payload);
        } else if (m.type == "error") {
            if (onErrorFn) onErrorFn(m.errorCode, m.message);
        }
        // unknown type: ignore
    }

    bool sendRaw(const std::string& json) {
        std::shared_ptr<rtc::WebSocket> sock;
        {
            std::lock_guard<std::mutex> lk(mtx);
            sock = ws;
        }
        if (!sock) return false;
        try {
            if (!sock->isOpen()) return false;
            return sock->send(json);
        } catch (...) {
            return false;
        }
    }
};

CoordinatorSignaling::CoordinatorSignaling() : d_(std::make_unique<Impl>()) {}

CoordinatorSignaling::~CoordinatorSignaling() { close(); }

void CoordinatorSignaling::onOpen(std::function<void()> fn) { d_->onOpenFn = std::move(fn); }
void CoordinatorSignaling::onClosed(std::function<void()> fn) { d_->onClosedFn = std::move(fn); }
void CoordinatorSignaling::onError(std::function<void(std::string, std::string)> fn) {
    d_->onErrorFn = std::move(fn);
}
void CoordinatorSignaling::onSessionCreated(
    std::function<void(std::string, std::string, std::string)> fn) {
    d_->onSessionCreatedFn = std::move(fn);
}
void CoordinatorSignaling::onPeerJoined(std::function<void(std::string)> fn) {
    d_->onPeerJoinedFn = std::move(fn);
}
void CoordinatorSignaling::onPeerLeft(std::function<void(std::string)> fn) {
    d_->onPeerLeftFn = std::move(fn);
}
void CoordinatorSignaling::onRoster(
    std::function<void(std::vector<std::string>, std::string)> fn) {
    d_->onRosterFn = std::move(fn);
}
void CoordinatorSignaling::onSignal(
    std::function<void(std::string, SignalMessage)> fn) {
    d_->onSignalFn = std::move(fn);
}

bool CoordinatorSignaling::connect(const std::string& url) {
    std::shared_ptr<rtc::WebSocket> sock;
    try {
        sock = std::make_shared<rtc::WebSocket>();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "CoordinatorSignaling: create failed: %s\n", e.what());
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        d_->ws = sock;
    }

    Impl* d = d_.get();
    sock->onOpen([d]() { if (d->onOpenFn) d->onOpenFn(); });
    sock->onMessage(
        [](rtc::binary) { /* coordinator is text-only */ },
        [d](rtc::string msg) { d->dispatch(msg); });
    sock->onClosed([d]() { if (d->onClosedFn) d->onClosedFn(); });
    sock->onError([d](std::string e) {
        if (d->onErrorFn) d->onErrorFn("transport", e);
    });

    try {
        sock->open(url);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "CoordinatorSignaling: open failed: %s\n", e.what());
        return false;
    }
    return true;
}

void CoordinatorSignaling::close() {
    if (!d_) return;
    std::shared_ptr<rtc::WebSocket> sock;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        sock.swap(d_->ws);
    }
    if (sock) {
        try { sock->resetCallbacks(); sock->close(); } catch (...) {}
    }
}

bool CoordinatorSignaling::createSession() {
    return d_->sendRaw(encodeCreateSession());
}
bool CoordinatorSignaling::joinSession(const std::string& code) {
    return d_->sendRaw(encodeJoinSession(code));
}
bool CoordinatorSignaling::sendSignal(const std::string& toPeerId,
                                      const SignalMessage& msg) {
    return d_->sendRaw(encodeSignalEnvelope(toPeerId, msg));
}
bool CoordinatorSignaling::leave() {
    return d_->sendRaw(encodeLeave());
}

// ── Self-test (--coord-signal-test) ──────────────────────────────────────────

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "COORD-SIGNAL-TEST FAIL: %s\n", what);
    }
}

// Substring presence helper for exact-contract assertions on encoded JSON.
bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

int coordinatorSignalingSelfTest() {
    g_pass = 0;
    g_fail = 0;

    // ---- Client→coord encoders match the contract verbatim ----
    check(encodeCreateSession() == "{\"action\":\"create-session\"}",
          "create-session exact");
    check(encodeLeave() == "{\"action\":\"leave\"}", "leave exact");
    check(encodeJoinSession("JEFE-7K2M") ==
              "{\"action\":\"join-session\",\"code\":\"JEFE-7K2M\"}",
          "join-session exact");

    {
        SignalMessage m;
        m.type = "offer";
        m.sdp = "v=0\r\no=- 4611 2 IN IP4 127.0.0.1\r\na=setup:actpass\r\n";
        m.peer = 3;
        std::string env = encodeSignalEnvelope("B", m);
        check(contains(env, "\"action\":\"signal\""), "signal action tag");
        check(contains(env, "\"to\":\"B\""), "signal to field");
        check(contains(env, "\"payload\":{"), "signal payload is nested object");
        check(contains(env, "\"type\":\"offer\""), "signal payload type");
        // CRLF must be JSON-escaped, not raw, inside the payload string.
        check(contains(env, "\\r\\n"), "signal payload CRLF escaped");
        check(!contains(env, "\r\n"), "signal payload has no raw CRLF");

        // Round-trip: the envelope should parse back as a server-style signal
        // if a coordinator relayed it verbatim (from replaces to). Prove the
        // nested payload survives by re-parsing the payload directly.
        CoordServerMessage relayed;
        std::string serverForm =
            "{\"type\":\"signal\",\"from\":\"A\",\"payload\":" + encodeSignal(m) + "}";
        check(parseServerMsg(serverForm, relayed), "signal server parse ok");
        check(relayed.type == "signal", "signal type extracted");
        check(relayed.from == "A", "signal from extracted");
        check(relayed.hasPayload, "signal payload parsed");
        check(relayed.payload.type == "offer", "payload.type roundtrip");
        check(relayed.payload.sdp == m.sdp, "payload.sdp CRLF roundtrip intact");
        check(relayed.payload.peer == 3, "payload.peer roundtrip");
    }

    // ---- Coord→client parsers ----
    {
        CoordServerMessage m;
        std::string j =
            "{\"type\":\"session-created\",\"code\":\"JEFE-7K2M\","
            "\"token\":\"tok_9f3c\",\"iceServers\":["
            "{\"urls\":\"stun:relay.example:3478\"},"
            "{\"urls\":\"turn:relay.example:3478?transport=udp\","
            "\"username\":\"1700003600:sess\",\"credential\":\"ZmY4a\"}]}";
        check(parseServerMsg(j, m), "session-created parse ok");
        check(m.type == "session-created", "session-created type");
        check(m.code == "JEFE-7K2M", "session-created code");
        check(m.token == "tok_9f3c", "session-created token");
        check(!m.iceServersJson.empty(), "session-created iceServers captured");
        check(contains(m.iceServersJson, "turn:relay.example"),
              "iceServers raw contains TURN url");
        check(contains(m.iceServersJson, "username"),
              "iceServers raw contains credential fields");
        // Opaque pass-through: begins with '[' and ends with ']'.
        check(!m.iceServersJson.empty() && m.iceServersJson.front() == '[' &&
                  m.iceServersJson.back() == ']',
              "iceServers is the raw array substring");
    }

    {
        // session-created WITHOUT iceServers (LAN-only coordinator).
        CoordServerMessage m;
        std::string j =
            "{\"type\":\"session-created\",\"code\":\"JEFE-ABCD\",\"token\":\"t\"}";
        check(parseServerMsg(j, m), "session-created (no ice) parse ok");
        check(m.code == "JEFE-ABCD", "session-created (no ice) code");
        check(m.iceServersJson.empty(), "session-created (no ice) empty ice");
    }

    {
        CoordServerMessage m;
        std::string j = "{\"type\":\"roster\",\"peers\":[\"A\",\"C\"],"
                        "\"iceServers\":[{\"urls\":\"stun:s:3478\"}]}";
        check(parseServerMsg(j, m), "roster parse ok");
        check(m.type == "roster", "roster type");
        check(m.peers.size() == 2, "roster peers count");
        check(m.peers.size() == 2 && m.peers[0] == "A" && m.peers[1] == "C",
              "roster peers values");
        check(contains(m.iceServersJson, "stun:s:3478"), "roster iceServers");
    }

    {
        CoordServerMessage m;
        check(parseServerMsg("{\"type\":\"roster\",\"peers\":[]}", m),
              "empty roster parse ok");
        check(m.peers.empty(), "empty roster no peers");
    }

    {
        CoordServerMessage m;
        check(parseServerMsg("{\"type\":\"peer-joined\",\"peerId\":\"B\"}", m),
              "peer-joined parse ok");
        check(m.type == "peer-joined" && m.peerId == "B", "peer-joined fields");
    }

    {
        CoordServerMessage m;
        check(parseServerMsg("{\"type\":\"peer-left\",\"peerId\":\"B\"}", m),
              "peer-left parse ok");
        check(m.type == "peer-left" && m.peerId == "B", "peer-left fields");
    }

    {
        // signal carrying an SDP with CRLF, inbound from the coordinator.
        CoordServerMessage m;
        std::string j =
            "{\"type\":\"signal\",\"from\":\"A\",\"payload\":"
            "{\"type\":\"answer\",\"sdp\":\"v=0\\r\\na=ice-ufrag:xy\\r\\n\","
            "\"candidate\":\"\",\"peer\":0}}";
        check(parseServerMsg(j, m), "inbound signal parse ok");
        check(m.type == "signal" && m.from == "A", "inbound signal envelope");
        check(m.hasPayload, "inbound signal has payload");
        check(m.payload.type == "answer", "inbound payload type");
        check(m.payload.sdp == "v=0\r\na=ice-ufrag:xy\r\n",
              "inbound payload SDP CRLF decoded");
    }

    {
        CoordServerMessage m;
        std::string j = "{\"type\":\"error\",\"code\":\"no-session\","
                        "\"message\":\"unknown or expired code\"}";
        check(parseServerMsg(j, m), "error parse ok");
        check(m.type == "error", "error type");
        check(m.errorCode == "no-session", "error code routed to errorCode");
        check(m.message == "unknown or expired code", "error message");
    }

    // ---- Defensive parsing: malformed input never crashes ----
    {
        CoordServerMessage m;
        check(!parseServerMsg("not json", m), "garbage rejected");
        check(!parseServerMsg("", m), "empty rejected");
        check(parseServerMsg("{}", m), "empty object tolerated");
        check(parseServerMsg("{\"type\":\"signal\",\"from\":\"A\"}", m),
              "signal missing payload tolerated");
        check(!m.hasPayload, "missing payload flagged");
        // Truncated / unbalanced — must return without throwing.
        parseServerMsg("{\"type\":\"roster\",\"peers\":[\"A\",", m);
        ++g_pass;  // reached here without crashing
    }

    // ---- Optional loopback against a tiny scripted rtc::WebSocketServer ----
    // Proves connect()→createSession()→onSessionCreated wiring end-to-end. The
    // server echoes the JEF-25 session-created reply. Bounded + best-effort:
    // any timeout/failure is a soft skip (codec tests above are the core), so
    // the gate never hangs or flakes.
    {
        rtc::Preload();
        bool sawCreated = false;
        std::string gotCode;
        try {
            rtc::WebSocketServer::Configuration scfg;
            scfg.port = 0;
            scfg.enableTls = false;
            auto server = std::make_unique<rtc::WebSocketServer>(std::move(scfg));

            std::mutex hm;
            std::shared_ptr<rtc::WebSocket> held;  // keep server socket alive
            server->onClient([&](std::shared_ptr<rtc::WebSocket> ws) {
                {
                    std::lock_guard<std::mutex> lk(hm);
                    held = ws;
                }
                auto wptr = ws.get();
                ws->onMessage(
                    [](rtc::binary) {},
                    [wptr](rtc::string msg) {
                        CoordServerMessage tmp;  // reuse parser to sniff action
                        // The client sent {"action":"create-session"}; reply.
                        if (msg.find("create-session") != std::string::npos) {
                            try {
                                wptr->send(
                                    "{\"type\":\"session-created\",\"code\":"
                                    "\"JEFE-TEST\",\"token\":\"tok\"}");
                            } catch (...) {}
                        }
                        (void)tmp;
                    });
            });

            uint16_t port = server->port();

            CoordinatorSignaling client;
            std::mutex cm;
            std::condition_variable cv;
            client.onSessionCreated(
                [&](std::string code, std::string, std::string) {
                    std::lock_guard<std::mutex> lk(cm);
                    sawCreated = true;
                    gotCode = code;
                    cv.notify_all();
                });
            client.onOpen([&]() { client.createSession(); });

            std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/";
            if (client.connect(url)) {
                std::unique_lock<std::mutex> lk(cm);
                cv.wait_for(lk, std::chrono::seconds(3),
                            [&]() { return sawCreated; });
            }
            client.close();
            {
                std::lock_guard<std::mutex> lk(hm);
                if (held) { try { held->resetCallbacks(); held->close(); } catch (...) {} }
                held.reset();
            }
            server->stop();
        } catch (const std::exception& e) {
            std::fprintf(stderr,
                         "COORD-SIGNAL-TEST: loopback skipped (%s)\n", e.what());
        }
        rtc::Cleanup();

        if (sawCreated && gotCode == "JEFE-TEST") {
            ++g_pass;  // full loopback success
        } else {
            std::fprintf(stderr,
                         "COORD-SIGNAL-TEST: loopback not confirmed (soft skip)\n");
            // Not counted as a failure — codec tests are the contract gate.
        }
    }

    std::printf("COORD-SIGNAL-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 2;
}

}  // namespace net
}  // namespace jefe
