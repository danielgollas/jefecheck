// SPDX-License-Identifier: GPL-2.0-or-later
// Cloud-coordinator signaling client (one rtc::WebSocket; wire protocol: coordinator repo docs/coordinator-protocol.md, developer_notes §34).
// Callbacks run on libdatachannel threads: register them BEFORE connect(). This TU must NOT include Qt/GL headers (§1).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gfcSignaling.h"  // jefe::net::SignalMessage + encode/parseSignal

namespace rtc {
class WebSocket;
}  // namespace rtc

namespace jefe {
namespace net {

// ── Envelope codec: pure functions, unit-testable; parsing never throws ─────

// Client->coord encoders. `authToken` is the coordinator access JWT; optional fields are
// omitted when empty/default, so an anonymous client's bytes match the original protocol.

// Host-side session policy sent with create-session; the coordinator enforces it for everyone.
struct SessionPolicy {
    bool requireKnock = true;
    std::string password;      // empty = none
    int idleTimeoutMinutes = 0;  // 0 = never
    int maxParticipants = 0;     // 0 = unlimited
};

std::string encodeCreateSession(const std::string& authToken = "");
std::string encodeCreateSession(const std::string& authToken,
                                const SessionPolicy& policy);
std::string encodeJoinSession(const std::string& code,
                              const std::string& displayName = "",
                              const std::string& authToken = "");
// Nests encodeSignal(msg) as the `payload` object.
std::string encodeSignalEnvelope(const std::string& toPeerId,
                                 const SignalMessage& msg);
std::string encodeLeave();

// Admission. Host-only: the coordinator rejects an admit/deny from anyone who
// is not the session's host.
std::string encodeAdmit(const std::string& joinerId);
std::string encodeDeny(const std::string& joinerId);

// A knock, as delivered to the host. `displayName` is peer-supplied: the
// coordinator sanitizes it (control characters stripped, length capped), but
// it still reaches the UI as untrusted text and must be rendered as plain
// text, never rich text. `email` is present ONLY when `verified` — an
// unverified joiner can call itself anything, including someone else's name,
// so the email is the only part of a knock the host can actually trust.
struct JoinRequest {
    std::string joinerId;
    std::string displayName;
    std::string email;
    bool verified = false;
};

// Decoded coord→client message. Only the fields relevant to `type` are filled.
struct CoordServerMessage {
    std::string type;  // session-created|roster|peer-joined|peer-left|signal|error

    // session-created:
    std::string code;
    std::string token;
    // session-created + roster (optional): raw JSON array substring, "" if absent.
    std::string iceServersJson;

    // roster:
    std::vector<std::string> peers;

    // peer-joined / peer-left:
    std::string peerId;

    // signal:
    std::string from;
    SignalMessage payload;   // parsed nested payload (best-effort)
    bool hasPayload = false;

    // join-request (host-only):
    std::string joinerId;
    std::string displayName;
    std::string email;
    bool verified = false;

    // error:
    std::string errorCode;   // the coordinator's `code` field
    std::string message;
};

// Parse a coord→client envelope. Returns true if it looked like a JSON object
// with a string `type`; false on gross malformation. Never throws.
bool parseServerMsg(const std::string& json, CoordServerMessage& out);

    // onJoinRequest fires on the HOST when someone knocks; onJoinPending fires on the
    // JOINER once parked in the lobby ("waiting for a human", not "coordinator is slow").
bool parseJoinRequest(const std::string& json, JoinRequest& out);

// ── CoordinatorSignaling ────────────────────────────────────────────────────
class CoordinatorSignaling {
public:
    CoordinatorSignaling();
    ~CoordinatorSignaling();

    CoordinatorSignaling(const CoordinatorSignaling&) = delete;
    CoordinatorSignaling& operator=(const CoordinatorSignaling&) = delete;

    // Register all callbacks BEFORE connect(). They fire on libdatachannel
    // background threads.
    void onOpen(std::function<void()> fn);
    void onClosed(std::function<void()> fn);
    // Both WebSocket transport errors and coordinator {"type":"error"} messages
    // route here. Transport errors use code "transport"; coordinator errors
    // carry the protocol `code` (e.g. "no-session", "session-ended").
    void onError(std::function<void(std::string code, std::string msg)> fn);

    void onSessionCreated(std::function<void(std::string code, std::string token,
                                             std::string iceServersJson)> fn);
    void onPeerJoined(std::function<void(std::string peerId)> fn);
    void onPeerLeft(std::function<void(std::string peerId)> fn);
    void onRoster(std::function<void(std::vector<std::string> peers,
                                     std::string iceServersJson)> fn);
    void onSignal(std::function<void(std::string fromPeerId, SignalMessage msg)> fn);
// Convenience view over parseServerMsg for a "join-request" envelope; false for any other type.
    void onJoinRequest(std::function<void(JoinRequest req)> fn);
    void onJoinPending(std::function<void()> fn);

    // Dial the coordinator. url is a full ws:// or wss:// URL. Async: onOpen
    // fires once the socket is up. Returns false only on immediate failure.
    bool connect(const std::string& url);
    void close();

    // Senders. Return false if the socket is not open yet. The access JWT and
    // knock nickname are optional; empty values are omitted from the wire.
    bool createSession(const std::string& authToken = "");
    bool createSession(const std::string& authToken, const SessionPolicy& policy);
    bool joinSession(const std::string& code,
                     const std::string& displayName = "",
                     const std::string& authToken = "");
    bool sendSignal(const std::string& toPeerId, const SignalMessage& msg);
    bool leave();
    // Host decision on a pending joiner.
    bool admit(const std::string& joinerId);
    bool deny(const std::string& joinerId);

    // Current reconnect/backoff state (thread-safe). One of "idle",
    // "connecting", "connected", "reconnecting", "failed", "closed".
    std::string reconnectStatus();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Headless codec + loopback self-test (--coord-signal-test). Prints
// "COORD-SIGNAL-TEST: pass=<N> fail=<M>" and returns 0 if fail==0 else 2.
int coordinatorSignalingSelfTest();

}  // namespace net
}  // namespace jefe
