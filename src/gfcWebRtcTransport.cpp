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
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

#include "gfcSignaling.h"
#include "gfcCoordinatorSignaling.h"  // JEF-27 coordinator-mode signaling

namespace jefe {
namespace net {

namespace {

// ── iceServers JSON → rtc::IceServer (JEF-27 / JEF-26) ───────────────────────
// The coordinator hands us the iceServers as an opaque raw-JSON array substring
// (see gfcCoordinatorSignaling.h): [{"urls":<string|string[]>,"username?":...,
// "credential?":...}, ...]. We parse it here — where rtc types live — into
// rtc::Configuration.iceServers. Defensive: malformed input yields an empty
// list and never throws (LAN behavior, no ICE servers).

void jsonSkipWs(const std::string& s, size_t& i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

// Parse a JSON string literal at s[i]=='"' into `val`; advance past the close
// quote. Handles the common escapes. Returns false on malformation.
bool jsonParseString(const std::string& s, size_t& i, std::string& val) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    val.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case 'n':  val += '\n'; break;
                case 'r':  val += '\r'; break;
                case 't':  val += '\t'; break;
                case 'b':  val += '\b'; break;
                case 'f':  val += '\f'; break;
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case '/':  val += '/';  break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= unsigned(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= unsigned(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= unsigned(h - 'A' + 10);
                        else return false;
                    }
                    if (code < 0x80) {
                        val += char(code);
                    } else if (code < 0x800) {
                        val += char(0xC0 | (code >> 6));
                        val += char(0x80 | (code & 0x3F));
                    } else {
                        val += char(0xE0 | (code >> 12));
                        val += char(0x80 | ((code >> 6) & 0x3F));
                        val += char(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: val += e; break;
            }
        } else {
            val += c;
        }
    }
    return false;  // unterminated
}

std::vector<rtc::IceServer> parseIceServers(const std::string& json) {
    std::vector<rtc::IceServer> out;
    if (json.empty()) return out;
    size_t i = 0;
    jsonSkipWs(json, i);
    if (i >= json.size() || json[i] != '[') return {};
    ++i;
    while (i < json.size()) {
        jsonSkipWs(json, i);
        if (i >= json.size()) return {};
        if (json[i] == ']') { ++i; break; }
        if (json[i] != '{') return {};  // expected an object
        ++i;

        std::vector<std::string> urls;
        std::string username, credential;

        while (i < json.size()) {
            jsonSkipWs(json, i);
            if (i >= json.size()) return {};
            if (json[i] == '}') { ++i; break; }
            std::string key;
            if (!jsonParseString(json, i, key)) return {};
            jsonSkipWs(json, i);
            if (i >= json.size() || json[i] != ':') return {};
            ++i;
            jsonSkipWs(json, i);
            if (i >= json.size()) return {};
            if (json[i] == '"') {
                std::string val;
                if (!jsonParseString(json, i, val)) return {};
                if (key == "urls" || key == "url") urls.push_back(val);
                else if (key == "username") username = val;
                else if (key == "credential") credential = val;
            } else if (json[i] == '[') {
                ++i;  // urls array
                while (i < json.size()) {
                    jsonSkipWs(json, i);
                    if (i >= json.size()) return {};
                    if (json[i] == ']') { ++i; break; }
                    if (json[i] == '"') {
                        std::string val;
                        if (!jsonParseString(json, i, val)) return {};
                        if (key == "urls" || key == "url") urls.push_back(val);
                    } else {
                        return {};  // non-string url element: malformed
                    }
                    jsonSkipWs(json, i);
                    if (i < json.size() && json[i] == ',') { ++i; continue; }
                }
            } else {
                // Scalar (number/bool/null): skip to the next , or }.
                while (i < json.size() && json[i] != ',' && json[i] != '}') ++i;
            }
            jsonSkipWs(json, i);
            if (i < json.size() && json[i] == ',') { ++i; continue; }
        }

        for (auto& u : urls) {
            try {
                rtc::IceServer s(u);  // scheme (stun:/turn:) sets the type
                if (!username.empty()) s.username = username;
                if (!credential.empty()) s.password = credential;
                out.push_back(std::move(s));
            } catch (...) {
                // Bad URL: skip this entry, keep the rest.
            }
        }

        jsonSkipWs(json, i);
        if (i < json.size() && json[i] == ',') { ++i; continue; }
        if (i < json.size() && json[i] == ']') { ++i; break; }
    }
    return out;
}


// InitLogger must run exactly once per process (guard against double-init).
std::once_flag g_loggerOnce;
void initLoggerOnce() {
    std::call_once(g_loggerOnce, [] { rtc::InitLogger(rtc::LogLevel::Error); });
}

// Opt-in state-transition tracing for the two-process --remote-test-webrtc
// harness (set JEFECHECK_REMOTE_TEST_DEBUG=1). Off by default so production
// sessions stay quiet.
bool traceEnabled() {
    static const bool on = std::getenv("JEFECHECK_REMOTE_TEST_DEBUG") != nullptr;
    return on;
}
void trace(const char* role, const char* what) {
    if (traceEnabled())
        std::fprintf(stderr, "[webrtc:%s] %s\n", role, what);
}
// Attach pc/gathering-state loggers (no-op unless tracing is on).
void attachStateTrace(const std::shared_ptr<rtc::PeerConnection>& pc,
                      const char* role) {
    if (!traceEnabled()) return;
    pc->onStateChange([role](rtc::PeerConnection::State s) {
        std::fprintf(stderr, "[webrtc:%s] pc state=%d\n", role,
                     static_cast<int>(s));
    });
    pc->onGatheringStateChange([role](rtc::PeerConnection::GatheringState g) {
        std::fprintf(stderr, "[webrtc:%s] gathering state=%d\n", role,
                     static_cast<int>(g));
    });
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
        std::string coordPeerId;  // JEF-27: coordinator connId (coordinator mode)
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;        // "jefe" — Channel::State
        std::shared_ptr<rtc::DataChannel> assetsDc;  // "assets" — Channel::Assets
        bool open = false;        // state ("jefe") channel open
        bool assetsOpen = false;  // assets channel open (JEF-28)
    };

