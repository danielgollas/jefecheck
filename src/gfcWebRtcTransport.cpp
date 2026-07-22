// JEF-24 Task 3: WebRtcTransport — HOST role over libdatachannel.
// See gfcWebRtcTransport.h for the offerer/answerer convention and the
// no-envelope-byte contract. The whole TU is guarded so a build without
// JEFECHECK_WEBRTC (factory then keeps the RakNet fallback) still links.
#include "gfcWebRtcTransport.h"

#ifdef JEFECHECK_WEBRTC

#include <rtc/rtc.hpp>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>

#include "gfcSignaling.h"

namespace jefe {
namespace net {

namespace {

// InitLogger must run exactly once per process (guard against double-init).
std::once_flag g_loggerOnce;
void initLoggerOnce() {
    std::call_once(g_loggerOnce, [] { rtc::InitLogger(rtc::LogLevel::Error); });
}

// Client role: the host is a single opaque peer. The app treats PeerIds as
// opaque tokens (JEF-22), so a fixed synthetic id is fine. We key the client's
// single Peer under this id in the same `peers` map the host uses, so send() /
// poll() / connectionCount() work uniformly for both roles.
constexpr PeerId kHostPeerId = 1;

} // namespace

struct WebRtcTransport::Impl {
    // One live peer = one PeerConnection + the reliable DataChannel the client
    // opened on it. `open` flips true on the channel's onOpen.
    struct Peer {
        int clientId = 0;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        bool open = false;
    };

    SignalingServer signaling;        // host role
    SignalingClient clientSignaling;  // client role

    // Single mutex guards the peer maps AND the event queue. All libdatachannel
    // / signaling callbacks fire on background threads and mutate under it; the
    // app thread (poll/send/closePeer/connectionCount) reads under it. Network
    // sends (dc->send) are done OUTSIDE the lock (shared_ptrs copied out first).
    std::mutex mtx;
    std::map<PeerId, Peer> peers;        // by assigned PeerId
    std::map<int, PeerId> clientToPeer;  // signaling clientId -> PeerId
    std::deque<TransportEvent> events;

    // PeerId assignment: atomic monotonic counter from 1 (0 == kInvalidPeerId).
    std::atomic<PeerId> nextPeerId{1};

    int maxClients = 0;
    bool hosting = false;
    bool client = false;  // set once connect() starts the client signaling

    // Client-session guard (mtx-protected). connect() sets it true; disconnect()/
    // teardownClient() set it false. The async client signaling callbacks
    // (onClientSignalingOpen etc.) fire on a WebSocket thread and may race a
    // connect()-then-immediate-disconnect(): they check this under the lock and
    // bail (constructing/pushing nothing) if the session was already torn down,
    // so a live pc/dc can't be orphaned into a session the caller abandoned.
    bool clientActive = false;

    void pushEvent(TransportEventType type, PeerId peer,
                   std::vector<unsigned char> bytes = {}) {
        std::lock_guard<std::mutex> lk(mtx);
        TransportEvent ev;
        ev.type = type;
        ev.peer = peer;
        ev.bytes = std::move(bytes);
        events.push_back(std::move(ev));
    }

    // Attach handlers to the reliable channel the client opened (host side).
    void setupChannel(PeerId pid, std::shared_ptr<rtc::DataChannel> dc) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(pid);
            if (it == peers.end()) return;  // peer already gone
            it->second.dc = dc;
        }

        dc->onOpen([this, pid]() {
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(pid);
                if (it != peers.end()) it->second.open = true;
            }
            pushEvent(TransportEventType::PeerConnected, pid);
        });

        // Binary frames only (jefe::wire frames). Text is unused on the wire.
        dc->onMessage(
            [this, pid](rtc::binary msg) {
                std::vector<unsigned char> bytes(msg.size());
                for (size_t i = 0; i < msg.size(); ++i)
                    bytes[i] = static_cast<unsigned char>(msg[i]);
                pushEvent(TransportEventType::Data, pid, std::move(bytes));
            },
            [](rtc::string) { /* text unused */ });

