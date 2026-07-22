# JEF-24: WebRtcTransport via libdatachannel + local signaling stub

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Checkbox steps.

**Goal:** A second `ITransport` implementation, `WebRtcTransport`, that carries the existing frame protocol over DTLS-encrypted WebRTC data channels, with a **local WebSocket signaling stub** for LAN sessions (the future cloud coordinator, JEF-25, replaces the stub's rendezvous). RakNet stays the default; transport is runtime-selectable.

**Architecture:** Star topology preserved — host owns one `rtc::PeerConnection` + one reliable-ordered `rtc::DataChannel` per client, exactly mirroring the RakNet hub. libdatachannel delivers on background threads; all callbacks push `TransportEvent`s into a mutex-guarded queue that the existing 4 ms `poll()` drains (manager stays single-threaded). PeerIds are synthetic monotonic tokens (opaque app-side, verified in JEF-22). Frames are sent **without** the RakNet envelope byte (developer_notes §32). Signaling: libdatachannel's `rtc::WebSocketServer`/`rtc::WebSocket` exchange JSON `{type: offer|answer|candidate, ...}`; the host's `startHost(port,...)` runs the signaling WebSocket on `port`, the client's `connect(ip,port,...)` dials it.

**Tech Stack:** libdatachannel v0.22.0 (MIT) via CMake FetchContent, OpenSSL (Homebrew `openssl@3`), C++20. Gates: build, `--wire-test` (regression), `--remote-test` (RakNet regression), new `--remote-test-webrtc` (two-process LAN WebRTC session). All bare, never piped.

## Global Constraints

- Branch `JEF-24-webrtc-transport` from **`feature/remote-webrtc`**; merges back into it, NEVER `qt-experimental`.
- libdatachannel pinned `v0.22.0`; FetchContent needs `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (old `plog` submodule vs CMake 4.x). Set it inside our CMake so callers don't need to. `OPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3` on macOS (guard platform-conditionally).
- Gated behind CMake option `JEFECHECK_WEBRTC` (default ON). When OFF, `WebRtcTransport` compiles to a stub whose `startHost`/`connect` return false and log "built without WebRTC"; the factory then falls back to RakNet. Networking TUs still must not include Qt/GL headers (developer_notes §1).
- `ITransport` contract is fixed (src/gfcTransport.h): `startHost/stopHost/connect/disconnect/poll/send/closePeer/connectionCount`, `TransportEvent{type,peer,bytes}`, `TransportEventType` (10 values). WebRtcTransport must satisfy it with ZERO change to the interface, `gfcNetworkClient`, `gfcNetworkServer`, `gfcNetworkManager`, or the wire format.
- Transport selection: env `JEFECHECK_TRANSPORT` (`raknet` default | `webrtc`), read once by a new factory `jefe::net::makeTransport()`; both client (src/gfcnetworkclient.cpp:43) and server (src/gfcnetworkserver.cpp:66) call the factory instead of `make_unique<RakNetTransport>`.
- Do NOT add the envelope byte in WebRtcTransport; `send` transmits frame bytes verbatim, `poll` delivers received bytes verbatim as `Data`.
- **libdatachannel lifecycle (spike-verified gotcha):** the process **segfaults at exit** if a `PeerConnection` is created without `rtc::Preload()` at startup and `rtc::Cleanup()` at shutdown. `WebRtcTransport` must call `rtc::Preload()` in its ctor (idempotent; refcounted) and `rtc::Cleanup()` in its dtor, and `rtc::InitLogger(rtc::LogLevel::Error)` once to quiet logging. Default `rtc::Configuration` works; `pc->createDataChannel("jefe")` returns state=1 (connecting) as expected. Confirmed building on this macOS host with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.
- Commits end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: libdatachannel CMake integration + transport factory (RakNet default, zero regression)

**Files:** Modify `CMakeLists.txt`; create `src/gfcTransportFactory.h`, `src/gfcTransportFactory.cpp`; modify `src/gfcnetworkclient.cpp:43`, `src/gfcnetworkserver.cpp:66`.

**Produces:** `jefe::net::TransportKind{ RakNet, WebRtc }`; `std::unique_ptr<jefe::net::ITransport> jefe::net::makeTransport(TransportKind kind = fromEnv())`; `TransportKind jefe::net::transportKindFromEnv()` (reads `JEFECHECK_TRANSPORT`, default RakNet). WebRtc kind resolves to RakNet in this task (WebRtcTransport lands Task 3-4) with a `log` note — keeps the build green and lets the factory land first.

**Steps:**
- [ ] Add to `CMakeLists.txt`: `option(JEFECHECK_WEBRTC "Build WebRTC transport (libdatachannel)" ON)`. When ON: `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)` before the FetchContent block; `if(APPLE) set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3) endif()`; `include(FetchContent)`; declare libdatachannel `GIT_TAG v0.22.0 GIT_SHALLOW TRUE` with `NO_EXAMPLES/NO_TESTS/NO_MEDIA=ON`; `FetchContent_MakeAvailable`; `target_link_libraries(jefecheck PRIVATE LibDataChannel::LibDataChannel)`; `target_compile_definitions(jefecheck PRIVATE JEFECHECK_WEBRTC=1)`.
- [ ] Create the factory. `makeTransport(RakNet)` → `make_unique<RakNetTransport>()`. `makeTransport(WebRtc)` → `#if JEFECHECK_WEBRTC` return `make_unique<WebRtcTransport>()` (declare it now; Task 3 defines it — to keep Task 1 self-contained, gate the WebRtc branch to fall back to RakNet with a `// Task 3` TODO and log, OR stub WebRtcTransport minimally; choose the option that builds). `#else` fall back to RakNet + log.
- [ ] Point client/server at `jefe::net::makeTransport()` (default from env).
- [ ] Gates: `cmake -B build_qt -DCMAKE_POLICY_VERSION_MINIMUM=3.5` (first configure downloads+builds libdatachannel — several minutes), `cmake --build build_qt -j8`; `--remote-test` green (still RakNet by default); `--wire-test` 106/0. Commit.

