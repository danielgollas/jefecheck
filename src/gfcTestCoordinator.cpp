// JEF-27 Task 3: test-double coordinator. See gfcTestCoordinator.h for the full
// JEF-25 contract + ordering guarantee. Built only when JEFECHECK_WEBRTC is on
// (it needs rtc::WebSocketServer); a no-WebRTC build compiles to an empty TU.
#include "gfcTestCoordinator.h"

#ifdef JEFECHECK_WEBRTC

#include <rtc/rtc.hpp>
#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace jefe {
namespace net {

namespace {

// ── Minimal flat-JSON helpers (mirrors gfcCoordinatorSignaling.cpp) ──────────
// The coordinator only needs to read a top-level `action`/`code`/`to` string and
// capture the raw `payload` value verbatim (it relays payloads opaquely). These
// are file-local and never throw.

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

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
                case 'n':  val += '\n'; break;
                case 'r':  val += '\r'; break;
                case 't':  val += '\t'; break;
                case 'b':  val += '\b'; break;
                case 'f':  val += '\f'; break;
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case '/':  val += '/';  break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    i += 4;  // coordinator does not need decoded \u; skip it
                    val += '?';
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

// Capture the raw substring of a whole JSON value at s[i] (object/array/string/
// number/literal), advancing i past it. Handles nesting + quoted strings.
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
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    }
    raw = s.substr(start, i - start);
    return true;
}

// Parse a client→coord envelope: fills `action`, plus `code`/`to` strings and
// the raw `payload` substring when present. Best-effort; never throws.
struct ClientMsg {
    std::string action;
    std::string code;        // join-session
    std::string to;          // signal target connId
    std::string payloadRaw;  // signal payload, captured verbatim
};

bool parseClientMsg(const std::string& json, ClientMsg& out) {
    out = ClientMsg{};
    size_t i = 0;
    skipWs(json, i);
    if (i >= json.size() || json[i] != '{') return false;
    ++i;
    skipWs(json, i);
    if (i < json.size() && json[i] == '}') return true;

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
            if (key == "action") out.action = val;
            else if (key == "code") out.code = val;
            else if (key == "to") out.to = val;
            // unknown string key: ignore
        } else {
            std::string raw;
            if (!captureRawValue(json, i, raw)) return false;
            if (key == "payload") out.payloadRaw = raw;
            // unknown non-string key: ignore
        }

        skipWs(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == '}') { ++i; break; }
        if (i >= json.size()) break;
    }
    return true;
}

void appendEscaped(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:   out += c;      break;
        }
    }
}

// A dummy public STUN entry — exercises the JEF-26 iceServers plumbing without
// requiring a real TURN relay (harmless on localhost).
const char* kIceServers =
    "[{\"urls\":\"stun:stun.l.google.com:19302\"}]";

bool traceEnabled() {
    static const bool on = std::getenv("JEFECHECK_REMOTE_TEST_DEBUG") != nullptr;
    return on;
}
void trace(const char* what, const std::string& a = "") {
    if (traceEnabled())
        std::fprintf(stderr, "[coord] %s %s\n", what, a.c_str());
}

}  // namespace

struct TestCoordinator::Impl {
    std::mutex mtx;
    std::unique_ptr<rtc::WebSocketServer> server;
    uint16_t boundPort = 0;
    std::atomic<int> nextId{1};

    // Session model: a single session with a fixed code. First create-session's
    // connection is the host; join-session's are joiners.
    std::map<std::string, std::shared_ptr<rtc::WebSocket>> clients;  // connId->ws
    std::string hostConnId;
    std::string sessionCode = "JEFE-TEST";
    bool hasSession = false;

    // ── Outbound send worker ─────────────────────────────────────────────────
    // libdatachannel's WebSocketServer gives each client socket its own receive
    // thread. Calling ws->send() on socket A from within socket B's receive
    // callback (the natural place a relay does it) does NOT reliably deliver —
    // observed: only the FIRST message a socket receives from its own callback
    // context is delivered; cross-socket relays are dropped. So ALL outbound
    // sends are marshalled onto ONE dedicated worker thread (neither a receive
    // callback thread nor the app thread), which reliably flushes every message.
    std::thread sendWorker;
    std::deque<std::pair<std::string, std::string>> sendQueue;  // (connId, msg)
    std::condition_variable sendCv;
    bool workerStop = false;