        dc->onClosed([this, pid]() {
            bool wasOpen = false;
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(pid);
                if (it != peers.end()) {
                    wasOpen = it->second.open;
                    it->second.open = false;
                }
            }
            if (wasOpen) pushEvent(TransportEventType::PeerLost, pid);
        });
    }

    // Create the answerer PeerConnection for a freshly connected signaling
    // client and wire its signaling forwarders. Called from onClientConnected.
    void onSignalingClientConnected(int clientId) {
        // Early bail if already visibly at capacity (cheap; the authoritative
        // check is re-done under the lock at insert time to close the TOCTOU).
        if (maxClients > 0) {
            std::lock_guard<std::mutex> lk(mtx);
            if (static_cast<int>(peers.size()) >= maxClients) {
                return;  // At capacity: ignore. (Client gets no answer.)
            }
        }

        PeerId pid = nextPeerId.fetch_add(1);

        rtc::Configuration config;  // empty iceServers — LAN host candidates.
        std::shared_ptr<rtc::PeerConnection> pc;
        try {
            pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: PeerConnection failed: %s\n",
                         e.what());
            return;
        }

        pc->onLocalDescription([this, clientId, pid](rtc::Description desc) {
            SignalMessage m;
            m.type = desc.typeString();  // "answer" for the host
            m.sdp = std::string(desc);
            m.peer = static_cast<int>(pid);
            signaling.sendTo(clientId, encodeSignal(m));
        });

        pc->onLocalCandidate([this, clientId, pid](rtc::Candidate cand) {
            SignalMessage m;
            m.type = "candidate";
            m.candidate = std::string(cand);
            m.mid = cand.mid();
            m.peer = static_cast<int>(pid);
            signaling.sendTo(clientId, encodeSignal(m));
        });

        // Host is the answerer: it RECEIVES the channel, never creates one.
        pc->onDataChannel([this, pid](std::shared_ptr<rtc::DataChannel> dc) {
            setupChannel(pid, dc);
        });

        bool inserted = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            // Authoritative capacity re-check under the lock (closes the
            // check-then-insert TOCTOU: two clients racing here can't both pass
            // a released-lock check and overshoot maxClients).
            if (maxClients == 0 ||
                static_cast<int>(peers.size()) < maxClients) {
                Peer p;
                p.clientId = clientId;
                p.pc = pc;
                peers[pid] = std::move(p);
                clientToPeer[clientId] = pid;
                inserted = true;
            }
        }
        if (!inserted) {
            // Lost the capacity race: drop this PeerConnection (reset callbacks
            // first so its forwarders can't fire into a peer we never stored).
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
        }
    }

    // Route an inbound signaling message (client's offer / ICE candidate) into
    // the matching PeerConnection.
    void onSignalingMessage(int clientId, const std::string& json) {
        SignalMessage m;
        if (!parseSignal(json, m)) return;

        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto cit = clientToPeer.find(clientId);
            if (cit == clientToPeer.end()) return;
            auto pit = peers.find(cit->second);
            if (pit == peers.end()) return;
            pc = pit->second.pc;
        }
        if (!pc) return;

        try {
            if (m.type == "offer") {
                // Feeding the remote offer triggers auto-negotiation: the host
                // generates its answer, delivered via onLocalDescription.
                pc->setRemoteDescription(rtc::Description(m.sdp, m.type));
            } else if (m.type == "answer") {
                pc->setRemoteDescription(rtc::Description(m.sdp, m.type));
            } else if (m.type == "candidate") {
                pc->addRemoteCandidate(rtc::Candidate(m.candidate, m.mid));
            }
            // "hello" and unknown types: ignored on the host.
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: signaling apply failed: %s\n",
                         e.what());
        }
    }

    void onSignalingClientDisconnected(int clientId) {
        PeerId pid = kInvalidPeerId;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = clientToPeer.find(clientId);
            if (it != clientToPeer.end()) pid = it->second;
        }
        // The signaling socket dropping does not itself tear down the media
        // channel; PeerLost is emitted from dc->onClosed. Nothing to do here
        // beyond leaving the peer intact (WebRTC survives signaling loss on a
        // LAN once connected). Suppress unused warning.
        (void)pid;
    }

    // ── Client role ─────────────────────────────────────────────────────
    // Wire the reliable channel WE opened to the host (offerer side). Mirrors
    // setupChannel but emits client-flavoured events (ConnectAccepted /
    // ConnectionLost) against the fixed synthetic kHostPeerId.
    void setupClientChannel(std::shared_ptr<rtc::DataChannel> dc) {
        dc->onOpen([this]() {
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(kHostPeerId);
                if (it != peers.end()) it->second.open = true;
            }
            pushEvent(TransportEventType::ConnectAccepted, kHostPeerId);
        });

        dc->onMessage(
            [this](rtc::binary msg) {
                std::vector<unsigned char> bytes(msg.size());
                for (size_t i = 0; i < msg.size(); ++i)
                    bytes[i] = static_cast<unsigned char>(msg[i]);
                pushEvent(TransportEventType::Data, kHostPeerId,
                          std::move(bytes));
            },
            [](rtc::string) { /* text unused */ });

        dc->onClosed([this]() {
            bool wasOpen = false;
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(kHostPeerId);
                if (it != peers.end()) {
                    wasOpen = it->second.open;
                    it->second.open = false;
                }
            }
            // A drop after the channel was open is a lost connection; a close
            // before it ever opened is a failed/graceful teardown. (disconnect()
            // resets callbacks first, so this fires only for spontaneous drops.)
            pushEvent(wasOpen ? TransportEventType::ConnectionLost
                              : TransportEventType::Disconnected,
                      kHostPeerId);
        });
    }

    // Signaling socket to the host is up: build the offerer PeerConnection and
    // open the reliable/ordered "jefe" channel (which triggers the offer).
    void onClientSignalingOpen() {
        // Bail if the caller already tore the session down (connect() then an
        // immediate disconnect() racing this WS-thread callback).
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!clientActive) return;
        }

        rtc::Configuration config;  // empty iceServers — LAN host candidates.
        std::shared_ptr<rtc::PeerConnection> pc;
        try {
            pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: client PeerConnection failed: %s\n",
                         e.what());
            pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
            return;
        }

        pc->onLocalDescription([this](rtc::Description desc) {
            SignalMessage m;
            m.type = desc.typeString();  // "offer" for the client (offerer)
            m.sdp = std::string(desc);
            m.peer = static_cast<int>(kHostPeerId);
            clientSignaling.send(encodeSignal(m));
        });

        pc->onLocalCandidate([this](rtc::Candidate cand) {
            SignalMessage m;
            m.type = "candidate";
            m.candidate = std::string(cand);
            m.mid = cand.mid();
            m.peer = static_cast<int>(kHostPeerId);
            clientSignaling.send(encodeSignal(m));
        });

        // Reliable + ordered (RakNet RELIABLE_ORDERED parity). The default
        // Reliability is already reliable (no maxPacketLifeTime/maxRetransmits)
        // and ordered (unordered=false); set explicitly for intent.
        rtc::DataChannelInit init;
        init.reliability.unordered = false;
        init.reliability.maxPacketLifeTime.reset();
        init.reliability.maxRetransmits.reset();

        std::shared_ptr<rtc::DataChannel> dc;
        try {
            dc = pc->createDataChannel("jefe", init);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: createDataChannel failed: %s\n",
                         e.what());
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
            pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
            return;
        }

        setupClientChannel(dc);

        bool inserted = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            // Re-check under the lock: disconnect() may have run while we built
            // the pc/dc above. Only publish the peer into a still-active session.
            if (clientActive) {
                Peer p;
                p.clientId = 0;  // no signaling-client id on the client side
                p.pc = pc;
                p.dc = dc;
                peers[kHostPeerId] = std::move(p);
                inserted = true;
            }
        }
        if (!inserted) {
            // Session was torn down mid-construction: drop the orphan quietly
            // (reset callbacks first so it can't push a stale event).
            try { dc->resetCallbacks(); dc->close(); } catch (...) {}
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
        }
    }

    // Inbound signaling from the host: apply the answer / ICE candidates.
    void onClientSignalingMessage(const std::string& json) {
        SignalMessage m;
        if (!parseSignal(json, m)) return;

        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(kHostPeerId);
            if (it != peers.end()) pc = it->second.pc;
        }
        if (!pc) return;

        try {
            if (m.type == "answer") {
                pc->setRemoteDescription(rtc::Description(m.sdp, m.type));
            } else if (m.type == "candidate") {
                pc->addRemoteCandidate(rtc::Candidate(m.candidate, m.mid));
            }
            // "offer"/"hello"/unknown: ignored on the client (offerer).
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: client signaling apply failed: %s\n",
                         e.what());
        }
    }

    void onClientSignalingClosed() {
        // Like the host: once the media channel is up it survives signaling
        // loss on a LAN; ConnectionLost comes from dc->onClosed. Nothing to do.
    }

    // Tear down the client's single PeerConnection + signaling socket. Safe to
    // call when not connected (nothing stored).
    void teardownClient() {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            // Flip the session guard first so any in-flight WS-thread callback
            // (onClientSignalingOpen still constructing a pc/dc) bails / drops
            // its orphan instead of publishing it after we return. Idempotent:
            // a second disconnect() finds no peer and no active session.
            clientActive = false;
            auto it = peers.find(kHostPeerId);
            if (it != peers.end()) {
                pc = it->second.pc;
                dc = it->second.dc;
                peers.erase(it);
            }
        }
        // Reset callbacks before close so onClosed can't push a spurious event.
        if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
        if (pc) { try { pc->resetCallbacks(); pc->close(); } catch (...) {} }
        clientSignaling.close();
        client = false;
    }

    // Close + erase a peer. Returns true if it existed.
    bool closePeerInternal(PeerId peer) {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(peer);
            if (it == peers.end()) return false;
            pc = it->second.pc;
            dc = it->second.dc;
            clientToPeer.erase(it->second.clientId);
            peers.erase(it);
        }
        // Reset callbacks + close outside the lock so no callback re-enters.
        // resetCallbacks() on BOTH the channel and the PeerConnection (teardown
        // symmetry) so neither's forwarders fire into a half-dead peer.
        if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
        if (pc) { try { pc->resetCallbacks(); pc->close(); } catch (...) {} }
        return true;
    }

    void teardownAll() {
        std::map<PeerId, Peer> toClose;
        {
            std::lock_guard<std::mutex> lk(mtx);
            clientActive = false;  // stop any in-flight client callback
            toClose.swap(peers);
            clientToPeer.clear();
        }
        for (auto& kv : toClose) {
            if (kv.second.dc) {
                try { kv.second.dc->resetCallbacks(); kv.second.dc->close(); }
                catch (...) {}
            }
            if (kv.second.pc) {
                // resetCallbacks() on the PeerConnection too (teardown symmetry).
                try { kv.second.pc->resetCallbacks(); kv.second.pc->close(); }
                catch (...) {}
            }
        }
        toClose.clear();
        // Tear down both roles' signaling — a transport instance is one or the
        // other, and stop()/close() are no-ops when the other was never used.
        signaling.stop();
        clientSignaling.close();
        hosting = false;
        client = false;
    }
};