### Task 2: Local signaling stub (WebSocket JSON offer/answer/candidate)

**Files:** Create `src/gfcSignaling.h`, `src/gfcSignaling.cpp`.

**Produces:** `jefe::net::SignalingServer` (wraps `rtc::WebSocketServer` on a port; `onClientConnected(fn(clientId, sendFn))`, `onMessage(fn(clientId, json))`, `broadcast/sendTo`) and `jefe::net::SignalingClient` (wraps `rtc::WebSocket` to `ws://ip:port`; `onOpen/onMessage`, `send(json)`). Message schema: `{ "type": "offer"|"answer"|"candidate"|"hello", "sdp"/"candidate"/"mid": ... , "peer": <id> }`. Use a minimal hand-rolled JSON (or nlohmann/json if libdatachannel already vends it — check `rtc` deps; prefer a tiny local encoder to avoid a new dep). Thread-safe: WebSocket callbacks fire on libdatachannel threads.

**Steps:**
- [ ] Implement both wrappers; document the message schema in the header.
- [ ] Loopback test behind `--signal-test`: start a `SignalingServer` on an ephemeral port, connect a `SignalingClient`, round-trip a `hello`+`offer` JSON, assert receipt both directions, print `SIGNAL-TEST: pass=N fail=0`. Gates: build, `--signal-test` pass, `--remote-test`/`--wire-test` still green. Commit.

### Task 3: WebRtcTransport — host role

**Files:** Create `src/gfcWebRtcTransport.h`, `src/gfcWebRtcTransport.cpp`; wire the real type into the factory (`src/gfcTransportFactory.cpp`).

**Produces:** `class jefe::net::WebRtcTransport : public ITransport`. Host: `startHost(port,password,maxClients)` starts a `SignalingServer` on `port`; per signaling client, create `rtc::PeerConnection` (STUN server list empty for LAN; host candidates suffice), set `onLocalDescription/onLocalCandidate` to forward via signaling, `onDataChannel` to capture the reliable channel, assign a fresh `PeerId` (monotonic counter from 1), push `PeerConnected`; `onMessage` on the channel pushes `Data{peer,bytes}`; channel close pushes `PeerLost`. Event queue: `std::mutex` + `std::deque<TransportEvent>`; `poll()` pops front. `connectionCount()` = live channel count.

