#ifndef GFCTRANSPORT_H
#define GFCTRANSPORT_H

// JEF-22: transport seam. Poll-based to match the existing 4 ms network pump.
// PeerId packs a RakNet SystemAddress (binaryAddress<<16 | port) so no RakNet
// type crosses this boundary. Serialization (BitStream) stays app-side until
// the versioned wire format lands (JEF-23).

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

// JEF-28: QoS lane selector. Orthogonal to the GFCNETID message type — the
// receive dispatch is by GFCNETID regardless of channel. `Assets` carries the
// bulk LUT/FX serialize bodies on a SEPARATE reliable-ordered stream so a big
// transfer can't head-of-line-block live state/chat/pointer/play/CC traffic.
// Defaulted to State everywhere so every existing call site is unchanged.
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
    // JEF-28: which QoS lane a Data event arrived on. Not needed for dispatch
    // (GFCNETID drives that) but cheap + useful. WebRTC sets it accurately from
    // the channel label; RakNet leaves it State (its receive side can't cheaply
    // recover the ordering channel — see gfcRakNetTransport.cpp).
    Channel channel = Channel::State;
};

// JEF-30: read-only per-peer connection health, surfaced for the Remote-session
// health indicator. Best-effort observability: a transport that can't produce
// real WebRTC stats (RakNet) fills what it can (connected/presence) and leaves
// the rest at the defaults. `rttMs` is -1 when unknown; `path` classifies the
// selected ICE candidate pair (Direct = host/srflx/prflx, Relay = TURN).
struct PeerStats {
    PeerId peer = kInvalidPeerId;
    long rttMs = -1;                 // round-trip time in ms; -1 = unknown
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    enum class Path { Unknown, Direct, Relay } path = Path::Unknown;
    bool connected = false;
};

// JEF-37: one joiner waiting in a cloud session's lobby. Mirrors the
// coordinator's join-request, kept here so gfcnetworkmanager and the Qt panel
// can carry it without including the signaling header (developer_notes §1).
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
    // JEF-28: `channel` selects the QoS lane (default State = unchanged
    // behavior). Assets routes onto a second reliable-ordered stream.
    virtual void send(const unsigned char* data, int len, PeerId target,
                      bool broadcastExcluding,
                      Channel channel = Channel::State) = 0;

    virtual void closePeer(PeerId peer, bool sendNotification) = 0;
    virtual int connectionCount() = 0;

    // JEF-27: cloud-coordinator session code assigned to the HOST after
    // create-session (empty for RakNet / LAN-WebRTC / joiners). Thread-safe.
    // Non-pure so only the coordinator-capable transport overrides it.
    virtual std::string assignedSessionCode() { return std::string(); }

    // JEF-30: per-peer connection health. Defaulted empty so only the
    // WebRTC/RakNet transports override. READ-ONLY + best-effort: never holds a
    // lock across a blocking stats call, tolerates the underlying API returning
    // nullopt/false. WebRTC returns real stats; RakNet returns basic presence.
    virtual std::vector<PeerStats> peerStats() { return {}; }

    // JEF-37: joiners knocking at a cloud session, waiting for the host's
    // decision. Non-empty only on a coordinator-mode HOST; a joiner never
    // learns who else is in the lobby. Defaulted so LAN transports, which have
    // no lobby, need no stub.
    virtual std::vector<PendingJoiner> pendingJoiners() { return {}; }
    // Decide one pending joiner. `admit` false denies. A joinerId that is no
    // longer pending (it gave up, or the other decision already landed) is a
    // no-op rather than an error -- two clicks on a stale row must not tear
    // anything down.
    virtual void decideJoiner(const std::string& /*joinerId*/, bool /*admit*/) {}

    // JEF-37, joiner side: true between "the coordinator parked me in the
    // lobby" and the host's decision. Without it a knocking joiner is
    // indistinguishable from a failed connect -- nothing is connected, so the
    // panel falls back to the connect forms while the person is, in fact,
    // waiting on a human.
    virtual bool awaitingAdmission() { return false; }
};

} // namespace net
} // namespace jefe

#endif
