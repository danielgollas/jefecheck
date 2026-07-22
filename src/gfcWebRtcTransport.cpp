// JEF-24 Task 3: WebRtcTransport — HOST role over libdatachannel.
// See gfcWebRtcTransport.h for the offerer/answerer convention and the
// no-envelope-byte contract. The whole TU is guarded so a build without
// JEFECHECK_WEBRTC (factory then keeps the RakNet fallback) still links.
#include "gfcWebRtcTransport.h"

#ifdef JEFECHECK_WEBRTC

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// ── JEF-28 Task 4: transport-level chunking on the "assets" channel ──────────
//
// The sync layer (gfcnetworkserver/client) hands the transport whole serialized
// LUT/FX bodies in ONE send() call. libdatachannel's SCTP data channel has a
// per-message size limit (256 KB local default, but only 64 KB is guaranteed
// when the peer doesn't advertise a larger max-message-size in its SDP), and a
// single over-limit send() fails. So Channel::Assets sends are split at the
// TRANSPORT level into ordered chunks; the sync layer stays oblivious and the
// receive side reassembles them into ONE Data event. The State ("jefe") channel
// carries small messages and is never chunked.
//
// This framing exists ONLY on the "assets" channel and NEVER carries the RakNet
// envelope byte — the reassembled payload is the app's jefe::wire frame verbatim
// (leading GFCNETID byte intact). Chunk wire format (little-endian), prepended
// to every "assets" frame (single/complete messages use chunkCount==1 so the
// receive path is uniform):
//
//   byte  0      magic 'A' (0x41)
//   byte  1      magic 'C' (0x43)
//   byte  2      version  (currently 1)
//   byte  3      flags    (reserved, 0)
//   bytes 4..7   messageId   uint32  — per-peer monotonic, from 1
//   bytes 8..11  chunkIndex  uint32  — 0-based
//   bytes 12..15 chunkCount  uint32  — total chunks (1 = unchunked/complete)
//   bytes 16..19 offset      uint32  — this chunk's byte offset in the message
//   bytes 20..23 totalLen    uint32  — full reassembled payload length
//   bytes 24..N  payload slice
//
// `offset` makes reassembly independent of the sender's chunk size (the receiver
// never has to know it), so the sender may clamp the chunk size to the
// negotiated max-message-size without a wire renegotiation.
constexpr size_t   kAssetHeaderSize    = 24;
constexpr size_t   kAssetChunkPayload  = 60 * 1024;      // 61440: frame < 64 KB
constexpr size_t   kAssetHighWater     = 1 * 1024 * 1024; // pause sending at 1 MB
constexpr size_t   kAssetLowThreshold  = 256 * 1024;     // resume-drain threshold
constexpr size_t   kAssetMaxMessage    = 128 * 1024 * 1024; // reject bigger (rx guard)
constexpr uint8_t  kAssetMagic0        = 'A';
constexpr uint8_t  kAssetMagic1        = 'C';
constexpr uint8_t  kAssetVersion       = 1;