**Steps:**
- [ ] Implement host role + the thread-safe event queue. `send(data,len,target,broadcastExcluding)`: target==peer → that channel; broadcastExcluding true → all channels except target (target==kInvalidPeerId → all). `closePeer` closes the pc/channel. No envelope byte.
- [ ] Gates: build; `--remote-test`/`--wire-test`/`--signal-test` green (host-only compiles, RakNet still default). Commit.

### Task 4: WebRtcTransport — client role + full send/poll

**Files:** Modify `src/gfcWebRtcTransport.cpp/.h`.

**Steps:**
- [ ] Client: `connect(ip,port,password)` starts a `SignalingClient` to `ws://ip:port`, creates a `rtc::PeerConnection`, creates the reliable-ordered `DataChannel` ("jefe"), forwards local SDP/candidates via signaling, applies remote SDP/candidates, on channel open pushes `ConnectAccepted` (peer = host's synthetic id, fixed e.g. 1), on message pushes `Data`, on close pushes `ConnectionLost`/`Disconnected`. `disconnect()` tears down.
- [ ] Reliability config: `rtc::DataChannelInit` reliable + ordered (matches RakNet RELIABLE_ORDERED). Guard `poll()`/`send()` against partial-connection races (channel not open yet → send drops or queues; match RakNet which silently drops pre-connect sends).
- [ ] Gates: build; existing tests green. Commit. (Full E2E proven in Task 5.)

### Task 5: `--remote-test-webrtc` harness, transport wiring, docs, merge

**Files:** Modify `src/main_qt.cpp` (+ its test bridge in `src/qt/SequenceLoadBridge_qt.cpp`), `developer_notes.md` (§33).

**Steps:**
- [ ] Add `--remote-test-webrtc` (and `--remote-test-webrtc-peer <ip> <port>`): identical to the RakNet `--remote-test` two-process harness but sets `JEFECHECK_TRANSPORT=webrtc` (or passes the kind explicitly to the bridge) so host+peer use WebRtcTransport over the localhost signaling stub. Assert `participants>=1 && mirrored_play==1`, exit 0/2. Reuse the existing harness scaffolding.
- [ ] Run it (bare): host establishes a WebRTC session with the spawned peer, sync chain completes, play mirrors. If flaky on ICE timing, add a bounded settle/retry (document it) — do NOT weaken the assertion.
- [ ] Confirm DTLS: note in the report that data flows over an encrypted SCTP/DTLS channel (structural — libdatachannel mandates DTLS; a packet capture is optional evidence).
- [ ] Gates: build; `--remote-test` (RakNet), `--remote-test-webrtc` (WebRTC), `--wire-test`, `--signal-test` all green; `--cc-test /Users/dgollas/projects/openexr-images/ScanLines/Desk.exr`, `--playlist-test` same image.
- [ ] `developer_notes.md` §33 "WebRTC transport (JEF-24)": factory + `JEFECHECK_TRANSPORT`, star topology over data channels, callback→queue→poll threading, synthetic PeerIds, no-envelope rule, signaling stub schema (and that JEF-25 replaces the WebSocket rendezvous with the cloud coordinator), CMake option + policy flag + OpenSSL, JEF-24 hardening TODOs carried from JEF-23 review (path traversal etc. still open — restate they block internet exposure).
- [ ] Commit; final whole-branch review; merge `--no-ff` into `feature/remote-webrtc`; push; rebuild the integration binary + rerun gates post-merge.

## Self-review notes (plan time)

- Front-loads the dependency risk exactly as the migration plan demands (Task 1 gets libdatachannel building before any transport code). A spike already proved v0.22.0 builds on this macOS host with the policy flag.
- CI (Ubuntu/MinGW) legs are written but not runnable here; the report must flag them as unverified so the reviewer/controller tracks them.
- The signaling stub is deliberately minimal and LAN-only; NAT traversal / STUN / TURN is JEF-26, cloud rendezvous is JEF-25 — this ticket stops at "works on a LAN via a local WebSocket."
- Synthetic PeerIds are safe because JEF-22's review confirmed PeerId is opaque everywhere above the transport; packPeerId's ip<<16|port scheme is RakNet-specific and not required here.
