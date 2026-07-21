#ifndef GFCRAKNETTRANSPORT_H
#define GFCRAKNETTRANSPORT_H

#include "gfcTransport.h"

class RakPeerInterface; // fwd-declared; RakNet headers only in the .cpp

namespace jefe {
namespace net {

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
              bool broadcastExcluding) override;
    void closePeer(PeerId peer, bool sendNotification) override;
    int connectionCount() override;

private:
    RakPeerInterface* peer_;
    bool hosting_ = false;
};

} // namespace net
} // namespace jefe

#endif