inline void wr32le(unsigned char* p, uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}
inline uint32_t rd32le(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
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

        // ── JEF-28 Task 4: assets-channel chunking state (guarded by Impl::mtx) ─
        // TX: pending, ready-to-send chunk frames + a monotonic per-peer message
        // id. Draining is serialized by assetTxDraining (only one drainer at a
        // time; the other caller — send() on the app thread or onBufferedAmountLow
        // on the rtc thread — returns and lets the active drainer continue).
        std::deque<rtc::binary> assetTxQueue;
        uint32_t assetTxMsgId   = 0;
        bool     assetTxDraining = false;
        // RX: reassembly buffer for the message currently in flight (SCTP is
        // reliable+ordered, so at most one message reassembles at a time; the
        // per-chunk `got` flags dedup and the fields are cross-checked defensively).
        uint32_t assetRxMsgId        = 0;   // 0 = nothing in progress
        uint32_t assetRxChunkCount   = 0;
        uint32_t assetRxTotalLen     = 0;
        uint32_t assetRxReceived     = 0;
        uint32_t assetRxLastComplete = 0;   // highest fully-reassembled msg id
        std::vector<unsigned char> assetRxBuf;
        std::vector<char>          assetRxGot;
        // Diagnostics (JEFECHECK_REMOTE_TEST_DEBUG): totals + peak buffered.
        uint64_t assetTxChunks   = 0;
        uint64_t assetRxChunks   = 0;
        size_t   assetTxMaxBuffered = 0;
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

    // ── JEF-28 Task 4: assets-channel chunk TX ───────────────────────────────
    // Split a whole asset payload into ordered chunk frames and append them to
    // `pid`'s TX queue (caller then calls drainAssetQueue). Runs under mtx.
    void enqueueAssetMessage(PeerId pid, const unsigned char* data, size_t len) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = peers.find(pid);
        if (it == peers.end()) return;
        Peer& p = it->second;
        const uint32_t msgId = ++p.assetTxMsgId;   // monotonic from 1

        size_t chunkPayload = kAssetChunkPayload;
        // Clamp to the negotiated max-message-size when known, so we never hand
        // SCTP an over-limit frame (offset in the header keeps rx independent).
        if (p.assetsDc) {
            size_t mx = 0;
            try { mx = p.assetsDc->maxMessageSize(); } catch (...) {}
            if (mx > kAssetHeaderSize + 1024 && mx - kAssetHeaderSize < chunkPayload)
                chunkPayload = mx - kAssetHeaderSize;
        }
        size_t count = (len == 0) ? 1 : (len + chunkPayload - 1) / chunkPayload;
        for (size_t i = 0; i < count; ++i) {
            const size_t off     = i * chunkPayload;
            const size_t thisLen = (len > off) ? std::min(chunkPayload, len - off) : 0;
            rtc::binary frame(kAssetHeaderSize + thisLen);
            unsigned char* h = reinterpret_cast<unsigned char*>(frame.data());
            h[0] = kAssetMagic0; h[1] = kAssetMagic1; h[2] = kAssetVersion; h[3] = 0;
            wr32le(h + 4,  msgId);
            wr32le(h + 8,  static_cast<uint32_t>(i));
            wr32le(h + 12, static_cast<uint32_t>(count));
            wr32le(h + 16, static_cast<uint32_t>(off));
            wr32le(h + 20, static_cast<uint32_t>(len));
            if (thisLen) std::memcpy(h + kAssetHeaderSize, data + off, thisLen);
            p.assetTxQueue.push_back(std::move(frame));
        }
        if (traceEnabled())
            std::fprintf(stderr, "[webrtc] assets TX msg=%u split into %zu chunk(s) "
                         "(%zu bytes)\n", msgId, count, len);
    }

    // Drain `pid`'s asset TX queue, honoring bufferedAmount backpressure. Sends
    // chunks while buffered < kAssetHighWater; when it hits the high-water mark
    // it stops and leaves the remainder queued — onBufferedAmountLow resumes the
    // drain once SCTP flushes below kAssetLowThreshold. Callable concurrently
    // from the app thread (send) and the rtc callback thread (onBufferedAmountLow);
    // assetTxDraining serializes to keep ordered chunks strictly in order. The
    // dc->send() calls happen OUTSIDE mtx (shared_ptr copied out), so the poll
    // path is never blocked and mtx is never held across a network send.
    void drainAssetQueue(PeerId pid) {
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(pid);
            if (it == peers.end()) return;
            Peer& p = it->second;
            if (p.assetTxDraining) return;             // another drainer is active
            if (p.assetTxQueue.empty()) return;
            if (!p.assetsOpen || !p.assetsDc) return;  // channel not ready
            dc = p.assetsDc;
            p.assetTxDraining = true;
        }
        for (;;) {
            rtc::binary frame;
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = peers.find(pid);
                if (it == peers.end()) return;         // peer gone; drop remainder
                Peer& p = it->second;
                size_t buffered = 0;
                try { buffered = dc->bufferedAmount(); } catch (...) {}
                if (buffered > p.assetTxMaxBuffered) p.assetTxMaxBuffered = buffered;
                if (p.assetTxQueue.empty()) { p.assetTxDraining = false; return; }
                if (buffered >= kAssetHighWater) {
                    // Backpressure: stop. onBufferedAmountLow will resume us when
                    // SCTP drains below kAssetLowThreshold (a fresh downward
                    // crossing, so no lost wakeup).
                    p.assetTxDraining = false;
                    if (traceEnabled())
                        std::fprintf(stderr, "[webrtc] assets TX paused: buffered=%zu "
                                     "queued=%zu\n", buffered, p.assetTxQueue.size());
                    return;
                }
                frame = std::move(p.assetTxQueue.front());
                p.assetTxQueue.pop_front();
                p.assetTxChunks++;
            }
            try { dc->send(frame.data(), frame.size()); } catch (...) { /* drop */ }
        }
    }

    // ── JEF-28 Task 4: assets-channel chunk RX / reassembly ──────────────────
    // Feed one inbound "assets" frame through header validation + reassembly.
    // Pushes ONE Data(Channel::Assets) event when the last chunk of a message
    // arrives. Never crashes on malformed/hostile input — every field is bounds
    // checked and an unreasonable total is rejected to bound memory.
    void handleAssetMessage(PeerId pid, const unsigned char* data, size_t n) {
        if (n < kAssetHeaderSize) return;                       // too short
        if (data[0] != kAssetMagic0 || data[1] != kAssetMagic1 ||
            data[2] != kAssetVersion) return;                   // bad magic/version
        const uint32_t msgId  = rd32le(data + 4);
        const uint32_t idx    = rd32le(data + 8);
        const uint32_t count  = rd32le(data + 12);
        const uint32_t offset = rd32le(data + 16);
        const uint32_t total  = rd32le(data + 20);
        const size_t   payLen = n - kAssetHeaderSize;
        // Structural + anti-abuse guards.
        if (count == 0) return;
        if (total == 0 && payLen == 0 && count == 1) { /* legit empty message */ }
        else if (total == 0) return;
        if (total > kAssetMaxMessage) return;                   // bound memory
        if (idx >= count) return;                               // index OOR
        if (static_cast<uint64_t>(count) > static_cast<uint64_t>(total) + 1)
            return;                                             // absurd chunkCount
        if (static_cast<uint64_t>(offset) + payLen > total) return;  // slice OOR

        std::vector<unsigned char> completed;
        bool haveCompleted = false;
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = peers.find(pid);
            if (it == peers.end()) return;
            Peer& p = it->second;
            p.assetRxChunks++;
            if (msgId <= p.assetRxLastComplete) return;         // stale / duplicate
            if (p.assetRxMsgId != msgId) {
                // Begin a new message (abandoning any incomplete prior — should
                // not happen on an ordered channel, but stay defensive).
                p.assetRxMsgId      = msgId;
                p.assetRxChunkCount = count;
                p.assetRxTotalLen   = total;
                p.assetRxReceived   = 0;
                p.assetRxBuf.assign(total, 0);
                p.assetRxGot.assign(count, 0);
            } else if (p.assetRxChunkCount != count || p.assetRxTotalLen != total) {
                return;                                         // inconsistent header
            }
            if (idx < p.assetRxGot.size() && !p.assetRxGot[idx]) {
                if (payLen) std::memcpy(p.assetRxBuf.data() + offset,
                                        data + kAssetHeaderSize, payLen);
                p.assetRxGot[idx] = 1;
                p.assetRxReceived++;
            }
            if (p.assetRxReceived == p.assetRxChunkCount) {
                completed = std::move(p.assetRxBuf);
                haveCompleted = true;
                p.assetRxLastComplete = msgId;
                p.assetRxMsgId    = 0;
                p.assetRxChunkCount = 0;
                p.assetRxReceived = 0;
                p.assetRxBuf.clear();
                p.assetRxGot.clear();
                if (traceEnabled())
                    std::fprintf(stderr, "[webrtc] assets RX msg=%u complete: "
                                 "%u chunk(s), %zu bytes\n", msgId, count,
                                 completed.size());
            }
        }
        if (haveCompleted)
            pushEvent(TransportEventType::Data, pid, std::move(completed),
                      Channel::Assets);
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

        // JEF-28 Task 4: on the assets channel, resume the paced chunk drain
        // whenever SCTP flushes below the low-water threshold (backpressure).
        if (isAssets) {
            dc->setBufferedAmountLowThreshold(kAssetLowThreshold);
            dc->onBufferedAmountLow([this, pid]() { drainAssetQueue(pid); });
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
                drainAssetQueue(pid);  // flush anything queued before open
            } else {
                trace("host", "peer channel OPEN");
                pushEvent(TransportEventType::PeerConnected, pid);
            }
        });

        // Binary frames only (jefe::wire frames). Text is unused on the wire.
        // Assets frames carry the chunk header (reassembled before dispatch);
        // state frames are the app payload verbatim.
        dc->onMessage(
            [this, pid, isAssets](rtc::binary msg) {
                const unsigned char* p =
                    reinterpret_cast<const unsigned char*>(msg.data());
                if (isAssets) { handleAssetMessage(pid, p, msg.size()); return; }
                std::vector<unsigned char> bytes(p, p + msg.size());
                pushEvent(TransportEventType::Data, pid, std::move(bytes),
                          Channel::State);
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

        // JEF-28 Task 4: backpressure resume on the client's assets channel.
        if (isAssets) {
            dc->setBufferedAmountLowThreshold(kAssetLowThreshold);
            dc->onBufferedAmountLow([this]() { drainAssetQueue(kHostPeerId); });
        }

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
                drainAssetQueue(kHostPeerId);  // flush anything queued pre-open
            } else {
                trace("client", "datachannel OPEN");
                pushEvent(TransportEventType::ConnectAccepted, kHostPeerId);
            }
        });

        dc->onMessage(
            [this, isAssets](rtc::binary msg) {
                const unsigned char* p =
                    reinterpret_cast<const unsigned char*>(msg.data());
                if (isAssets) { handleAssetMessage(kHostPeerId, p, msg.size()); return; }
                std::vector<unsigned char> bytes(p, p + msg.size());
                pushEvent(TransportEventType::Data, kHostPeerId, std::move(bytes),
                          Channel::State);
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

    const bool assets = (channel == Channel::Assets);
    const size_t n = static_cast<size_t>(len);

    // Resolve target PeerIds under the lock (RakNet parity, gfcTransport.h):
    //   broadcastExcluding=false → `target` ONLY; target==kInvalidPeerId → NOBODY.
    //   broadcastExcluding=true  → everyone EXCEPT `target`
    //                              (target==kInvalidPeerId → everyone).
    // We collect PeerIds (not raw channels) so the assets path can address each
    // peer's per-peer chunk queue; a peer whose target channel isn't open yet is
    // skipped (same drop-if-not-open behavior the state channel always had).
    std::vector<PeerId>                              statePeers;   // state fast path
    std::vector<std::shared_ptr<rtc::DataChannel>>   stateChans;
    std::vector<PeerId>                              assetPeers;   // chunked path
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        auto consider = [&](PeerId pid, const Impl::Peer& p) {
            if (assets) {
                if (p.assetsOpen && p.assetsDc) assetPeers.push_back(pid);
            } else if (p.open && p.dc) {
                statePeers.push_back(pid);
                stateChans.push_back(p.dc);
            }
        };
        if (!broadcastExcluding) {
            if (target != kInvalidPeerId) {
                auto it = d_->peers.find(target);
                if (it != d_->peers.end()) consider(it->first, it->second);
            }
        } else {
            for (auto& kv : d_->peers) {
                if (kv.first == target) continue;
                consider(kv.first, kv.second);
            }
        }
    }

    if (!assets) {
        // State channel: small messages, one frame each, no chunk header.
        const std::byte* bytes = reinterpret_cast<const std::byte*>(data);
        for (auto& dc : stateChans) {
            try { dc->send(bytes, n); } catch (...) { /* drop silently */ }
        }
        return;
    }

    // Assets channel (JEF-28 Task 4): split into ordered chunks per peer, then
    // drain with bufferedAmount backpressure. Enqueue for every target first,
    // then drain — draining one peer must not delay queueing the others.
    for (PeerId pid : assetPeers) d_->enqueueAssetMessage(pid, data, n);
    for (PeerId pid : assetPeers) d_->drainAssetQueue(pid);
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

// JEF-30: per-peer WebRTC stats. Lock discipline mirrors send()/drainAssetQueue:
// hold Impl::mtx ONLY long enough to copy each peer's id + pc shared_ptr + open
// flag out; then query libdatachannel (rtt/bytes/getSelectedCandidatePair) with
// the lock RELEASED — those calls touch the rtc worker/SCTP state and may block,
// and holding mtx across them would stall the poll/send path. Every read is
// best-effort: a throwing/absent value degrades to the field's default.
std::vector<PeerStats> WebRtcTransport::peerStats() {
    struct Snapshot {
        PeerId id;
        std::shared_ptr<rtc::PeerConnection> pc;
        bool open;
    };
    std::vector<Snapshot> snap;
    {
        std::lock_guard<std::mutex> lk(d_->mtx);
        snap.reserve(d_->peers.size());
        for (auto& kv : d_->peers)
            snap.push_back({kv.first, kv.second.pc, kv.second.open});
    }

    std::vector<PeerStats> out;
    out.reserve(snap.size());
    for (auto& s : snap) {
        PeerStats ps;
        ps.peer = s.id;
        // connected == the STATE ("jefe") channel is open (single source of
        // truth for peer liveness, matching connectionCount()/PeerConnected).
        ps.connected = s.open;
        if (!s.pc) { out.push_back(ps); continue; }

        try {
            auto r = s.pc->rtt();
            if (r.has_value()) ps.rttMs = static_cast<long>(r->count());
        } catch (...) {}
        try { ps.bytesSent = static_cast<uint64_t>(s.pc->bytesSent()); } catch (...) {}
        try { ps.bytesReceived = static_cast<uint64_t>(s.pc->bytesReceived()); }
        catch (...) {}

        try {
            rtc::Candidate local, remote;
            if (s.pc->getSelectedCandidatePair(&local, &remote)) {
                // Classify by the REMOTE candidate: a Relayed remote means the
                // media flows through a TURN server; anything else is a direct
                // (host / server-reflexive / peer-reflexive) path.
                switch (remote.type()) {
                    case rtc::Candidate::Type::Relayed:
                        ps.path = PeerStats::Path::Relay;
                        break;
                    case rtc::Candidate::Type::Host:
                    case rtc::Candidate::Type::ServerReflexive:
                    case rtc::Candidate::Type::PeerReflexive:
                        ps.path = PeerStats::Path::Direct;
                        break;
                    default:
                        ps.path = PeerStats::Path::Unknown;
                        break;
                }
            }
        } catch (...) {}

        out.push_back(ps);
    }
    return out;
}

} // namespace net
} // namespace jefe

#endif // JEFECHECK_WEBRTC