WebRtcTransport::WebRtcTransport() : d_(std::make_unique<Impl>()) {
    initLoggerOnce();
    rtc::Preload();  // balanced by rtc::Cleanup() in the dtor.
}

WebRtcTransport::~WebRtcTransport() {
    d_->teardownAll();
    d_.reset();
    rtc::Cleanup();  // MUST come after all rtc objects are destroyed.
}

bool WebRtcTransport::startHost(unsigned short port, const std::string& /*password*/,
                                int maxClients) {
    if (d_->hosting) return false;
    d_->maxClients = maxClients;

    // Register callbacks BEFORE start() (documented SignalingServer contract).
    Impl* d = d_.get();
    d_->signaling.onClientConnected(
        [d](int id) { d->onSignalingClientConnected(id); });
    d_->signaling.onClientDisconnected(
        [d](int id) { d->onSignalingClientDisconnected(id); });
    d_->signaling.onMessage(
        [d](int id, const std::string& json) { d->onSignalingMessage(id, json); });

    if (!d_->signaling.start(port)) {
        std::fprintf(stderr, "WebRtcTransport: signaling start failed on port %u\n",
                     static_cast<unsigned>(port));
        return false;
    }
    d_->hosting = true;
    return true;
}

void WebRtcTransport::stopHost() {
    d_->teardownAll();
}

