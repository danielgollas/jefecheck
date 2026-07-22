# JEF-27: Client Coordinator UX (create/join cloud session by code)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Checkbox steps.

**Goal:** Let a JefeCheck user **create a cloud session** (get a short code) or **join by code** through the cloud coordinator (JEF-25 protocol), establishing a DTLS-encrypted WebRTC session without either peer needing a public IP — surfaced in the Remote dialog. Consume the coordinator-supplied TURN `iceServers` (JEF-26) into the WebRTC config. Verified locally against a coordinator; live cloud deploy is JEF-25/26's operator step.

**Architecture:** A new `CoordinatorSignaling` C++ client speaks the **JEF-25 coordinator protocol** over `rtc::WebSocket` (WSS): host sends `{action:"create-session"}` → gets `{type:"session-created", code, token, iceServers?}`; joiner sends `{action:"join-session", code}` → gets `{type:"roster", peers, iceServers?}` + `{type:"peer-joined"}`/`{type:"peer-left"}`; SDP/ICE ride inside `{action:"signal", to, payload}` ⇄ `{type:"signal", from, payload}` where `payload` is the existing JEF-24 `SignalMessage`. `WebRtcTransport` gains a **coordinator mode**: both host and joiner connect to the coordinator (no local `SignalingServer`); the coordinator relays the handshake; peers still form the same P2P star. Coordinator config (URL, role, session code, password) is plumbed via a widened `makeTransport` config + new `gfcServerParams`/`gfcConnectionParams` fields. The `iceServers` from the coordinator feed `rtc::Configuration.iceServers` (the JEF-24 TODO insertion points). The Remote dialog gets a **Cloud** mode: "Create session" shows the code; "Join by code" takes a code.

