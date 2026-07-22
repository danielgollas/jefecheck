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
    virtual void send(const unsigned char* data, int len, PeerId target,
                      bool broadcastExcluding) = 0;

    virtual void closePeer(PeerId peer, bool sendNotification) = 0;
    virtual int connectionCount() = 0;

    // JEF-27: cloud-coordinator session code assigned to the HOST after
    // create-session (empty for RakNet / LAN-WebRTC / joiners). Thread-safe.
    // Non-pure so only the coordinator-capable transport overrides it.
    virtual std::string assignedSessionCode() { return std::string(); }
};

} // namespace net
} // namespace jefe

#endif
