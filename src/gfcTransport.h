// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCTRANSPORT_H
#define GFCTRANSPORT_H

// Transport seam. Poll-based to match the 4 ms network pump. PeerId packs a
// RakNet SystemAddress (binaryAddress<<16 | port) so no RakNet type crosses
// this boundary; serialization (BitStream) stays app-side.

#include <cstdint>
#include <string>
#include <vector>

namespace jefe {
namespace net {

using PeerId = uint64_t;
constexpr PeerId kInvalidPeerId = 0;

inline PeerId packPeerId(uint32_t binaryAddress, uint16_t port) {
    return (static_cast<uint64_t>(binaryAddress) << 16) | port;
}

// QoS lane selector, orthogonal to the GFCNETID message type (dispatch is by
// GFCNETID). `Assets` carries bulk LUT/FX bodies on a separate reliable-ordered
// stream so a big transfer can't head-of-line-block live traffic.
enum class Channel { State = 0, Assets = 1 };

enum class TransportEventType {
    ConnectAccepted,   // client: server accepted us (peer = server)
    ConnectFailed,     // client: attempt failed
    ServerFull,        // client: no free incoming connections
    AlreadyConnected,  // client: duplicate connect (log only)
    Disconnected,      // client: graceful disconnect from server
    ConnectionLost,    // client: dropped
    PeerConnected,     // host: new incoming connection (peer = who)
    PeerDisconnected,  // host: peer left gracefully (peer = who)
    PeerLost,          // host: peer dropped (peer = who)
    Data               // app packet; bytes = full payload incl. leading id byte
};

struct TransportEvent {
    TransportEventType type;
    PeerId peer = kInvalidPeerId;
    std::vector<unsigned char> bytes; // only for Data
    // Which QoS lane a Data event arrived on. WebRTC sets it from the channel
    // label; RakNet leaves it State (its receive side can't recover the channel).
    Channel channel = Channel::State;
};

// Read-only per-peer connection health for the Remote-session indicator.
// `rttMs` is -1 when unknown; `path` classifies the selected ICE candidate
// pair (Direct = host/srflx/prflx, Relay = TURN). RakNet fills only presence.
struct PeerStats {
    PeerId peer = kInvalidPeerId;
    long rttMs = -1;                 // round-trip time in ms; -1 = unknown
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    enum class Path { Unknown, Direct, Relay } path = Path::Unknown;
    bool connected = false;
};

// One joiner waiting in a cloud session's lobby. Mirrors the coordinator's
// join-request, kept here so the Qt panel needs no signaling header (developer_notes §1).
// `displayName` is peer-supplied and reaches the UI as untrusted text; `email`
// is set only when `verified`.
struct PendingJoiner {
    std::string joinerId;
    std::string displayName;
    std::string email;
    bool verified = false;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    // Host role
    virtual bool startHost(unsigned short port, const std::string& password,
                           int maxClients) = 0;
    virtual void stopHost() = 0;

    // Client role
    virtual bool connect(const std::string& ip, unsigned short port,
                         const std::string& password) = 0;
    virtual void disconnect() = 0;

    // IO. poll() returns false when no event is available this call.
    virtual bool poll(TransportEvent& ev) = 0;
    // Mirrors RakNet Send semantics used app-wide today:
    // broadcastExcluding=false -> send to target only;
    // broadcastExcluding=true  -> send to everyone EXCEPT target
    //   (target==kInvalidPeerId -> everyone).
    // `channel` selects the QoS lane; Assets routes onto a second reliable-ordered stream.
    virtual void send(const unsigned char* data, int len, PeerId target,
                      bool broadcastExcluding,
                      Channel channel = Channel::State) = 0;

    virtual void closePeer(PeerId peer, bool sendNotification) = 0;
    virtual int connectionCount() = 0;

    // Cloud-coordinator session code assigned to the HOST after create-session
    // (empty for RakNet / LAN-WebRTC / joiners). Thread-safe.
    virtual std::string assignedSessionCode() { return std::string(); }

    // Per-peer connection health, read-only and best-effort: never holds a lock
    // across a blocking stats call. WebRTC returns real stats; RakNet basic presence.
    virtual std::vector<PeerStats> peerStats() { return {}; }

    // Joiners waiting for the host's decision; non-empty only on a
    // coordinator-mode HOST. Defaulted so LAN transports need no stub.
    virtual std::vector<PendingJoiner> pendingJoiners() { return {}; }
    // Decide one pending joiner. `admit` false denies. A joinerId that is no
    // longer pending (it gave up, or the other decision already landed) is a
    // no-op rather than an error -- two clicks on a stale row must not tear
    // anything down.
    virtual void decideJoiner(const std::string& /*joinerId*/, bool /*admit*/) {}

    // Joiner side: true between "the coordinator parked me in the lobby" and
    // the host's decision, so a waiting joiner is not mistaken for a failed connect.
    virtual bool awaitingAdmission() { return false; }

    // The last {"type":"error"} the coordinator sent; false when there has been
    // none. The code matters, not just the text: "sign in to host" and "out of
    // credits" have different fixes.
    virtual bool lastCoordinatorError(std::string& /*code*/,
                                      std::string& /*message*/) { return false; }
};

} // namespace net
} // namespace jefe

#endif
