// JEF-24 Task 2: local WebSocket signaling stub. See gfcSignaling.h for the
// message schema and threading contract.
#include "gfcSignaling.h"

#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace jefe {
namespace net {

// ── Tiny flat-JSON encoder/parser ──────────────────────────────────────
// Handles exactly the SignalMessage fields: a flat object of string and int
// values. Not a general JSON library. Defensive: never throws.

static void appendEscaped(std::string& out, const std::string& s) {
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

std::string encodeSignal(const SignalMessage& msg) {
    std::string out = "{";
    out += "\"type\":\"";
    appendEscaped(out, msg.type);
    out += "\"";
    if (!msg.sdp.empty()) {
        out += ",\"sdp\":\"";
        appendEscaped(out, msg.sdp);
        out += "\"";
    }
    if (!msg.candidate.empty()) {
        out += ",\"candidate\":\"";
        appendEscaped(out, msg.candidate);
        out += "\"";
    }
    if (!msg.mid.empty()) {
        out += ",\"mid\":\"";
        appendEscaped(out, msg.mid);
        out += "\"";
    }
    out += ",\"peer\":";
    out += std::to_string(msg.peer);
    out += "}";
    return out;
}

namespace {

// Skip ASCII whitespace.
void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
        ++i;
    }
}

// Parse a JSON string literal starting at s[i]=='"'. Fills out `val`,
// advances i past the closing quote. Returns false on malformation.
bool parseString(const std::string& s, size_t& i, std::string& val) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;  // opening quote
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
                    // Decode \uXXXX to UTF-8 (BMP only; sufficient here).
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
                default: val += e; break;  // tolerate unknown escapes
            }
        } else {
            val += c;
        }
    }
    return false;  // unterminated
}

}  // namespace

bool parseSignal(const std::string& json, SignalMessage& out) {
    out = SignalMessage{};
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
            else if (key == "sdp") out.sdp = val;
            else if (key == "candidate") out.candidate = val;
            else if (key == "mid") out.mid = val;
            // unknown string key: ignore
        } else {
            // Number, true/false/null, or nested (we don't emit nesting).
            // Read a bare token up to , or }.
            size_t start = i;
            int depth = 0;
            while (i < json.size()) {
                char c = json[i];
                if (c == '{' || c == '[') { ++depth; }
                else if (c == '}' || c == ']') {
                    if (depth == 0) break;
                    --depth;
                } else if (c == ',' && depth == 0) {
                    break;
                } else if (c == '"') {
                    // skip a nested string safely
                    std::string tmp;
                    if (!parseString(json, i, tmp)) return false;
                    continue;
                }
                ++i;
            }
            std::string tok = json.substr(start, i - start);
            if (key == "peer") {
                // Defensive integer parse.
                out.peer = 0;
                try {
                    size_t pos = 0;
                    out.peer = std::stoi(tok, &pos);
                } catch (...) {
                    out.peer = 0;
                }
            }
        }

        skipWs(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == '}') return true;
        if (i >= json.size()) return true;  // tolerate missing close brace
    }
    return true;
}

// ── SignalingServer ─────────────────────────────────────────────────────

struct SignalingServer::Impl {
    std::unique_ptr<rtc::WebSocketServer> server;
    std::mutex mtx;  // guards clients + nextId
    std::map<int, std::shared_ptr<rtc::WebSocket>> clients;
    int nextId = 1;

    std::function<void(int)> onConnected;
    std::function<void(int)> onDisconnected;
    std::function<void(int, const std::string&)> onMsg;
};

SignalingServer::SignalingServer() : d_(std::make_unique<Impl>()) {}

SignalingServer::~SignalingServer() { stop(); }

void SignalingServer::onClientConnected(std::function<void(int)> fn) {
    d_->onConnected = std::move(fn);
}
void SignalingServer::onClientDisconnected(std::function<void(int)> fn) {
    d_->onDisconnected = std::move(fn);
}
void SignalingServer::onMessage(std::function<void(int, const std::string&)> fn) {
    d_->onMsg = std::move(fn);
}