    SignalingServer signaling;        // host role (LAN)
    SignalingClient clientSignaling;  // client role (LAN)

    // ── JEF-27 coordinator mode ──────────────────────────────────────────
    // When coordinatorMode is set (via configureCoordinator, before
    // startHost/connect), the transport dials a CoordinatorSignaling to the
    // cloud coordinator instead of the LAN SignalingServer/Client. The P2P
    // PeerConnection/DataChannel/event-queue machinery below is reused verbatim.
    bool coordinatorMode = false;
    std::string coordinatorUrl;
    std::string coordSessionCode;   // join-only (host: empty)
    std::string coordPassword;
    std::string assignedCode;       // host: code the coordinator handed us
    std::string coordIceServersJson;  // opaque raw-JSON array, "" if none
    std::unique_ptr<CoordinatorSignaling> coord;
    std::map<std::string, PeerId> coordToPeer;  // coordinator connId -> PeerId
    std::string clientHostCoordId;  // joiner: the host's coordinator connId
    bool clientPcBuilt = false;     // joiner: guard against double-build

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
                   std::vector<unsigned char> bytes = {},
                   Channel channel = Channel::State) {
        std::lock_guard<std::mutex> lk(mtx);
        TransportEvent ev;
        ev.type = type;
        ev.peer = peer;
        ev.bytes = std::move(bytes);
        ev.channel = channel;
        events.push_back(std::move(ev));
    }

    // Build the rtc::Configuration for a PeerConnection. In coordinator mode
    // this folds in the coordinator-supplied iceServers (STUN/TURN, JEF-26); in
    // LAN mode coordIceServersJson is empty, so this is an empty config (the
    // JEF-24 host-candidates-only behavior — unchanged).
    rtc::Configuration makeConfig() {
        std::string js;
        { std::lock_guard<std::mutex> lk(mtx); js = coordIceServersJson; }
        rtc::Configuration cfg;
        for (auto& s : parseIceServers(js)) cfg.iceServers.push_back(s);
        if (traceEnabled() && !cfg.iceServers.empty())
            std::fprintf(stderr, "[webrtc] applied %zu coordinator iceServer(s)\n",
                         cfg.iceServers.size());
        return cfg;
    }

