#ifndef GFCRAKNETTRANSPORT_H
#define GFCRAKNETTRANSPORT_H

#include "gfcTransport.h"

class RakPeerInterface; // fwd-declared; RakNet headers only in the .cpp

namespace jefe {
namespace net {

// RakNet-backed ITransport. ITransport carries jefe::wire FRAMES
// ([u8 version][u16 msgType LE][payload]): send() prepends the single RakNet
// envelope byte (GFCNET_USER_PACKET_BASE) and poll() strips it. Other
// transports (e.g. WebRtcTransport) must NOT add one.
class RakNetTransport : public ITransport {
public:
    RakNetTransport();
    ~RakNetTransport() override;

    bool startHost(unsigned short port, const std::string& password,
                   int maxClients) override;
    void stopHost() override;
    bool connect(const std::string& ip, unsigned short port,
                 const std::string& password) override;
    void disconnect() override;
    bool poll(TransportEvent& ev) override;
    void send(const unsigned char* data, int len, PeerId target,
              bool broadcastExcluding,
              Channel channel = Channel::State) override;
    void closePeer(PeerId peer, bool sendNotification) override;
    int connectionCount() override;

    // RakNet exposes no WebRTC-style stats: one basic entry per connected peer
    // (connected=true, path=Unknown, rtt=-1, bytes=0).
    std::vector<PeerStats> peerStats() override;

private:
    RakPeerInterface* peer_;
    bool hosting_ = false;
};

} // namespace net
} // namespace jefe

#endif
