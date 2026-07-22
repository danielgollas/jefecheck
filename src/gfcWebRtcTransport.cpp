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

    SignalingServer signaling;

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
        if (maxClients > 0) {
            std::lock_guard<std::mutex> lk(mtx);
            if (static_cast<int>(peers.size()) >= maxClients) {
                // At capacity: ignore. (Client sees no answer / times out.)
                return;
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

        {
            std::lock_guard<std::mutex> lk(mtx);
            Peer p;
            p.clientId = clientId;
            p.pc = pc;
            peers[pid] = std::move(p);
            clientToPeer[clientId] = pid;
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
        if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
        if (pc) { try { pc->close(); } catch (...) {} }
        return true;
    }

    void teardownAll() {
        std::map<PeerId, Peer> toClose;
        {
            std::lock_guard<std::mutex> lk(mtx);
            toClose.swap(peers);
            clientToPeer.clear();
        }
        for (auto& kv : toClose) {
            if (kv.second.dc) {
                try { kv.second.dc->resetCallbacks(); kv.second.dc->close(); }
                catch (...) {}
            }
            if (kv.second.pc) {
                try { kv.second.pc->close(); } catch (...) {}
            }
        }
        toClose.clear();
        signaling.stop();
        hosting = false;
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

// ── Client role — Task 4 ────────────────────────────────────────────────
bool WebRtcTransport::connect(const std::string& /*ip*/, unsigned short /*port*/,
                              const std::string& /*password*/) {
    return false;  // Task 4
}

void WebRtcTransport::disconnect() {
    // Task 4
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
    std::vector<std::shared_ptr<rtc::DataChannel>> targets;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        if (!broadcastExcluding && target != kInvalidPeerId) {
            auto it = d_->peers.find(target);
            if (it != d_->peers.end() && it->second.open && it->second.dc)
                targets.push_back(it->second.dc);
        } else {
            // broadcastExcluding: everyone except `target`
            // (target==kInvalidPeerId → everyone).
            for (auto& kv : d_->peers) {
                if (broadcastExcluding && kv.first == target) continue;
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