bool SignalingServer::start(uint16_t port) {
    try {
        rtc::WebSocketServer::Configuration cfg;
        cfg.port = port;
        cfg.enableTls = false;
        d_->server = std::make_unique<rtc::WebSocketServer>(std::move(cfg));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SignalingServer: start failed: %s\n", e.what());
        return false;
    }

    Impl* d = d_.get();
    d->server->onClient([d](std::shared_ptr<rtc::WebSocket> ws) {
        int id;
        {
            std::lock_guard<std::mutex> lk(d->mtx);
            id = d->nextId++;
            d->clients[id] = ws;
        }

        ws->onOpen([d, id]() {
            if (d->onConnected) d->onConnected(id);
        });

        // Two-arg overload: we only care about text (JSON) frames.
        ws->onMessage(
            [](rtc::binary) { /* signaling is text-only */ },
            [d, id](rtc::string msg) {
                if (d->onMsg) d->onMsg(id, msg);
            });

        auto drop = [d, id]() {
            bool erased = false;
            {
                std::lock_guard<std::mutex> lk(d->mtx);
                erased = d->clients.erase(id) > 0;
            }
            if (erased && d->onDisconnected) d->onDisconnected(id);
        };
        ws->onClosed(drop);
        ws->onError([drop](std::string) { drop(); });
    });
    return true;
}

void SignalingServer::stop() {
    if (!d_) return;
    // Drop client sockets first, then the server, so no callback fires into
    // a half-destroyed Impl.
    std::map<int, std::shared_ptr<rtc::WebSocket>> toClose;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        toClose.swap(d_->clients);
    }
    for (auto& kv : toClose) {
        try { kv.second->resetCallbacks(); kv.second->close(); } catch (...) {}
    }
    toClose.clear();
    if (d_->server) {
        try { d_->server->stop(); } catch (...) {}
        d_->server.reset();
    }
}

uint16_t SignalingServer::boundPort() const {
    if (d_ && d_->server) {
        try { return d_->server->port(); } catch (...) {}
    }
    return 0;
}

void SignalingServer::sendTo(int clientId, const std::string& json) {
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        auto it = d_->clients.find(clientId);
        if (it != d_->clients.end()) ws = it->second;
    }
    if (ws) {
        try { ws->send(json); } catch (...) {}
    }
}

// ── SignalingClient ─────────────────────────────────────────────────────

struct SignalingClient::Impl {
    std::shared_ptr<rtc::WebSocket> ws;
    std::function<void()> onOpenFn;
    std::function<void(const std::string&)> onMsgFn;
    std::function<void()> onClosedFn;
};

SignalingClient::SignalingClient() : d_(std::make_unique<Impl>()) {}

SignalingClient::~SignalingClient() { close(); }

void SignalingClient::onOpen(std::function<void()> fn) { d_->onOpenFn = std::move(fn); }
void SignalingClient::onMessage(std::function<void(const std::string&)> fn) {
    d_->onMsgFn = std::move(fn);
}
void SignalingClient::onClosed(std::function<void()> fn) { d_->onClosedFn = std::move(fn); }

bool SignalingClient::connect(const std::string& ip, uint16_t port) {
    try {
        d_->ws = std::make_shared<rtc::WebSocket>();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SignalingClient: create failed: %s\n", e.what());
        return false;
    }

    Impl* d = d_.get();
    d->ws->onOpen([d]() { if (d->onOpenFn) d->onOpenFn(); });
    d->ws->onMessage(
        [](rtc::binary) {},
        [d](rtc::string msg) { if (d->onMsgFn) d->onMsgFn(msg); });
    d->ws->onClosed([d]() { if (d->onClosedFn) d->onClosedFn(); });

    try {
        d_->ws->open("ws://" + ip + ":" + std::to_string(port) + "/");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SignalingClient: open failed: %s\n", e.what());
        return false;
    }
    return true;
}

void SignalingClient::close() {
    if (d_ && d_->ws) {
        try { d_->ws->resetCallbacks(); d_->ws->close(); } catch (...) {}
        d_->ws.reset();
    }
}

bool SignalingClient::send(const std::string& json) {
    if (!d_ || !d_->ws) return false;
    if (!d_->ws->isOpen()) return false;
    try { return d_->ws->send(json); } catch (...) { return false; }
}

}  // namespace net
}  // namespace jefe