    // Attach handlers to a reliable channel the client opened (host side).
    // JEF-28: onDataChannel fires once per channel; branch on the label so the
    // "jefe" channel drives State + peer lifecycle (PeerConnected/PeerLost) and
    // the "assets" channel is a silent Channel::Assets lane (its open/close does
    // NOT emit peer events — the state channel is the single source of truth for
    // whether a peer is connected).
    void setupChannel(PeerId pid, std::shared_ptr<rtc::DataChannel> dc) {
        const bool isAssets = (dc->label() == "assets");
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(pid);
            if (it == peers.end()) return;  // peer already gone
            if (isAssets) it->second.assetsDc = dc;
            else          it->second.dc = dc;
        }

        dc->onOpen([this, pid, isAssets]() {
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(pid);
                if (it != peers.end()) {
                    if (isAssets) it->second.assetsOpen = true;
                    else          it->second.open = true;
                }
            }
            if (isAssets) {
                trace("host", "peer ASSETS channel OPEN");  // silent
            } else {
                trace("host", "peer channel OPEN");
                pushEvent(TransportEventType::PeerConnected, pid);
            }
        });

        // Binary frames only (jefe::wire frames). Text is unused on the wire.
        dc->onMessage(
            [this, pid, isAssets](rtc::binary msg) {
                std::vector<unsigned char> bytes(msg.size());
                for (size_t i = 0; i < msg.size(); ++i)
                    bytes[i] = static_cast<unsigned char>(msg[i]);
                pushEvent(TransportEventType::Data, pid, std::move(bytes),
                          isAssets ? Channel::Assets : Channel::State);
            },
            [](rtc::string) { /* text unused */ });

