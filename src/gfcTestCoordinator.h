#ifndef GFCTESTCOORDINATOR_H
#define GFCTESTCOORDINATOR_H

// JEF-27 Task 3: self-contained C++ TEST-DOUBLE coordinator.
//
// jefe::net::TestCoordinator is a minimal rtc::WebSocketServer that implements
// the JEF-25 coordinator rendezvous contract (field-matched to the canonical
// coordinator repo, ~/projects/jefecheck-coordinator src/protocol.ts). It exists
// ONLY to drive the two-process --coord-test end-to-end gate: a host process
// create-session's, one or more joiners join by code, and the coordinator relays
// the WebRTC SDP/ICE handshake between them (peers still form the real P2P star).
//
// ── PROTOCOL (verbatim contract) ────────────────────────────────────────────
// Client → coordinator (tagged by `action`):
//   {"action":"create-session"}
//   {"action":"join-session","code":<string>}
//   {"action":"signal","to":<connId>,"payload":<any>}   // payload relayed opaque
//   {"action":"leave"}
// Coordinator → client (tagged by `type`):
//   {"type":"session-created","code":<string>,"token":<string>,"iceServers":[..]}
//   {"type":"peer-joined","peerId":<connId>}
//   {"type":"peer-left","peerId":<connId>}
//   {"type":"roster","peers":[<connId>...],"iceServers":[..]}
//   {"type":"signal","from":<connId>,"payload":<any>}
//   {"type":"error","code":<string>,"message":<string>}
// peerIds are the coordinator's connection-id STRINGS ("c1","c2",...).
//
// ── ORDERING GUARANTEE ──────────────────────────────────────────────────────
// On {join-session}, the coordinator sends {peer-joined,peerId:Y} to the HOST
// BEFORE it sends the joiner its {roster,peers:[host]} — and always before it
// relays Y's first {signal} to the host. The client drops signals from an
// unknown coordId, so peer-joined MUST reach the host first. libdatachannel
// delivers per-socket messages in order, so a peer-joined queued before any
// relayed signal is guaranteed to be processed first on the host.
//
// This test coordinator returns a dummy public STUN iceServers entry on both
// session-created and roster to exercise the JEF-26 iceServers plumbing (the
// transport must apply it without breaking the localhost/LAN session — a public
// STUN entry is harmless on 127.0.0.1).
//
// rtc usage is bracketed by rtc::Preload()/Cleanup() internally (refcounted, so
// it nests harmlessly with the host transport's bracket in the same process).
// This TU must NOT include Qt/GL headers (developer_notes §1); rtc/* headers
// live only in the .cpp (pImpl-hidden here).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace jefe {
namespace net {

class TestCoordinator {
public:
    TestCoordinator();
    ~TestCoordinator();

    TestCoordinator(const TestCoordinator&) = delete;
    TestCoordinator& operator=(const TestCoordinator&) = delete;

    // Bind a WebSocketServer on an ephemeral port. Returns false on failure.
    bool start();
    // The bound port (0 until start() succeeds).
    uint16_t port() const;
    // Full dial URL for the coordinator: "ws://127.0.0.1:<port>/".
    std::string url() const;
    // Stop the server and drop all client sockets. Idempotent.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace net
} // namespace jefe

#endif
