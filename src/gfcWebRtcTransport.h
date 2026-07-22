#ifndef GFCWEBRTCTRANSPORT_H
#define GFCWEBRTCTRANSPORT_H

// JEF-24 Task 3: WebRTC-backed ITransport over libdatachannel.
//
// Topology / SDP convention (host and client MUST agree — see Task 4):
//   The JOINING CLIENT is the OFFERER: it createDataChannel() + creates the
//   offer. The HOST is the ANSWERER: it never createDataChannel(); it receives
//   the channel via PeerConnection::onDataChannel and produces the answer by
//   feeding the client's offer into setRemoteDescription (auto-negotiation).
//
// This class carries jefe::wire FRAMES verbatim over a reliable/ordered data
// channel — NO envelope byte (unlike RakNetTransport, whose byte is a RakNet
// routing detail). Signaling (SDP offer/answer + ICE candidates) is exchanged
// over a local WebSocket rendezvous (jefe::net::SignalingServer / Client).
//
// This TU must not include Qt/GL headers (developer_notes §1). rtc/* headers
// live only in the .cpp; the impl is pImpl-hidden here.

#include <memory>
#include <string>

#include "gfcTransport.h"

namespace jefe {
namespace net {

class WebRtcTransport : public ITransport {
public:
    WebRtcTransport();
    ~WebRtcTransport() override;

    WebRtcTransport(const WebRtcTransport&) = delete;
    WebRtcTransport& operator=(const WebRtcTransport&) = delete;

    // Host role (fully implemented this task).
    bool startHost(unsigned short port, const std::string& password,
                   int maxClients) override;
    void stopHost() override;

    // Client role — Task 4 completes these; stubbed for now.
    bool connect(const std::string& ip, unsigned short port,
                 const std::string& password) override;
    void disconnect() override;

    bool poll(TransportEvent& ev) override;
    void send(const unsigned char* data, int len, PeerId target,
              bool broadcastExcluding) override;
    void closePeer(PeerId peer, bool sendNotification) override;
    int connectionCount() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace net
} // namespace jefe

#endif
