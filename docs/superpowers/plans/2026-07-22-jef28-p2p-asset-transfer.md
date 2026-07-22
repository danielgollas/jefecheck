# JEF-28: P2P Asset Transfer (harden + verify the late-join LUT/FX sync)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Checkbox steps.

**Goal:** Make peer-to-peer LUT/FX asset sync actually reliable and safe: verify the existing late-join transfer works with real assets, give it a true content hash, chunk large files with backpressure, move bulk transfer to a dedicated data channel so it can't stall live state, and close the path-traversal security gap. This is the "fixes the late-join LUT/FX gap" ticket — but the transfer machinery already exists (see below), so the work is hardening + verification, not a rewrite.

**What already exists (from the JEF-28 code map — do NOT rebuild it):** `serializeFX/unserializeFX/serializeLUT/unserializeLUT` (`gfcNetworkStructures.cpp`) carry full `.jfx`/`.vert`/`.frag`/`.cube` file bodies over `jefe::wire`. The join handshake (`GFCNETID_REQUESTFXHASHES → LOADEDFXSHASHES → REQUESTFXS → REQUESTEDFXS → MISSINGFXS`, LUT equivalents, in `gfcnetworkserver.cpp`/`gfcnetworkclient.cpp`) already computes the joiner's missing set and pushes the host's loaded LUT/FX to the joiner, which writes them to `sett.receivedPath` and hot-loads via `loadLUT`/`loadFX`. It is transport-agnostic (runs over RakNet/LAN-WebRTC/coordinator identically). **The gaps are real, though:** (1) never verified with a non-empty asset (the only checked-in `--remote-test` run had 0 assets); (2) one-shot, unchunked, no `bufferedAmount` backpressure — a multi-MB LUT risks the SCTP message-size limit and floods the single `"jefe"` channel; (3) the "content hash" is `std::hash` over parsed values for LUT and **metadata-only** for FX (shader source not hashed → false dedup); (4) `sett.receivedPath` is declared but never assigned (received files land in CWD); (5) path traversal — a peer-supplied filename is used to write a file (the carried §32/§33 security TODO).

**Architecture:** Keep the existing sync state machine. (T1) Replace the hash with a portable content digest over the actual file bytes (LUT + FX incl. shader source); assign `receivedPath`; sanitize received filenames (basename only). (T2) A `--asset-test` two-process harness proves a late joiner receives + hot-loads a real host LUT and FX. (T3) A dedicated reliable `"assets"` data channel per peer (separate from `"jefe"` state), with `ITransport` gaining a channel selector; the FX/LUT bulk bodies route over `"assets"`, state/chat/pointer stay on `"jefe"`. (T4) Chunking + `bufferedAmount` backpressure on the assets channel; reassembly on receive; large-fixture verification. (T5) docs §35 + merge.

**Tech Stack:** C++20, libdatachannel (`rtc::DataChannel::bufferedAmount`/`onBufferedAmountLow`), `jefe::wire`. Gates bare, never piped.

## Global Constraints

