#ifndef GFCWEBRTCTRANSPORT_H
#define GFCWEBRTCTRANSPORT_H

// WebRTC-backed ITransport over libdatachannel (host + client roles).
//
// Topology / SDP convention (host and client MUST agree):
//   The JOINING CLIENT is the OFFERER: it createDataChannel() + creates the
//   offer. The HOST is the ANSWERER: it never createDataChannel(); it receives
//   the channel via PeerConnection::onDataChannel and produces the answer by
//   feeding the client's offer into setRemoteDescription (auto-negotiation).
//
// The offerer opens TWO reliable/ordered channels per peer: "jefe"
// (Channel::State) and "assets" (bulk LUT/FX bodies, Channel::Assets). The
// answerer branches on dc->label() in onDataChannel. PeerConnected/ConnectAccepted
// fire on the STATE channel opening only. send() routes by the Channel argument.
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
// SessionPolicy: the host-side create-session policy, defined alongside the
// wire encoders so there is one definition rather than a parallel struct here
// that could drift from the JSON it produces.
#include "gfcCoordinatorSignaling.h"

namespace jefe {
namespace net {

class WebRtcTransport : public ITransport {
public:
    WebRtcTransport();
    ~WebRtcTransport() override;

    WebRtcTransport(const WebRtcTransport&) = delete;
    WebRtcTransport& operator=(const WebRtcTransport&) = delete;

    // Switch this instance into cloud-coordinator mode. MUST be called before
    // startHost()/connect(). It dials a CoordinatorSignaling at `url`
    // (ws://|wss://) instead of a LAN SignalingServer/Client; a joiner joins by
    // `sessionCode`. `password` is inert here (kept for signature parity with
    // the LAN transport); access is the join code plus, for hosting, `authToken`.
    // `displayName` is the nickname the host sees when a joiner knocks.
    void configureCoordinator(const std::string& url,
                              const std::string& sessionCode,
                              const std::string& password,
                              const std::string& authToken = "",
                              const std::string& displayName = "",
                              const SessionPolicy& policy = SessionPolicy{},
                              // See TransportConfig::selfJoinNonce. On a HOST
                              // this value auto-admits; empty means every joiner
                              // knocks, including the host's own client.
                              const std::string& selfJoinNonce = "");

    // Host role.
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

    // Coordinator-assigned session code (host only; empty otherwise).
    std::string assignedSessionCode() override;

    // Per-peer WebRTC health (rtt / bytes / direct-vs-relay). Copies the pc
    // shared_ptrs out under the peer-map lock and queries libdatachannel
    // OUTSIDE it (the stats calls may block).
    std::vector<PeerStats> peerStats() override;

    // Lobby. pendingJoiners() is a snapshot under the lock the coordinator
    // callback writes under; decideJoiner() sends admit/deny and drops the row
    // locally so a second click cannot re-send.
    std::vector<PendingJoiner> pendingJoiners() override;
    void decideJoiner(const std::string& joinerId, bool admit) override;
    bool awaitingAdmission() override;
    bool lastCoordinatorError(std::string& code, std::string& message) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace net
} // namespace jefe

#endif
