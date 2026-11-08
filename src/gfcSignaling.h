// SPDX-License-Identifier: GPL-2.0-or-later
// Local WebSocket signaling stub for the WebRTC transport (LAN, no auth). Must not
// include Qt/GL headers (developer_notes §1); callbacks fire on libdatachannel threads.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace rtc {
class WebSocket;
class WebSocketServer;
}  // namespace rtc

namespace jefe {
namespace net {

// Decoded signaling message (see schema above).
struct SignalMessage {
    std::string type;
    std::string sdp;
    std::string candidate;
    std::string mid;
    int peer = 0;
};

// Encode a SignalMessage to a compact JSON string. Only non-empty string
// fields (and always "type" + "peer") are emitted.
std::string encodeSignal(const SignalMessage& msg);

// Parse a flat JSON signaling object. Returns true if it looked like a JSON
// object; false on gross malformation. Recognised fields are filled in;
// unknown fields are ignored. Never throws.
bool parseSignal(const std::string& json, SignalMessage& out);

// ── SignalingServer ────────────────────────────────────────────────────
// Listens on a TCP port for WebSocket clients. Each connected client is
// assigned a small monotonic integer id (starting at 1). Callbacks report
// connect/disconnect and inbound JSON; sendTo() pushes JSON to one client.
class SignalingServer {
public:
    SignalingServer();
    ~SignalingServer();

    SignalingServer(const SignalingServer&) = delete;
    SignalingServer& operator=(const SignalingServer&) = delete;

    // Set callbacks before start(). fn(clientId).
    void onClientConnected(std::function<void(int)> fn);
    void onClientDisconnected(std::function<void(int)> fn);
    // fn(clientId, rawJson).
    void onMessage(std::function<void(int, const std::string&)> fn);

    // Bind and start listening. port 0 → OS picks an ephemeral port; read
    // the actual value back with boundPort(). Returns false on failure.
    bool start(uint16_t port);
    void stop();

    // Actual bound port (valid after a successful start()). 0 if not started.
    uint16_t boundPort() const;

    // Send a JSON string to one client. No-op if the id is unknown/closed.
    void sendTo(int clientId, const std::string& json);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// ── SignalingClient ────────────────────────────────────────────────────
// Dials a SignalingServer at ws://ip:port. Callbacks fire on libdatachannel
// threads; register them before connect().
class SignalingClient {
public:
    SignalingClient();
    ~SignalingClient();

    SignalingClient(const SignalingClient&) = delete;
    SignalingClient& operator=(const SignalingClient&) = delete;

    void onOpen(std::function<void()> fn);
    void onMessage(std::function<void(const std::string&)> fn);
    void onClosed(std::function<void()> fn);

    // Connect to ws://ip:port. Async: onOpen fires once the socket is up.
    bool connect(const std::string& ip, uint16_t port);
    void close();

    // Send a JSON string. Returns false if the socket is not open yet.
    bool send(const std::string& json);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Headless loopback self-test (--signal-test). Starts a server on an
// ephemeral port, connects a client, round-trips hello+offer both ways,
// prints "SIGNAL-TEST: pass=<N> fail=<M>" and returns 0 if fail==0 else 2.
int signalingSelfTest();

}  // namespace net
}  // namespace jefe
