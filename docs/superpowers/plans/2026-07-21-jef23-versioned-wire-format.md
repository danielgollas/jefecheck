# JEF-23: Versioned Wire Format Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Replace `RakNet::BitStream`/`StringCompressor` at every message read/write site with a small, versioned, explicitly little-endian wire format (`jefe::wire`), so serialization no longer depends on RakNet and JEF-24 can carry the same frames over WebRTC.

**Architecture:** Frames are `[u8 version=1][u16 msgType LE][payload]`; `msgType` reuses the `GFCNETID_*` values. The app now hands *frames* to `ITransport`; `RakNetTransport` owns the RakNet envelope (prepends `GFCNET_USER_PACKET_BASE` (91) on send so RakNet routes the packet as user data, strips it in `poll()` before emitting `Data`). `jefe::wire::Writer/Reader` are bounds-checked, little-endian, with `u32`-length-prefixed strings (StringCompressor's Huffman coding and per-field max-length truncation are dropped — sanctioned wire break, version byte starts at 1). Struct-based messages get extracted `encode*/decode*` functions in one TU with round-trip tests behind a new `--wire-test` flag; ad-hoc handshake messages (hash lists, sync signals, peers list) are ported inline with Writer/Reader.

**Tech Stack:** C++20, CMake glob build. Gates: build, `--wire-test` (new), `--remote-test` (bare bundle binary: `./build_qt/jefecheck.app/Contents/MacOS/jefecheck --remote-test; echo exit=$?`), never piped.

## Global Constraints

- Branch `JEF-23-versioned-wire-format` from **`feature/remote-webrtc`** (worktree `/Users/dgollas/workspaces/JEF-23-versioned-wire-format/jefecheck2`). Merges back into `feature/remote-webrtc` — NEVER to `qt-experimental` (integration-branch policy, 2026-07-21).
- Wire-compat break is sanctioned; BEHAVIOR (message semantics, ordering, manager side effects, status strings, sync state machines) must not change. Handler bodies keep their exact logic; only serialization calls are substituted.
- `gfcNetworkManager.h` public API unchanged. The `GFCNETID_*` inner switches stay default-action-free.
- After this phase: `BitStream`/`StringCompressor` appear nowhere in `gfcnetworkclient.*`, `gfcnetworkserver.*`, `gfcNetworkManager.*`, `gfcNetworkStructures.*`. (`gfcRakNetTransport.cpp` may keep RakNet includes — it's the transport TU.)
- Reference inventories: JEF-22 plan + `developer_notes.md` §31. Message-site tables: client sends (22 live sites), server sends (~20 sites incl. the 12-label verbatim-forward group), handler read sequences per the JEF-22 inventory. The verbatim-forward group needs no serialization at all (bytes pass through).
- `ID_USER_PACKET_ENUM == GFCNET_USER_PACKET_BASE == 91` static_assert stays in `gfcRakNetTransport.cpp`.
- Commits end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: `jefe::wire` primitives + `--wire-test` harness

**Files:** Create `src/gfcWire.h` (header-only). Modify `src/main_qt.cpp` (+ wherever the other `--*-test` flags register, e.g. the CLI11 block) to add `--wire-test`.

**Produces:** `jefe::wire::Writer` (`writeU8/U16/U32/U64/I32/F32/F64/Bool/String(std::string)/Bytes`), `jefe::wire::Reader` (matching `readX(T&) -> bool` bounds-checked, `ok()`, `remaining()`), frame helpers `beginFrame(Writer&, uint16_t msgType)` (writes version 1 + msgType) and `readFrameHeader(Reader&, uint16_t& msgType) -> bool` (checks version==1). All little-endian explicit (byte-wise shifts, no memcpy-of-struct). `Writer::data()/size()`. Strings: `u32` length + raw bytes; `Reader::readString` rejects length > `remaining()`.

**Steps:**
- [ ] Write `src/gfcWire.h` with the API above (implementation inline; ~150 lines).
- [ ] Add `--wire-test` flag following the existing `--render-test` pattern in `main_qt.cpp`: runs `jefe::qt::wireSelfTest()` — put the test function in `src/qt/SequenceLoadBridge_qt.cpp` ONLY if other tests live there, otherwise a small free function in a new `src/gfcWireTest.cpp` called directly from `main_qt.cpp` (check how `--render-test` dispatches first and mirror it). Tests: round-trip every primitive incl. boundary values (0, max, negative, NaN-free floats), string with UTF-8 + empty, truncated-buffer reads return false without UB, version-mismatch frame rejected. Print `WIRE-TEST: pass=N fail=0`, exit 0/2.
- [ ] Build + run `--wire-test` (expect pass), commit.

### Task 2: Message codecs TU + FX/LUT/struct round-trips

**Files:** Create `src/gfcWireMessages.h`, `src/gfcWireMessages.cpp`. Modify `src/gfcNetworkStructures.h/.cpp` (port `serializeFX/unserializeFX/serializeLUT/unserializeLUT` from `RakNet::BitStream*` to `jefe::wire::Writer&/Reader&`). Extend `--wire-test`.

**Produces:** `jefe::wire::encodeX(Writer&, const T&)` / `decodeX(Reader&, T&) -> bool` for each struct-based message: `gfcNetPlayPauseInfo`, `gfcNetPointerInfo`, `gfcNetRemotePointerInfo`, `std::vector<gfcNetTransformationInfo>`, `std::vector<gfcNetPlateColorCorrectionInfo>`, `gfcNetOtherStatesInfo`, `gfcNetFXAddInfo`, `gfcNetFXCommonInfo`, `gfcNetFXAttribInfo`, `gfcNetFXStackMessage`, layer-change (quadID+string), chat entry (type,time,sender,message,color), playlist string/item/event. Field order and types must mirror the legacy BitStream sequences exactly as documented by reading each legacy handler pair (send + receive) — the legacy read/write sequences are the spec; enumerate them from `gfcnetworkclient.cpp`/`gfcnetworkserver.cpp` before coding.

**Steps:**
- [ ] For each message: read the legacy write site AND read site, write the codec pair, add a `--wire-test` round-trip with representative values (non-empty vectors, non-ASCII strings, negative floats).
- [ ] Port the four FX/LUT serialize helpers in `gfcNetworkStructures.cpp` to Writer/Reader (same field order); round-trip them in `--wire-test` too (construct minimal `gfcFX`/LUT fixtures if feasible; if constructing fixtures requires GL context, cover via byte-level golden test instead and note it).
- [ ] Build + `--wire-test` green (primitives + all codecs), commit. (`--remote-test` still green — nothing live changed yet.)

### Task 3: RakNet envelope + port `gfcNetworkClient`

**Files:** Modify `src/gfcRakNetTransport.cpp` (envelope), `src/gfcnetworkclient.cpp` (+`.h` only if includes change).

**Steps:**
- [ ] Envelope: `RakNetTransport::send` prepends one byte `GFCNET_USER_PACKET_BASE` before the frame (single allocation or scratch buffer); `poll()` Data case strips it (`ev.bytes.assign(p->data+1, p->data+p->length)`). System-id mapping unchanged. Document in the header comment: "ITransport carries frames; the RakNet envelope byte is this TU's concern."
- [ ] Client Send* methods: replace BitStream construction with `Writer w; beginFrame(w, GFCNETID_X); encodeX(w, info); transport_->send(w.data(), (int)w.size(), serverPeerId_, false);` Ad-hoc payloads (hash lists: u32 count + strings; nickname send: string+color; playlist XML strings) written inline with Writer.
- [ ] Client pump Data case: `Reader r(ev.bytes.data(), ev.bytes.size()); uint16_t msgType; if (!readFrameHeader(r, msgType)) break; switch (msgType) { ... }` — every case's reads become Reader/decodeX calls; ALL side effects, `setTakeNotifications` pairings, status strings, sinc flags byte-identical. No default action added.
- [ ] Remove BitStream/StringCompressor includes from client files. Build green; `--wire-test` green. **`--remote-test` is EXPECTED RED** (server still legacy) — record the failure output in the report; do not chase it. Commit.

### Task 4: Port `gfcNetworkServer` (+ manager sweep)

**Files:** Modify `src/gfcnetworkserver.cpp` (+`.h` includes), `src/gfcNetworkManager.*` (only if stray includes remain).

**Steps:**
- [ ] Same substitutions as Task 3 for all server sends and the pump. The 12-label verbatim-forward group forwards `ev.bytes` untouched (already frame bytes — confirm no header re-read needed). Targeted/broadcast semantics preserved exactly (`ev.peer`/`kInvalidPeerId` per §31).
- [ ] Server-composed messages (system chat, PEERSINSESSION, sync requests): Writer + beginFrame with the same GFCNETID values.
- [ ] Remove BitStream/StringCompressor includes from server + manager + structures headers (structures' serialize helpers now take Writer/Reader — done in Task 2; delete any leftover BitStream declarations).
- [ ] Build + `--wire-test` + **`--remote-test` GREEN** (full sync chain over the new format) + `--cc-test /Users/dgollas/projects/openexr-images/ScanLines/Desk.exr` + `--playlist-test` (same image). Commit.

### Task 5: Audit, docs, gates

**Steps:**
- [ ] Grep audit: `grep -rn "BitStream\|StringCompressor" src/gfcnetworkclient.* src/gfcnetworkserver.* src/gfcNetworkManager.* src/gfcNetworkStructures.*` → zero hits. `grep -rn "IgnoreBits\|ReadCompressed\|WriteCompressed"` same files → zero.
- [ ] Dead-include sweep in touched files; build stays green.
- [ ] `developer_notes.md` §32 "Versioned wire format (JEF-23)": frame layout, envelope ownership rule, version-mismatch drop behavior, StringCompressor/truncation-limit removal, codec TU + `--wire-test`, and that JEF-24's WebRtcTransport must NOT add the envelope byte.
- [ ] All four gates green; commit.

## Self-review notes (plan time)

- The mid-plan red `--remote-test` (Task 3) is deliberate and bounded: client and server cannot be format-mixed because the sync handshake requires both sides to parse. Reviewers for Task 3 verify fidelity statically; Task 4's gate proves the pair.
- Legacy read/write sequences are the codec spec; Task 2 mandates reading BOTH sides of each message before writing its codec — this is where silent field-order drift would hide.
- The verbatim-forward group is the reason the frame header must be self-contained in the payload bytes (server never re-parses before forwarding) — preserved by design.
