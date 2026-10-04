# JEF-30: Observability + session-health UI

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Checkbox steps.

**Goal:** Surface per-participant WebRTC connection health in the client (RTT, throughput, direct-vs-relay path) and provide cloud-side CloudWatch dashboards + alarms for the coordinator (signaling errors, TURN bandwidth, active sessions). Two deliverables in two repos, each merging to its own integration branch.

**Architecture:** (Client) `WebRtcTransport` gains per-peer stats read from libdatachannel (`rtc::PeerConnection::rtt()`, `bytesSent()/bytesReceived()`, `getSelectedCandidatePair()` → candidate `Type` for direct/relayed), exposed via a TU-safe bridge getter; the Remote dialog shows a small per-participant health indicator. A headless `--stats-test` asserts the getters return plausible values during a live session. (Cloud) an OpenTofu `observability` module builds a CloudWatch dashboard + alarms over the signaling Lambda / DynamoDB / coturn metrics, `tofu validate`-clean; deploy deferred.

**Tech Stack:** C++20, libdatachannel stats API, Qt6 (dialog), OpenTofu (cloud). Gates bare, never piped (client) + `tofu validate`/`fmt` (cloud). No live AWS.

## Global Constraints

- CLIENT tasks (T1, T2, T4) branch `JEF-30-observability` from **`feature/remote-webrtc`** (jefecheck2), merge back into it. CLOUD task (T3) is in the **jefecheck-coordinator** repo on **`feature/coordinator`**, merges back into it. NEVER merge to `qt-experimental`/`main`.
- Zero regression: all client gates (`--remote-test`, `--remote-test-webrtc`, `--coord-test`, `--asset-test`, `--asset-test-webrtc`, `--wire-test`, `--cc-test`, `--fx-test`, `--playlist-test`) green; coordinator `npm test` (80) + `tofu validate` green.
- Stats are READ-ONLY observability — must not perturb the transport (no locks held across libdatachannel calls; stats reads are best-effort, tolerate `nullopt`/false from the API). RakNet mode has no WebRTC stats — the getter returns "n/a"/basic (connected/participant count) for RakNet, real stats for WebRTC. Path type: `Relayed` remote candidate ⇒ "relay (TURN)", else "direct".
- libdatachannel stats API (verified present): `rtc::PeerConnection::rtt() -> optional<chrono::milliseconds>`, `bytesSent()/bytesReceived() -> size_t`, `getSelectedCandidatePair(Candidate* local, Candidate* remote) -> bool`, `Candidate::type() -> enum{Unknown,Host,ServerReflexive,PeerReflexive,Relayed}`.
- No new external deps. Commits end `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: Per-peer WebRTC stats in WebRtcTransport + TU-safe getter + `--stats-test` (client)

**Files:** `src/gfcTransport.h` (a `PeerStats` struct + a virtual `peerStats()` on ITransport, defaulted), `src/gfcWebRtcTransport.{h,cpp}` (implement), `src/gfcRakNetTransport.cpp` (return basic/n-a), `src/qt/SequenceLoadBridge_qt.{h,cpp}` (TU-safe getter), `src/main_qt.cpp` (`--stats-test`).

**Produces:** `struct jefe::net::PeerStats { PeerId peer; long rttMs = -1; uint64_t bytesSent=0, bytesReceived=0; enum class Path { Unknown, Direct, Relay } path = Path::Unknown; bool connected=false; };` and `virtual std::vector<PeerStats> ITransport::peerStats() { return {}; }`. WebRtcTransport reads each peer's `rtt()`/`bytesSent()`/`bytesReceived()`/`getSelectedCandidatePair()` under the peer-map lock ONLY to copy the shared_ptr out, then queries libdatachannel OUTSIDE the lock (stats calls may block). Path: remote candidate `Relayed` → Relay, else Direct. Bridge getter `jefe::qt::remotePeerStats()` returns a UI-friendly per-participant view (nickname if resolvable + rtt/kbps/path); honors `gCloudConnectInFlight` (return empty during a cloud connect, like the other getters).

**Steps:**
- [ ] Add PeerStats + ITransport::peerStats() (defaulted empty). RakNet override returns one entry per connected peer with path=Unknown, connected=true, rtt=-1 (no WebRTC stats).
- [ ] WebRtcTransport::peerStats(): iterate peers (copy shared_ptrs under lock), query rtt/bytes/candidate-pair outside the lock, map candidate Type→Path. Tolerate nullopt/false. Include the assets channel bytes if cheap, else just the pc totals.
- [ ] Bridge getter + `--stats-test`: reuse the `--coord-test` (or `--remote-test-webrtc`) two-process harness; after the session is established + some traffic has flowed, call peerStats()/remotePeerStats() and assert: at least one peer, connected=true, bytesSent>0 || bytesReceived>0 (traffic observed), path is Direct or Relay (not Unknown) for the WebRTC peer. Print `STATS-TEST: peers=N rtt=<ms> bytes=<n> path=<direct|relay>`, exit 0 iff a WebRTC peer with real stats is observed. Bounded, deterministic, reap children.
- [ ] Gates: build; `--stats-test` (peers>=1, real stats, exit 0); all existing client gates green. Commit.

### Task 2: Session-health indicator in the Remote dialog (client)

**Files:** `src/qt/RemotePanel_qt.{h,cpp}`.

**Produces:** In the connected session view, each participant row (or a small panel) shows a health indicator: RTT (ms), throughput (kbps, derived from byte deltas over the refresh interval), and a path badge ("direct" / "relay (TURN)") + a colored dot (good/warn) from RTT thresholds. Driven by the existing `refreshConnectionState()` timer via `jefe::qt::remotePeerStats()`. RakNet sessions show "connected" without the WebRTC-specific fields (graceful).

**Steps:**
- [ ] Extend the participant display to render per-peer stats from `remotePeerStats()`; compute kbps from byte deltas between refreshes (store the previous sample + timestamp — use a monotonic clock; guard div-by-zero). Path badge + a good/warn dot (e.g. green < 100ms, amber >= 100ms; relay always shows the TURN badge). Object names: dotted-leaf scheme (`remote.participant.rtt`, `remote.participant.path`, …).
- [ ] Do NOT block the UI; stats reads are cheap getters on the GUI thread (already gated by gCloudConnectInFlight). Handle empty stats (pre-connect / RakNet) gracefully.
- [ ] Gates: build (Qt UI compiles); all headless gates green (dialog not exercised headlessly). Commit.

### Task 3: OpenTofu observability module (cloud — jefecheck-coordinator repo)

**Files (in ~/projects/jefecheck-coordinator worktree at /Users/dgollas/workspaces/JEF-30-observability/jefecheck-coordinator):** `infra/modules/observability/{main.tf,variables.tf,outputs.tf}`, `infra/envs/dev/main.tf` (wire it), `infra/README.md`.

**Produces:** `modules/observability`: an `aws_cloudwatch_dashboard` with widgets for — signaling Lambda `Invocations`/`Errors`/`Duration`/`Throttles` (per function or aggregated), API Gateway WS `ConnectCount`/`MessageCount`/integration errors, DynamoDB `ConsumedRead/WriteCapacityUnits`/`ThrottledRequests`, coturn EC2 `NetworkIn`/`NetworkOut` (the TURN relay bandwidth — the credit-metering signal), and a custom `ActiveSessions` metric namespace (documented as emitted by the coordinator later — the widget references the namespace/metric name as the contract). Plus `aws_cloudwatch_metric_alarm`s: signaling Lambda error rate high, TURN NetworkOut above a threshold, DynamoDB throttling > 0. Variables: the resource names/ARNs/dimensions from the signaling + turn modules (function names, table name, api id, instance id). Outputs: dashboard name/URL.

**Steps:**
- [ ] Write the module + wire it in `envs/dev` (pass the signaling/turn module outputs as dimensions). Reference real metric namespaces (`AWS/Lambda`, `AWS/ApiGateway`, `AWS/DynamoDB`, `AWS/EC2`) + the custom `JefeCheck/Coordinator ActiveSessions` contract.
- [ ] Gates: `cd infra/envs/dev && /opt/homebrew/bin/tofu init -backend=false && /opt/homebrew/bin/tofu validate` (pass); `cd infra && /opt/homebrew/bin/tofu fmt -check -recursive` (clean); coordinator `npm test` still 80. Update infra/README with the dashboard + the ActiveSessions-metric contract (the coordinator Lambda should `PutMetricData` on session create/close — note as a follow-up if not wired). Commit.
- [ ] Final review of the cloud diff; merge `--no-ff` into `feature/coordinator` (from the coordinator worktree).

### Task 4: Client docs §36 + merge (client)

**Files:** `developer_notes.md` (§36).

**Steps:**
- [ ] `developer_notes.md` §36 "Session-health stats (JEF-30)": the libdatachannel stats read (rtt/bytes/candidate-pair→path), the ITransport::peerStats() seam (WebRTC real / RakNet basic), the bridge getter + gCloudConnectInFlight gating, the dialog indicator (RTT/kbps/path badge), the `--stats-test` harness, and that the cloud dashboards live in the coordinator repo (JEF-30 cloud half). Keep §31-35 intact.
- [ ] Full client gate sweep. Commit.
- [ ] Final whole-branch review of the CLIENT diff; merge `--no-ff` into `feature/remote-webrtc`; rebuild + rerun `--stats-test`/`--remote-test-webrtc`/`--coord-test` post-merge.

## Self-review notes (plan time)

- Two independent halves (client stats UI on feature/remote-webrtc; cloud dashboards on feature/coordinator) — each merges to its own branch; the controller runs a review + merge per half.
- Stats are read-only + best-effort (tolerate libdatachannel returning nullopt/false); no perturbation of the transport, no lock across libdatachannel calls — matches the transport's existing discipline.
- The `--stats-test` gives a real headless gate (observes non-zero bytes + a resolved path over a live WebRTC session), so the client half is genuinely verified, not just build-green.
- The cloud dashboards are `tofu validate`-clean but not deployed (no AWS creds) — same honest boundary as JEF-25/26. The `ActiveSessions` custom metric is a documented contract the coordinator Lambda would emit (a small follow-up), so the widget is meaningful once the coordinator emits it.
- RakNet graceful-degradation keeps the indicator honest across all three transport modes.
