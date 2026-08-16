// JEF-27 Task 1: cloud-coordinator signaling client (JEF-25 protocol).
//
// jefe::net::CoordinatorSignaling wraps a single rtc::WebSocket dialled to the
// JefeCheck cloud coordinator (ws://host:port or wss://host). It speaks the
// JEF-25 rendezvous protocol: create/join a session by short code, exchange
// presence, and relay the WebRTC SDP/ICE handshake. It is the cloud replacement
// for the JEF-24 LAN SignalingClient/SignalingServer pair (gfcSignaling.h) —
// the coordinator is the rendezvous; STUN/TURN iceServers (JEF-26) ride along.
//
// ── PROTOCOL CONTRACT (verbatim from the coordinator repo) ─────────────────
// Canonical source: ~/projects/jefecheck-coordinator, branch feature/coordinator
//   docs/coordinator-protocol.md + src/protocol.ts.
// Messages are JSON over the WebSocket. Client→coord messages are tagged by an
// `action` field; coord→client messages by a `type` field.
//
// Client → coordinator:
//   {"action":"create-session"}
//   {"action":"join-session","code":<string>}          // code e.g. "JEFE-7K2M"
//   {"action":"signal","to":<string>,"payload":<any>}  // to = target connId
//   {"action":"leave"}
// The `payload` of a signal is OPAQUE to the coordinator (relayed byte-for-byte)
// and carries the JEF-24 SignalMessage (gfcSignaling.h) as a NESTED JSON OBJECT:
//   "payload":{"type":"offer","sdp":"v=0\r\n...","peer":0}
//
// Coordinator → client:
//   {"type":"session-created","code":<string>,"token":<string>,
//                                              "iceServers?":[IceServer]}  (host)
//   {"type":"roster","peers":[<string>...],"iceServers?":[IceServer]}      (joiner;
//                                                    peers EXCLUDES the joiner)
//   {"type":"peer-joined","peerId":<string>}
//   {"type":"peer-left","peerId":<string>}
//   {"type":"signal","from":<string>,"payload":<any>}  // from = sender connId
//   {"type":"error","code":<string>,"message":<string>}
// IceServer := {"urls":<string|string[]>,"username?":<string>,"credential?":<string>}
//   iceServers is OPTIONAL (present only when the coordinator has TURN
//   configured). Every peer id (peerId, signal.to, signal.from, roster entries)
//   is a connection-id STRING (`connId`) — hence std::string everywhere.
//
// ── iceServers handling ────────────────────────────────────────────────────
// This client passes iceServers through as an OPAQUE raw-JSON substring (the
// exact `[...]` array text, or "" when absent). The WebRtcTransport (JEF-27
// Task 2) parses it into rtc::Configuration.iceServers at the PeerConnection
// build site — keeping the codec here small and dependency-free. Callbacks that
// receive iceServers get that raw substring verbatim.
//
// ── Threading ──────────────────────────────────────────────────────────────
// libdatachannel fires WebSocket callbacks on its own background threads. Shared
// state (the socket handle) is mutex-guarded; user callbacks may be invoked from
// a libdatachannel thread — keep them short and re-entrancy-safe. Register all
// callbacks BEFORE connect(). rtc objects require rtc::Preload()/Cleanup()
// bracketing at the process level or libdatachannel segfaults at exit.
//
// ── Reconnect / backoff (JEF-27 Task 5) ──────────────────────────────────────
// If the coordinator socket closes UNEXPECTEDLY (onClosed without a deliberate
// close()/leave()), a bounded exponential-backoff reconnect loop re-dials the
// same URL on a dedicated background thread (1s, 2s, 4s … capped at 30s, up to
// kMaxReconnectAttempts, then gives up with onError("reconnect-failed",…)). A
// deliberate close()/leave() sets an intentional-close flag that cancels/skips
// reconnect and wakes a sleeping loop. reconnectStatus() exposes the state
// ("idle"/"connecting"/"connected"/"reconnecting"/"failed"/"closed").
//
// DESIGN PRINCIPLE (design spec §5): the coordinator is a rendezvous needed for
// JOIN and RECONNECT only. Once peers hold a P2P DataChannel, a coordinator drop
// must NOT kill the established session — this class owns ONLY the coordinator
// WebSocket; it never touches the P2P PeerConnections (those live in
// WebRtcTransport and continue independently). Phase-1 limitation: on reconnect
// success the signaling channel is restored but session MEMBERSHIP is NOT
// resumed (the protocol has no resume/re-announce). So reconnect does NOT re-fire
// onOpen (which would create a NEW session with a fresh code / re-join) — it just
// restores the socket and logs. See developer_notes §34.
//
// This TU must NOT include Qt/GL headers (developer_notes §1).
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

// ── Envelope codec (JEF-25) ────────────────────────────────────────────────
// Pure functions — no sockets — so they are unit-testable in isolation.
// Encoders build the client→coord `action` envelopes; parseServerMsg decodes a
// coord→client `type` envelope. Parsing is defensive: malformed input yields a
// best-effort struct and never throws.

// Client→coord encoders.
// JEF-31: `authToken` is the coordinator access JWT. Both encoders OMIT every
// optional field when empty, so an anonymous client's bytes are identical to
// the pre-auth protocol -- which is what keeps a self-hosted coordinator and
// the --coord-test harness working untouched.
std::string encodeCreateSession(const std::string& authToken = "");
std::string encodeJoinSession(const std::string& code,
                              const std::string& displayName = "",
                              const std::string& authToken = "");
// Nests encodeSignal(msg) as the `payload` object.
std::string encodeSignalEnvelope(const std::string& toPeerId,
                                 const SignalMessage& msg);
std::string encodeLeave();

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

    // error:
    std::string errorCode;   // the coordinator's `code` field
    std::string message;
};

// Parse a coord→client envelope. Returns true if it looked like a JSON object
// with a string `type`; false on gross malformation. Never throws.
bool parseServerMsg(const std::string& json, CoordServerMessage& out);

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

    // Dial the coordinator. url is a full ws:// or wss:// URL. Async: onOpen
    // fires once the socket is up. Returns false only on immediate failure.
    bool connect(const std::string& url);
    void close();

    // Senders. Return false if the socket is not open yet.
    // JEF-31/37: optional access JWT and knock nickname. Empty values are
    // omitted from the wire entirely (see the encoder declarations above).
    bool createSession(const std::string& authToken = "");
    bool joinSession(const std::string& code,
                     const std::string& displayName = "",
                     const std::string& authToken = "");
    bool sendSignal(const std::string& toPeerId, const SignalMessage& msg);
    bool leave();

    // Current reconnect/backoff state (thread-safe). One of "idle",
    // "connecting", "connected", "reconnecting", "failed", "closed".
    std::string reconnectStatus();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Headless codec + loopback self-test (--coord-signal-test). Encodes each
// client action and parses each server type with representative values,
// including a signal whose payload SDP contains CRLF; optionally a loopback
// against a tiny rtc::WebSocketServer echoing the contract. Prints
// "COORD-SIGNAL-TEST: pass=<N> fail=<M>" and returns 0 if fail==0 else 2.
int coordinatorSignalingSelfTest();

}  // namespace net
}  // namespace jefe