// ── Client role ─────────────────────────────────────────────────────────
// connect() mirrors RakNet's semantics: it returns as soon as the async attempt
// has STARTED (the signaling socket began dialing). The actual WebRTC session
// completes later and is reported via events (ConnectAccepted on channel open,
// ConnectFailed/ConnectionLost on failure). InitLogger/Preload were already run
// in the ctor (idempotent, refcounted, balanced by Cleanup in the dtor).
bool WebRtcTransport::connect(const std::string& ip, unsigned short port,
                              const std::string& /*password*/) {
    if (d_->hosting || d_->client) return false;  // one role per instance
    initLoggerOnce();

    // Mark the session active BEFORE the async dial so the WS-thread callbacks
    // (which may fire immediately) see a live session. A racing disconnect()
    // flips this false and the callbacks bail.
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        d_->clientActive = true;
    }

    // Register signaling callbacks BEFORE connect() (SignalingClient contract).
    Impl* d = d_.get();
    d_->clientSignaling.onOpen([d]() { d->onClientSignalingOpen(); });
    d_->clientSignaling.onMessage(
        [d](const std::string& json) { d->onClientSignalingMessage(json); });
    d_->clientSignaling.onClosed([d]() { d->onClientSignalingClosed(); });

    if (!d_->clientSignaling.connect(ip, port)) {
        std::fprintf(stderr, "WebRtcTransport: client signaling connect failed\n");
        std::lock_guard<std::mutex> lk(d_->mtx);
        d_->clientActive = false;
        return false;
    }
    d_->client = true;
    return true;  // async — completion arrives via poll() events
}

