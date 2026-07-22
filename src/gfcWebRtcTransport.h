#ifndef GFCWEBRTCTRANSPORT_H
#define GFCWEBRTCTRANSPORT_H

// JEF-24: WebRTC-backed ITransport over libdatachannel (host + client roles).
//
// Topology / SDP convention (host and client MUST agree — see Task 4):
//   The JOINING CLIENT is the OFFERER: it createDataChannel() + creates the
//   offer. The HOST is the ANSWERER: it never createDataChannel(); it receives
//   the channel via PeerConnection::onDataChannel and produces the answer by
//   feeding the client's offer into setRemoteDescription (auto-negotiation).
//
// JEF-28: the offerer opens TWO reliable/ordered channels per peer — "jefe"
// (state/chat/pointer/play/CC = Channel::State) and "assets" (bulk LUT/FX
// bodies = Channel::Assets). The answerer receives both via onDataChannel and
// branches on dc->label(). PeerConnected/ConnectAccepted fire on the STATE
// channel opening only; the assets channel opens silently. send() routes by
// the Channel argument.
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

    // JEF-27: switch this instance into cloud-coordinator mode. MUST be called
    // before startHost()/connect(). In coordinator mode the transport dials a
    // CoordinatorSignaling to `url` (ws://|wss://) instead of a LAN
    // SignalingServer/Client: the host create-session's; a joiner joins by
    // `sessionCode`. `password` is carried for future coordinator auth.
    void configureCoordinator(const std::string& url,
                              const std::string& sessionCode,
                              const std::string& password);

    // Host role (fully implemented this task).
    bool startHost(unsigned short port, const std::string& password,
                   int maxClients) override;
    void stopHost() override;

    // Client role (offerer). connect() returns once the async signaling dial
    // has started (RakNet parity); completion arrives via poll() events.
    bool connect(const std::string& ip, unsigned short port,
                 const std::string& password) override;
    void disconnect() override;

    bool poll(TransportEvent& ev) override;
    void send(const unsigned char* data, int len, PeerId target,
              bool broadcastExcluding,
              Channel channel = Channel::State) override;
    void closePeer(PeerId peer, bool sendNotification) override;
    int connectionCount() override;

    // JEF-27: coordinator-assigned session code (host only; empty otherwise).
    std::string assignedSessionCode() override;

    // JEF-30: per-peer WebRTC connection health (rtt / bytes / direct-vs-relay).
    // Copies the pc shared_ptrs out under the peer-map lock, then queries
    // libdatachannel OUTSIDE the lock (the stats calls may block).
    std::vector<PeerStats> peerStats() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace net
} // namespace jefe

#endif