        dc->onClosed([this, pid, isAssets]() {
            if (isAssets) {
                // Assets channel closing is silent; the state channel drives
                // PeerLost. Just clear the readiness flag.
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(pid);
                if (it != peers.end()) it->second.assetsOpen = false;
                return;
            }
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
        trace("host", "signaling client connected");
        // Early bail if already visibly at capacity (cheap; the authoritative
        // check is re-done under the lock at insert time to close the TOCTOU).
        if (maxClients > 0) {
            std::lock_guard<std::mutex> lk(mtx);
            if (static_cast<int>(peers.size()) >= maxClients) {
                return;  // At capacity: ignore. (Client gets no answer.)
            }
        }

        PeerId pid = nextPeerId.fetch_add(1);

        rtc::Configuration config = makeConfig();  // LAN: empty; coord: iceServers
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

        attachStateTrace(pc, "host");

        // Host is the answerer: it RECEIVES the channel, never creates one.
        pc->onDataChannel([this, pid](std::shared_ptr<rtc::DataChannel> dc) {
            trace("host", "onDataChannel");
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
        const bool isAssets = (dc->label() == "assets");  // JEF-28

        dc->onOpen([this, isAssets]() {
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(kHostPeerId);
                if (it != peers.end()) {
                    if (isAssets) it->second.assetsOpen = true;
                    else          it->second.open = true;
                }
            }
            if (isAssets) {
                trace("client", "assets datachannel OPEN");  // silent
            } else {
                trace("client", "datachannel OPEN");
                pushEvent(TransportEventType::ConnectAccepted, kHostPeerId);
            }
        });

        dc->onMessage(
            [this, isAssets](rtc::binary msg) {
                std::vector<unsigned char> bytes(msg.size());
                for (size_t i = 0; i < msg.size(); ++i)
                    bytes[i] = static_cast<unsigned char>(msg[i]);
                pushEvent(TransportEventType::Data, kHostPeerId,
                          std::move(bytes),
                          isAssets ? Channel::Assets : Channel::State);
            },
            [](rtc::string) { /* text unused */ });

        dc->onClosed([this, isAssets]() {
            if (isAssets) {
                // Assets channel closing is silent; the state channel drives the
                // ConnectionLost/Disconnected lifecycle. Just clear readiness.
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(kHostPeerId);
                if (it != peers.end()) it->second.assetsOpen = false;
                return;
            }
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
        trace("client", "signaling open");
        // Bail if the caller already tore the session down (connect() then an
        // immediate disconnect() racing this WS-thread callback).
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!clientActive) return;
        }

        rtc::Configuration config = makeConfig();  // LAN: empty; coord: iceServers
        std::shared_ptr<rtc::PeerConnection> pc;
        try {
            pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: client PeerConnection failed: %s\n",
                         e.what());
            pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
            return;
        }

        attachStateTrace(pc, "client");

        pc->onLocalDescription([this](rtc::Description desc) {
            trace("client", "-> local offer/desc");
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

        // JEF-28: open BOTH the "jefe" (State) and "assets" (Assets) channels.
        std::shared_ptr<rtc::DataChannel> dc, assetsDc;
        try {
            dc = pc->createDataChannel("jefe", init);
            assetsDc = pc->createDataChannel("assets", init);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: createDataChannel failed: %s\n",
                         e.what());
            if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
            pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
            return;
        }

        setupClientChannel(dc);
        setupClientChannel(assetsDc);

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
                p.assetsDc = assetsDc;
                peers[kHostPeerId] = std::move(p);
                inserted = true;
            }
        }
        if (!inserted) {
            // Session was torn down mid-construction: drop the orphan quietly
            // (reset callbacks first so it can't push a stale event).
            try { dc->resetCallbacks(); dc->close(); } catch (...) {}
            try { assetsDc->resetCallbacks(); assetsDc->close(); } catch (...) {}
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
                trace("client", "<- remote answer");
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
        std::shared_ptr<rtc::DataChannel> dc, assetsDc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            // Flip the session guard first so any in-flight WS-thread callback
            // (onClientSignalingOpen still constructing a pc/dc) bails / drops
            // its orphan instead of publishing it after we return. Idempotent:
            // a second disconnect() finds no peer and no active session.
            clientActive = false;
            clientPcBuilt = false;
            clientHostCoordId.clear();
            auto it = peers.find(kHostPeerId);
            if (it != peers.end()) {
                pc = it->second.pc;
                dc = it->second.dc;
                assetsDc = it->second.assetsDc;
                coordToPeer.erase(it->second.coordPeerId);
                peers.erase(it);
            }
        }
        // Reset callbacks before close so onClosed can't push a spurious event.
        if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
        if (assetsDc) { try { assetsDc->resetCallbacks(); assetsDc->close(); } catch (...) {} }
        if (pc) { try { pc->resetCallbacks(); pc->close(); } catch (...) {} }
        clientSignaling.close();
        if (coord) coord->close();  // JEF-27: also drop the coordinator socket
        client = false;
    }