void WebRtcTransport::disconnect() {
    d_->teardownClient();  // safe when not connected
}

bool WebRtcTransport::poll(TransportEvent& ev) {
    std::lock_guard<std::mutex> lk(d_->mtx);
    if (d_->events.empty()) return false;
    ev = std::move(d_->events.front());
    d_->events.pop_front();
    return true;
}

void WebRtcTransport::send(const unsigned char* data, int len, PeerId target,
                           bool broadcastExcluding) {
    if (len <= 0 || data == nullptr) return;

    // Collect target channels under the lock, then send after unlocking.
    // RakNet parity (gfcTransport.h):
    //   broadcastExcluding=false → `target` ONLY; target==kInvalidPeerId → NOBODY.
    //   broadcastExcluding=true  → everyone EXCEPT `target`
    //                              (target==kInvalidPeerId → everyone).
    std::vector<std::shared_ptr<rtc::DataChannel>> targets;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        if (!broadcastExcluding) {
            // Unicast. kInvalidPeerId addresses no one — send to nobody
            // (matches RakNet, and never a broadcast).
            if (target != kInvalidPeerId) {
                auto it = d_->peers.find(target);
                if (it != d_->peers.end() && it->second.open && it->second.dc)
                    targets.push_back(it->second.dc);
            }
        } else {
            // Broadcast to everyone except `target`
            // (target==kInvalidPeerId → nothing to exclude → everyone).
            for (auto& kv : d_->peers) {
                if (kv.first == target) continue;
                if (kv.second.open && kv.second.dc)
                    targets.push_back(kv.second.dc);
            }
        }
    }

    const std::byte* bytes = reinterpret_cast<const std::byte*>(data);
    const size_t n = static_cast<size_t>(len);
    for (auto& dc : targets) {
        try { dc->send(bytes, n); } catch (...) { /* drop silently */ }
    }
}

void WebRtcTransport::closePeer(PeerId peer, bool /*sendNotification*/) {
    d_->closePeerInternal(peer);
}

int WebRtcTransport::connectionCount() {
    std::lock_guard<std::mutex> lk(d_->mtx);
    int n = 0;
    for (auto& kv : d_->peers)
        if (kv.second.open && kv.second.dc) ++n;
    return n;
}

} // namespace net
} // namespace jefe

#endif // JEFECHECK_WEBRTC