    void enqueueSend(const std::string& connId, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mtx);
        sendQueue.emplace_back(connId, msg);
        sendCv.notify_one();
    }

    void sendLoop() {
        for (;;) {
            std::pair<std::string, std::string> job;
            std::shared_ptr<rtc::WebSocket> ws;
            {
                std::unique_lock<std::mutex> lk(mtx);
                sendCv.wait(lk, [this] { return workerStop || !sendQueue.empty(); });
                if (workerStop && sendQueue.empty()) return;
                job = std::move(sendQueue.front());
                sendQueue.pop_front();
                auto it = clients.find(job.first);
                if (it != clients.end()) ws = it->second;
            }
            if (!ws) { trace("send NO-SOCK to=", job.first); continue; }
            try {
                if (ws->isOpen()) { ws->send(job.second); trace("send ok to=", job.first); }
                else trace("send CLOSED to=", job.first);
            } catch (...) { trace("send THREW to=", job.first); }
        }
    }

    // Queue `msg` for delivery to `connId` on the worker thread.
    void sendTo(const std::string& connId, const std::string& msg) {
        enqueueSend(connId, msg);
    }

    void handleMessage(const std::string& connId, const std::string& raw) {
        ClientMsg m;
        if (!parseClientMsg(raw, m)) return;

        if (m.action == "create-session") {
            {
                std::lock_guard<std::mutex> lk(mtx);
                hostConnId = connId;
                hasSession = true;
            }
            trace("create-session host=", connId);
            std::string out = "{\"type\":\"session-created\",\"code\":\"";
            appendEscaped(out, sessionCode);
            out += "\",\"token\":\"tok-test\",\"iceServers\":";
            out += kIceServers;
            out += "}";
            sendTo(connId, out);
            return;
        }

        if (m.action == "join-session") {
            std::string host;
            bool ok;
            {
                std::lock_guard<std::mutex> lk(mtx);
                ok = hasSession && m.code == sessionCode && !hostConnId.empty();
                host = hostConnId;
            }
            if (!ok) {
                sendTo(connId,
                       "{\"type\":\"error\",\"code\":\"no-session\","
                       "\"message\":\"unknown or expired code\"}");
                return;
            }
            trace("join-session joiner=", connId);
            // ORDERING: peer-joined to the HOST first, THEN roster to the joiner.
            std::string pj = "{\"type\":\"peer-joined\",\"peerId\":\"";
            appendEscaped(pj, connId);
            pj += "\"}";
            sendTo(host, pj);

            std::string roster = "{\"type\":\"roster\",\"peers\":[\"";
            appendEscaped(roster, host);
            roster += "\"],\"iceServers\":";
            roster += kIceServers;
            roster += "}";
            sendTo(connId, roster);
            return;
        }

        if (m.action == "signal") {
            // Relay {type:signal,from:connId,payload:<raw>} to `to` verbatim.
            std::string out = "{\"type\":\"signal\",\"from\":\"";
            appendEscaped(out, connId);
            out += "\",\"payload\":";
            out += m.payloadRaw.empty() ? "null" : m.payloadRaw;
            out += "}";
            sendTo(m.to, out);
            return;
        }

        if (m.action == "leave") {
            handleClose(connId);
            return;
        }
    }

    void handleClose(const std::string& connId) {
        std::vector<std::string> others;
        bool wasHost = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = clients.find(connId);
            if (it == clients.end()) return;  // already gone
            clients.erase(it);
            wasHost = (connId == hostConnId);
            for (auto& kv : clients) others.push_back(kv.first);
            if (wasHost) { hostConnId.clear(); hasSession = false; }
        }
        trace("close conn=", connId);
        std::string pl = "{\"type\":\"peer-left\",\"peerId\":\"";
        appendEscaped(pl, connId);
        pl += "\"}";
        for (auto& o : others) sendTo(o, pl);
    }
};

TestCoordinator::TestCoordinator() : d_(std::make_unique<Impl>()) {
    rtc::Preload();  // balanced in stop()/dtor (refcounted).
}

TestCoordinator::~TestCoordinator() {
    stop();
    rtc::Cleanup();
}

bool TestCoordinator::start() {
    try {
        rtc::WebSocketServer::Configuration cfg;
        cfg.port = 0;          // ephemeral
        cfg.enableTls = false;
        d_->server = std::make_unique<rtc::WebSocketServer>(std::move(cfg));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "TestCoordinator: server create failed: %s\n",
                     e.what());
        return false;
    }
    d_->boundPort = d_->server->port();

    // Start the outbound send worker (see Impl::sendLoop for why).
    d_->sendWorker = std::thread([d = d_.get()] { d->sendLoop(); });

    Impl* d = d_.get();
    d_->server->onClient([d](std::shared_ptr<rtc::WebSocket> ws) {
        const std::string connId = "c" + std::to_string(d->nextId.fetch_add(1));
        {
            std::lock_guard<std::mutex> lk(d->mtx);
            d->clients[connId] = ws;
        }
        trace("client connected connId=", connId);
        rtc::WebSocket* wptr = ws.get();
        ws->onMessage(
            [](rtc::binary) { /* text-only protocol */ },
            [d, connId](rtc::string msg) { d->handleMessage(connId, msg); });
        ws->onClosed([d, connId]() { d->handleClose(connId); });
        (void)wptr;
    });
    return true;
}

uint16_t TestCoordinator::port() const { return d_->boundPort; }

std::string TestCoordinator::url() const {
    return "ws://127.0.0.1:" + std::to_string(d_->boundPort) + "/";
}

void TestCoordinator::stop() {
    // Stop the send worker first so no send races the socket teardown.
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        d_->workerStop = true;
        d_->sendCv.notify_all();
    }
    if (d_->sendWorker.joinable()) d_->sendWorker.join();

    std::map<std::string, std::shared_ptr<rtc::WebSocket>> held;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        held.swap(d_->clients);
        d_->hostConnId.clear();
        d_->hasSession = false;
    }
    for (auto& kv : held) {
        if (kv.second) {
            try { kv.second->resetCallbacks(); kv.second->close(); } catch (...) {}
        }
    }
    held.clear();
    if (d_->server) {
        try { d_->server->stop(); } catch (...) {}
        d_->server.reset();
    }
}

} // namespace net
} // namespace jefe

#else  // !JEFECHECK_WEBRTC

// No-WebRTC build: provide out-of-line stubs so the header stays usable and the
// linker is satisfied (the harness paths that use it are also compiled out).
namespace jefe { namespace net {
struct TestCoordinator::Impl {};
TestCoordinator::TestCoordinator() : d_(nullptr) {}
TestCoordinator::~TestCoordinator() {}
bool TestCoordinator::start() { return false; }
uint16_t TestCoordinator::port() const { return 0; }
std::string TestCoordinator::url() const { return {}; }
void TestCoordinator::stop() {}
} }

#endif // JEFECHECK_WEBRTC