    // Close + erase a peer. Returns true if it existed.
    bool closePeerInternal(PeerId peer) {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc, assetsDc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(peer);
            if (it == peers.end()) return false;
            pc = it->second.pc;
            dc = it->second.dc;
            assetsDc = it->second.assetsDc;
            clientToPeer.erase(it->second.clientId);
            if (!it->second.coordPeerId.empty())
                coordToPeer.erase(it->second.coordPeerId);
            peers.erase(it);
        }
        // Reset callbacks + close outside the lock so no callback re-enters.
        // resetCallbacks() on BOTH channels and the PeerConnection (teardown
        // symmetry) so no forwarder fires into a half-dead peer.
        if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
        if (assetsDc) { try { assetsDc->resetCallbacks(); assetsDc->close(); } catch (...) {} }
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
            coordToPeer.clear();
            clientPcBuilt = false;
            clientHostCoordId.clear();
        }
        for (auto& kv : toClose) {
            if (kv.second.dc) {
                try { kv.second.dc->resetCallbacks(); kv.second.dc->close(); }
                catch (...) {}
            }
            if (kv.second.assetsDc) {
                try { kv.second.assetsDc->resetCallbacks(); kv.second.assetsDc->close(); }
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
        if (coord) coord->close();  // JEF-27: drop the coordinator socket too
        hosting = false;
        client = false;
    }

    // ── JEF-27 coordinator mode: HOST ────────────────────────────────────
    // The host create-session's; each coordinator `peer-joined` spins up an
    // ANSWERER PeerConnection (host never creates the channel) whose SDP/ICE is
    // relayed back to that joiner via coord->sendSignal(coordPeerId, ...).

    void onCoordSessionCreated(const std::string& code, const std::string& /*token*/,
                               const std::string& iceServersJson) {
        std::lock_guard<std::mutex> lk(mtx);
        assignedCode = code;
        coordIceServersJson = iceServersJson;
        trace("host", "coordinator session created");
    }

    void onCoordHostPeerJoined(const std::string& coordPeerId) {
        trace("host", "coordinator peer joined");
        if (maxClients > 0) {
            std::lock_guard<std::mutex> lk(mtx);
            if (static_cast<int>(peers.size()) >= maxClients) return;  // at capacity
        }

        PeerId pid = nextPeerId.fetch_add(1);

        rtc::Configuration config = makeConfig();  // coordinator iceServers
        std::shared_ptr<rtc::PeerConnection> pc;
        try {
            pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: coord PeerConnection failed: %s\n",
                         e.what());
            return;
        }

        pc->onLocalDescription([this, coordPeerId, pid](rtc::Description desc) {
            SignalMessage m;
            m.type = desc.typeString();  // "answer" for the host
            m.sdp = std::string(desc);
            m.peer = static_cast<int>(pid);
            if (coord) coord->sendSignal(coordPeerId, m);
        });
        pc->onLocalCandidate([this, coordPeerId, pid](rtc::Candidate cand) {
            SignalMessage m;
            m.type = "candidate";
            m.candidate = std::string(cand);
            m.mid = cand.mid();
            m.peer = static_cast<int>(pid);
            if (coord) coord->sendSignal(coordPeerId, m);
        });
        attachStateTrace(pc, "host");
        pc->onDataChannel([this, pid](std::shared_ptr<rtc::DataChannel> dc) {
            trace("host", "coord onDataChannel");
            setupChannel(pid, dc);
        });

        bool inserted = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (maxClients == 0 || static_cast<int>(peers.size()) < maxClients) {
                Peer p;
                p.coordPeerId = coordPeerId;
                p.pc = pc;
                peers[pid] = std::move(p);
                coordToPeer[coordPeerId] = pid;
                inserted = true;
            }
        }
        if (!inserted) {
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
        }
    }

    void onCoordHostSignal(const std::string& fromCoordId, const SignalMessage& m) {
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto cit = coordToPeer.find(fromCoordId);
            if (cit == coordToPeer.end()) return;
            auto pit = peers.find(cit->second);
            if (pit == peers.end()) return;
            pc = pit->second.pc;
        }
        if (!pc) return;
        try {
            if (m.type == "offer" || m.type == "answer") {
                pc->setRemoteDescription(rtc::Description(m.sdp, m.type));
            } else if (m.type == "candidate") {
                pc->addRemoteCandidate(rtc::Candidate(m.candidate, m.mid));
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: coord host apply failed: %s\n",
                         e.what());
        }
    }

    void onCoordHostPeerLeft(const std::string& coordPeerId) {
        PeerId pid = kInvalidPeerId;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = coordToPeer.find(coordPeerId);
            if (it != coordToPeer.end()) pid = it->second;
        }
        if (pid == kInvalidPeerId) return;
        // Peer left the session: notify then tear the PeerConnection down.
        pushEvent(TransportEventType::PeerLost, pid);
        closePeerInternal(pid);
    }

    // ── JEF-27 coordinator mode: JOINER ──────────────────────────────────
    // The joiner is the OFFERER. It joins by code; the roster (or a fallback
    // peer-joined) tells it the host's coordinator connId, then it builds the
    // offerer PeerConnection + "jefe" channel and relays SDP/ICE to the host.

    void buildClientOfferer(const std::string& hostCoordId) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!clientActive || clientPcBuilt) return;  // once only
            clientPcBuilt = true;
            clientHostCoordId = hostCoordId;
        }

        rtc::Configuration config = makeConfig();  // coordinator iceServers
        std::shared_ptr<rtc::PeerConnection> pc;
        try {
            pc = std::make_shared<rtc::PeerConnection>(config);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: coord client PC failed: %s\n",
                         e.what());
            pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
            return;
        }
        attachStateTrace(pc, "client");

        pc->onLocalDescription([this, hostCoordId](rtc::Description desc) {
            trace("client", "coord -> local offer/desc");
            SignalMessage m;
            m.type = desc.typeString();  // "offer" for the joiner (offerer)
            m.sdp = std::string(desc);
            m.peer = static_cast<int>(kHostPeerId);
            if (coord) coord->sendSignal(hostCoordId, m);
        });
        pc->onLocalCandidate([this, hostCoordId](rtc::Candidate cand) {
            SignalMessage m;
            m.type = "candidate";
            m.candidate = std::string(cand);
            m.mid = cand.mid();
            m.peer = static_cast<int>(kHostPeerId);
            if (coord) coord->sendSignal(hostCoordId, m);
        });

        rtc::DataChannelInit init;
        init.reliability.unordered = false;
        init.reliability.maxPacketLifeTime.reset();
        init.reliability.maxRetransmits.reset();

        // JEF-28: open BOTH the "jefe" (State) and "assets" (Assets) channels.
        std::shared_ptr<rtc::DataChannel> dc, assetsDc;
        try {
            dc = pc->createDataChannel("jefe", init);
            assetsDc = pc->createDataChannel("assets", init);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: coord createDataChannel failed: %s\n",
                         e.what());
            if (dc) { try { dc->resetCallbacks(); dc->close(); } catch (...) {} }
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
            pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
            return;
        }
        setupClientChannel(dc);
        setupClientChannel(assetsDc);

        bool inserted = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (clientActive) {
                Peer p;
                p.coordPeerId = hostCoordId;
                p.pc = pc;
                p.dc = dc;
                p.assetsDc = assetsDc;
                peers[kHostPeerId] = std::move(p);
                coordToPeer[hostCoordId] = kHostPeerId;
                inserted = true;
            }
        }
        if (!inserted) {
            try { dc->resetCallbacks(); dc->close(); } catch (...) {}
            try { assetsDc->resetCallbacks(); assetsDc->close(); } catch (...) {}
            try { pc->resetCallbacks(); pc->close(); } catch (...) {}
        }
    }

    void onCoordClientRoster(const std::vector<std::string>& peersList,
                             const std::string& iceServersJson) {
        { std::lock_guard<std::mutex> lk(mtx); coordIceServersJson = iceServersJson; }
        // The roster EXCLUDES the joiner; the host is the (first) existing peer.
        if (!peersList.empty()) buildClientOfferer(peersList.front());
        // Empty roster (host not present yet): wait for peer-joined.
    }

    void onCoordClientPeerJoined(const std::string& coordPeerId) {
        // Fallback path when we joined before the host's roster entry existed.
        buildClientOfferer(coordPeerId);
    }

    void onCoordClientSignal(const std::string& /*fromCoordId*/, const SignalMessage& m) {
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(kHostPeerId);
            if (it != peers.end()) pc = it->second.pc;
        }
        if (!pc) return;
        try {
            if (m.type == "answer") {
                trace("client", "coord <- remote answer");
                pc->setRemoteDescription(rtc::Description(m.sdp, m.type));
            } else if (m.type == "candidate") {
                pc->addRemoteCandidate(rtc::Candidate(m.candidate, m.mid));
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WebRtcTransport: coord client apply failed: %s\n",
                         e.what());
        }
    }

    void onCoordClientError(const std::string& code, const std::string& msg) {
        std::fprintf(stderr, "WebRtcTransport: coordinator error [%s] %s\n",
                     code.c_str(), msg.c_str());
        pushEvent(TransportEventType::ConnectFailed, kHostPeerId);
    }

    // Bring up the coordinator socket for the HOST (create-session on open).
    bool startHostCoordinator() {
        coord = std::make_unique<CoordinatorSignaling>();
        Impl* d = this;
        coord->onSessionCreated([d](std::string c, std::string t, std::string ice) {
            d->onCoordSessionCreated(c, t, ice);
        });
        coord->onPeerJoined([d](std::string p) { d->onCoordHostPeerJoined(p); });
        coord->onPeerLeft([d](std::string p) { d->onCoordHostPeerLeft(p); });
        coord->onSignal([d](std::string from, SignalMessage m) {
            d->onCoordHostSignal(from, m);
        });
        coord->onError([d](std::string c, std::string m) {
            std::fprintf(stderr, "WebRtcTransport: coordinator error [%s] %s\n",
                         c.c_str(), m.c_str());
        });
        coord->onOpen([d]() { if (d->coord) d->coord->createSession(); });
        if (!coord->connect(coordinatorUrl)) {
            std::fprintf(stderr, "WebRtcTransport: coordinator connect failed (host)\n");
            return false;
        }
        hosting = true;
        return true;
    }

    // Bring up the coordinator socket for the JOINER (join-session on open).
    bool connectCoordinator() {
        { std::lock_guard<std::mutex> lk(mtx); clientActive = true; }
        coord = std::make_unique<CoordinatorSignaling>();
        Impl* d = this;
        coord->onRoster([d](std::vector<std::string> p, std::string ice) {
            d->onCoordClientRoster(p, ice);
        });
        coord->onPeerJoined([d](std::string p) { d->onCoordClientPeerJoined(p); });
        coord->onSignal([d](std::string from, SignalMessage m) {
            d->onCoordClientSignal(from, m);
        });
        coord->onError([d](std::string c, std::string m) {
            d->onCoordClientError(c, m);
        });
        std::string code = coordSessionCode;
        coord->onOpen([d, code]() { if (d->coord) d->coord->joinSession(code); });
        if (!coord->connect(coordinatorUrl)) {
            std::fprintf(stderr, "WebRtcTransport: coordinator connect failed (join)\n");
            std::lock_guard<std::mutex> lk(mtx);
            clientActive = false;
            return false;
        }
        client = true;
        return true;
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

void WebRtcTransport::configureCoordinator(const std::string& url,
                                           const std::string& sessionCode,
                                           const std::string& password) {
    d_->coordinatorMode = true;
    d_->coordinatorUrl = url;
    d_->coordSessionCode = sessionCode;
    d_->coordPassword = password;
}

std::string WebRtcTransport::assignedSessionCode() {
    std::lock_guard<std::mutex> lk(d_->mtx);
    return d_->assignedCode;
}

bool WebRtcTransport::startHost(unsigned short port, const std::string& /*password*/,
                                int maxClients) {
    if (d_->hosting) return false;
    d_->maxClients = maxClients;

    // JEF-27: coordinator mode dials the cloud coordinator instead of a LAN
    // SignalingServer. Everything downstream (PeerConnection/DataChannel/event
    // queue) is shared.
    if (d_->coordinatorMode) return d_->startHostCoordinator();

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

    // JEF-27: coordinator mode joins the cloud session by code instead of
    // dialing a LAN SignalingServer at ip:port.
    if (d_->coordinatorMode) return d_->connectCoordinator();

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
                           bool broadcastExcluding, Channel channel) {
    if (len <= 0 || data == nullptr) return;

    // JEF-28: pick the state ("jefe") or assets channel per peer based on the
    // requested lane. If the chosen channel isn't open yet, the peer is skipped
    // (same drop-if-not-open behavior as the state channel had before).
    const bool assets = (channel == Channel::Assets);
    auto pick = [assets](const Impl::Peer& p) -> std::shared_ptr<rtc::DataChannel> {
        if (assets) return (p.assetsOpen && p.assetsDc) ? p.assetsDc : nullptr;
        return (p.open && p.dc) ? p.dc : nullptr;
    };

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
                if (it != d_->peers.end()) {
                    if (auto dc = pick(it->second)) targets.push_back(dc);
                }
            }
        } else {
            // Broadcast to everyone except `target`
            // (target==kInvalidPeerId → nothing to exclude → everyone).
            for (auto& kv : d_->peers) {
                if (kv.first == target) continue;
                if (auto dc = pick(kv.second)) targets.push_back(dc);
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