**Tech Stack:** C++20, libdatachannel (`rtc::WebSocket`), the existing hand-rolled JSON in gfcSignaling. Test: a self-contained C++ **test-double coordinator** (a `rtc::WebSocketServer` implementing the JEF-25 message contract, field-matched to the coordinator repo's `docs/coordinator-protocol.md`) driving a two-process `--coord-test`; optionally the real Node coordinator via `JEFECHECK_COORDINATOR_WS`. Gates bare, never piped.

## Global Constraints

- Branch `JEF-27-client-coordinator-ux` from **`feature/remote-webrtc`**; merges back into it, NEVER `qt-experimental`.
- The coordinator protocol is the CONTRACT: the C++ client's emitted/consumed messages MUST match the coordinator repo's schema verbatim (actions `create-session`/`join-session`/`signal`/`leave`; server types `session-created`/`peer-joined`/`peer-left`/`roster`/`signal`/`error`; the SDP/ICE `SignalMessage` travels as the `signal` `payload`). Cross-check field names against `~/projects/jefecheck-coordinator` `docs/coordinator-protocol.md` + `src/protocol.ts` (the canonical source). Do NOT invent fields.
- Zero regression: RakNet default, JEF-24 LAN WebRTC (`--remote-test`, `--remote-test-webrtc`), `--wire-test`, `--signal-test` all stay green. Coordinator mode is additive (selected by config/env).
- iceServers from the coordinator are fed into `rtc::Configuration` at BOTH the host and joiner PeerConnection build sites (gfcWebRtcTransport.cpp ~:170, ~:331). Empty list ⇒ LAN behavior unchanged.
- Password: enforce it in the coordinator handshake where the protocol supports it (the coordinator gates by code/token in phase-1; if the protocol has no password field, document that the code IS the gate and note the residual — do not fake enforcement).
- macOS bundle-binary gates, bare, never piped. Commits end `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: `CoordinatorSignaling` client (JEF-25 protocol) + message codec + unit test

**Files:** Create `src/gfcCoordinatorSignaling.h`, `src/gfcCoordinatorSignaling.cpp`; extend the `--signal-test` area or add `--coord-signal-test` in `src/main_qt.cpp`.

**Produces:** `jefe::net::CoordinatorSignaling` wrapping `rtc::WebSocket` to a coordinator `wss://`/`ws://` URL. API: `connect(url)`, callbacks `onSessionCreated(fn(code, token, iceServersJson))`, `onPeerJoined(fn(peerId))`, `onPeerLeft(fn(peerId))`, `onRoster(fn(peers[], iceServersJson))`, `onSignal(fn(fromPeerId, SignalMessage))`, `onError(fn(code, msg))`; senders `createSession()`, `joinSession(code)`, `sendSignal(toPeerId, const SignalMessage&)`, `leave()`. A codec (`encodeClientMsg`/`parseServerMsg`) for the JEF-25 envelope, reusing/extending gfcSignaling's flat-JSON helpers; the `SignalMessage` payload nests inside the `signal` message.

**Steps:**
- [ ] Read `~/projects/jefecheck-coordinator/docs/coordinator-protocol.md` + `src/protocol.ts` FIRST; pin the exact action/type strings + field names in a header comment as the contract.
- [ ] Implement CoordinatorSignaling + the envelope codec (nested payload for signal). Thread-safe (callbacks on libdatachannel threads).
- [ ] `--coord-signal-test`: encode each client message + parse each server message with representative values (a session-created with iceServers, a signal carrying an SDP with `\r\n`, an error), assert round-trip/field extraction; print `COORD-SIGNAL-TEST: pass=N fail=0`, exit 0/2. (No network — pure codec + a loopback against a tiny in-test rtc::WebSocketServer echoing the contract if feasible; keep it deterministic and hang-free.)
- [ ] Build; `--coord-signal-test` pass; `--signal-test`/`--wire-test`/`--remote-test` green. Commit.

### Task 2: Transport config plumbing + WebRtcTransport coordinator mode

**Files:** `src/gfcTransportFactory.{h,cpp}` (widen), `src/gfcWebRtcTransport.{h,cpp}`, `src/gfcNetworkStructures.h` (gfcServerParams/gfcConnectionParams fields), `src/gfcnetworkclient.cpp`/`gfcnetworkserver.cpp` (pass config), `src/qt/SequenceLoadBridge_qt.*` (params).

**Produces:** A `TransportConfig { TransportKind kind; std::string coordinatorUrl; bool coordinatorMode; std::string sessionCode; /* host: empty */ std::string password; }` passed to `makeTransport(TransportConfig)` (keep the old `makeTransport(kind)` working). `gfcServerParams`/`gfcConnectionParams` gain `coordinatorUrl`, `sessionCode`, `coordinatorMode`. WebRtcTransport coordinator mode: `startHost` in coordinator mode connects a `CoordinatorSignaling`, calls `createSession()`, exposes the assigned code (a getter, e.g. via a transport→manager→bridge path or an event); each `peer-joined` → new answerer `rtc::PeerConnection` (host is answerer; joiner offers) with iceServers; relays SDP/ICE via `sendSignal`. `connect` in coordinator mode connects `CoordinatorSignaling`, `joinSession(code)`, on roster/peer-joined builds the offerer PeerConnection with iceServers, relays via `sendSignal`. Reuse the existing PeerConnection/DataChannel/event-queue machinery; only the signaling backend + config differ.

**Steps:**
- [ ] Widen the factory + params; keep existing call sites compiling (default config = current behavior).
- [ ] Implement coordinator-mode host + joiner in WebRtcTransport, feeding coordinator iceServers into rtc::Configuration at both PC build sites. Map coordinator peerIds ↔ transport PeerIds. Expose the session code (getter/event).
- [ ] Guard the connect/disconnect races as in JEF-24 (reuse the clientActive-style guard). No envelope byte (frames unchanged).
- [ ] Build; existing gates green (RakNet + LAN webrtc unaffected — coordinator mode is off by default). Commit. (Full E2E in Task 3.)

### Task 3: `--coord-test` two-process E2E (test-double coordinator) + iceServers proof

**Files:** `src/main_qt.cpp` (+ bridge), a test-double coordinator (in `src/qt/SequenceLoadBridge_qt.cpp` test area or a small `src/gfcTestCoordinator.{h,cpp}` used only by the harness).

**Produces:** A minimal C++ **test-double coordinator** = a `rtc::WebSocketServer` implementing the JEF-25 contract (assign code on create-session; on join-session by code, wire the two conns; relay `signal` payloads between them; emit peer-joined/roster/peer-left; optionally attach a dummy iceServers list). `--coord-test` (orchestrator) starts the test coordinator on an ephemeral port, runs the host process (create-session → code), spawns the peer (`--coord-test-peer <coordUrl> <code>`) that joins by code; both establish WebRTC through the coordinator and mirror play. Assert participants>=1 && mirrored_play==1.

**Steps:**
- [ ] Implement the test-double coordinator; field-match its messages to the coordinator repo's protocol (the reviewer will cross-check).
- [ ] Wire `--coord-test`/`--coord-test-peer` reusing the re-exec + split-phase host pattern from `--remote-test-webrtc`, swapping ip/port for coordinatorUrl + session code.
- [ ] Run it (bare): host creates cloud session, peer joins by code, WebRTC session mirrors play. Add an iceServers assertion: the test coordinator returns a (dummy STUN) iceServers list and the transport must apply it without breaking the loopback session (prove the plumbing; a real TURN relay isn't exercised locally). Bounded timeouts; deterministic.
- [ ] Gates: build; `--coord-test` (participants>=1 mirrored_play=1 exit 0); `--remote-test`, `--remote-test-webrtc`, `--wire-test`, `--signal-test`, `--coord-signal-test`, `--cc-test <Desk.exr>`, `--playlist-test` all green. Commit.

### Task 4: Qt Remote dialog — Cloud mode (create/join by code, show code, reconnect)

**Files:** `src/qt/RemotePanel_qt.{h,cpp}` (class `RemoteDialog_Qt`), `src/qt/SequenceLoadBridge_qt.{h,cpp}` (new bridge entry points + params).

**Produces:** A third **Cloud** segment (alongside Host/Join): "Create session" (+ optional coordinator URL field defaulting to a setting/env; on create, display the assigned **session code** prominently with a copy affordance) and "Join by code" (a code field + coordinator URL). New bridge fns `connectAsCloudHost(RemoteCloudHostParams{coordinatorUrl,password})` / `connectAsCloudClient(RemoteCloudJoinParams{coordinatorUrl,sessionCode,password})` funnel into `gfcNetworkManager::startServer`/`startConnection` with coordinator-mode params. A getter surfaces the assigned code to the dialog (`jefe::qt::remoteSessionCode()`), and status/participants reuse the existing `refreshConnectionState()` path. Reconnect: on drop, offer reconnect (reuse session code) with bounded backoff (or a Reconnect button) — keep it simple, document behavior.

**Steps:**
- [ ] Add the Cloud UI segment + the code display + copy button; wire the two bridge entry points + the code getter. Object names follow the dotted-leaf locator scheme (tests/ui/jefecheck/locators.py).
- [ ] Build; a manual/scripted smoke (the dialog opens, Cloud mode toggles, fields validate). No new automated GUI gate required beyond build + the existing tests staying green. Commit.

### Task 5: Reconnect/backoff, docs §34, merge

**Files:** `developer_notes.md` (§34), possibly small transport reconnect logic.

**Steps:**
- [ ] Reconnect: if not already done in Task 4, add bounded WS reconnect/backoff to CoordinatorSignaling (coordinator drop mid-session should not kill an established P2P session — the coordinator is only needed for join/reconnect; document this exactly, matching the design spec §5).
- [ ] `developer_notes.md` §34 "Cloud coordinator client (JEF-27)": the coordinator protocol the client speaks (contract + link to the coordinator repo), coordinator mode vs LAN stub vs RakNet selection, config plumbing (TransportConfig + params fields), iceServers consumption (JEF-26), the session-code UX, reconnect behavior, the `--coord-test` test-double + the `JEFECHECK_COORDINATOR_WS` option for the real Node coordinator, and the still-open items (real cloud deploy = operator; password enforcement status).
- [ ] Full gate sweep (all from Task 3). Commit.
- [ ] Final whole-branch review; merge `--no-ff` into `feature/remote-webrtc`; rebuild integration binary + rerun `--remote-test`/`--remote-test-webrtc`/`--coord-test` post-merge.

## Self-review notes (plan time)

- The test-double coordinator is the reliable, self-contained gate; its risk (divergence from the canonical Node coordinator) is mitigated by field-matching against the coordinator repo's protocol doc/source and a reviewer cross-check, plus the `JEFECHECK_COORDINATOR_WS` escape hatch to run against the real thing.
- Coordinator mode reuses ALL the JEF-24 PeerConnection/DataChannel/event-queue/PeerId machinery — only the signaling backend (coordinator vs local stub) and the config plumbing are new, keeping the surface small and LAN/RakNet paths untouched.
- iceServers (JEF-26) consumption lands here because this is where the client obtains them (from the coordinator) — the coturn side is already built; this closes the client half.
- Config plumbing through gfcServerParams/gfcConnectionParams is the one invasive change; kept additive so RakNet + LAN WebRTC construction is unchanged.
- Reconnect keeps the P2P session alive when the coordinator drops (coordinator is rendezvous-only post-connection) — matches design spec §5 resilience.