- Branch `JEF-28-p2p-asset-transfer` from **`feature/remote-webrtc`**; merges back into it, NEVER `qt-experimental`.
- Do NOT rebuild the existing sync state machine or the serialize/unserialize body-carrying — harden it. Preserve the handshake ordering + the documented "requested-count loop is normal" quirk (§32).
- Zero regression across all three transport modes (RakNet default, LAN-webrtc, coordinator) + all gates (`--remote-test`, `--remote-test-webrtc`, `--coord-test`, `--wire-test`, `--signal-test`, `--coord-signal-test`, `--cc-test`, `--fx-test`, `--playlist-test`).
- Content hash MUST be a real content digest (over file bytes), deterministic and portable across builds/platforms (the current `std::hash` is build-dependent). Changing the hash is a wire-affecting behavior change for the sync dedup — it's intended; document it. The `GFCNETID_*` enum stays anchored at `GFCNET_USER_PACKET_BASE=91` (append new IDs at the end only).
- SECURITY: received filenames must be reduced to their basename (no `/`, `\`, `..`) before writing under `receivedPath` — this closes the tracked path-traversal TODO. Also range-check nothing else regresses.
- `ITransport::send` channel selector must be additive/defaulted so RakNet + existing call sites are unaffected (RakNet maps the asset channel to a second reliable channel or the same channel — document; WebRTC maps to the two data channels).
- macOS bundle-binary gates, bare, never piped. Commits end `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: Real content hash + receivedPath + path-traversal fix

**Files:** `src/gfcStructures.cpp` (GetMD5Hash or a new content-digest fn), `src/trilerp.cpp` (LUT md5Hash source), `src/gfcfx.cpp` (FX md5Hash source), `src/gfcNetworkStructures.cpp` (unserialize write path + sanitize), `src/qt/qt_prefs_persist` or startup (assign `sett.receivedPath`), test in `--wire-test`.

**Steps:**
- [ ] Content digest: make the LUT hash cover the raw `.cube`/1D file bytes (read the file, digest the bytes) and the FX hash cover the `.jfx` + `.vert` + `.frag` bytes (uncomment/re-add the shader-source contribution — `gfcfx.cpp:484,592`). Use a deterministic, portable digest (a real hash — e.g. a vendored small SHA-1/FNV-1a over bytes; NOT `std::hash`). Keep `GetMD5Hash(std::string)` callers working or migrate them. Two peers on different builds must compute the SAME hash for the same file bytes.
- [ ] Assign `sett.receivedPath` at startup to a real per-user dir (e.g. under `getApplicationDataPath()/received` or a temp/received subfolder); create it if missing. Document where.
- [ ] Sanitize: in `unserializeFX`/`unserializeLUT`, reduce the peer-supplied filename to its basename before `receivedPath + name` (strip any path separators / `..`). Reject/skip an empty result.
- [ ] `--wire-test` additions: a golden content-digest vector (same bytes → same hash literal), an FX whose shader body differs but metadata matches now hashes DIFFERENTLY (proves the shader-source fix), and a path-traversal input (`../../evil`) is reduced to `evil`.
- [ ] Build; `--wire-test` green; `--remote-test`, `--cc-test`, `--fx-test` green. Commit.

### Task 2: `--asset-test` — verify late-join transfer of a real LUT + FX

**Files:** `src/main_qt.cpp` (+ bridge in `src/qt/SequenceLoadBridge_qt.cpp`), a fixture LUT/FX (reuse `src/FX/` shaders + a small `.cube` fixture; create a tiny identity `.cube` if none exists).

**Steps:**
- [ ] `--asset-test` (orchestrator) + `--asset-test-peer` reusing the `--remote-test-webrtc` re-exec/split-phase pattern (or RakNet — pick the simplest reliable transport for the gate; WebRTC preferred since it's the future). BEFORE the peer joins: the host `loadLUT(<fixture.cube>)` and `loadFX(<fixture.jfx>)` (via the same manager calls the UI uses). Spawn the peer; after the sync completes, assert the PEER's `lutManager`/`fxManager` now contain the fixture's hash (add TU-safe bridge getters `remoteAssetHasLUT(hash)` / counts). Print `ASSET-TEST: lut=<0|1> fx=<0|1>`, exit 0 iff both received.
- [ ] Run it (bare); it MUST show the joiner received both. If it reveals the existing transfer is actually broken (e.g. empty-asset-only path, the receivedPath/CWD issue, a hot-load failure), FIX the transfer here (that's the point of the ticket) and document what was broken. Bounded timeouts; deterministic; reap children.
- [ ] Gates: build; `--asset-test` (lut=1 fx=1 exit 0); all existing gates green. Commit.

### Task 3: Dedicated `"assets"` data channel (separate from state)

**Files:** `src/gfcTransport.h` (channel selector), `src/gfcWebRtcTransport.{h,cpp}` (second channel), `src/gfcRakNetTransport.cpp` (map channel), `src/gfcnetworkserver.cpp`/`gfcnetworkclient.cpp` (send asset bodies on the assets channel), `src/gfcTestCoordinator`/harness if needed.

**Steps:**
- [ ] Add a channel selector to `ITransport::send` (e.g. `enum class Channel { State, Assets };` defaulted to State) and to the `Data` event (which channel it arrived on) — additive, existing call sites default to State (zero behavior change). RakNet: map Assets → a second reliable-ordered channel id (or the same; document). WebRTC: second `rtc::DataChannel("assets", reliable+ordered)` per peer (mirror the `"jefe"` creation at both offerer sites + an `onDataChannel` branch keyed by `dc->label()`), routed in send/poll.
- [ ] Route the FX/LUT bulk serialize sends (`GFCNETID_MISSINGFXS/MISSINGLUTS/REQUESTEDFXS/REQUESTEDLUTS`) over Channel::Assets; keep chat/pointer/play/CC/state on Channel::State. Receiving side dispatches by the same GFCNETID regardless of channel (the channel is a QoS lane, not a new protocol).
- [ ] Gates: build; `--asset-test` still lut=1 fx=1 (now over the assets channel); `--remote-test-webrtc`, `--coord-test`, `--remote-test` (RakNet), all state gates green — proving state traffic is unaffected. Commit.

### Task 4: Chunking + backpressure for large assets

**Files:** `src/gfcWebRtcTransport.cpp` (bufferedAmount + chunking), possibly a chunk/reassembly layer in the sync send path, `--asset-test` large fixture.

**Steps:**
- [ ] Chunking: if an asset frame exceeds a safe SCTP message size (e.g. > 200 KB), split into ordered chunks on the assets channel and reassemble on receive (a small chunk header: assetMsgId, index, count/total, bytes — new `GFCNETID_ASSETCHUNK` or a transport-level split; choose transport-level so the sync layer stays oblivious, OR a wire-level chunk message — document). Backpressure: before sending each chunk, if `dc->bufferedAmount()` exceeds a high-water mark, wait for `onBufferedAmountLow` (or defer via the event loop) rather than blindly queuing — so a big transfer doesn't balloon memory or starve the socket.
- [ ] Large-fixture `--asset-test` variant: a multi-MB LUT (generate a large `.cube` at test time, or a big FX) transfers correctly and the receiver's hash matches — proving chunking + reassembly + backpressure. Assert byte-integrity via the content hash.
- [ ] Gates: build; `--asset-test` (small + large) lut=1 fx=1; all existing gates green; confirm state channel stays responsive during a large transfer (a qualitative note or a timing assertion). Commit.

### Task 5: docs §35 + merge

**Steps:**
- [ ] `developer_notes.md` §35 "P2P asset transfer (JEF-28)": the existing sync state machine (reference §31/§32), the new content digest (portable, over file bytes, covers shader source) + why the hash changed, `receivedPath` location + the basename sanitization (closes the path-traversal TODO — update §32/§33 to mark it resolved), the dedicated `"assets"` channel + the `ITransport` channel selector, chunking + backpressure design + thresholds, the `--asset-test` harness (+ large variant). Note remaining items (e.g. media-file transfer beyond LUT/FX is future; resume-after-drop if not implemented).
- [ ] Full gate sweep. Commit.
- [ ] Final whole-branch review; merge `--no-ff` into `feature/remote-webrtc`; rebuild + rerun `--asset-test`/`--remote-test`/`--remote-test-webrtc`/`--coord-test` post-merge.

## Self-review notes (plan time)

- The ticket's headline ("fixes late-join LUT/FX gap") is really "verify + harden" — T2 proves the existing push works (or fixes it if it doesn't), which is the honest deliverable. The code map shows the mechanism exists but was never exercised with a real asset.
- T1 closes three genuine latent bugs (build-dependent hash, FX-ignores-shader-source, receivedPath=CWD) + the tracked path-traversal security TODO — high value independent of the channel/chunking work.
- T3's `ITransport` channel selector is the one cross-transport change; kept additive/defaulted so RakNet + all existing sends are untouched (State by default). If the RakNet second-channel mapping is fiddly, mapping Assets→same channel on RakNet is acceptable (RakNet is legacy) — document.
- T4 chunking done at the transport level keeps the sync layer oblivious; backpressure via bufferedAmount is the libdatachannel-idiomatic approach.
- Everything is locally verifiable (two-process --asset-test with real fixtures); no cloud/AWS needed.
