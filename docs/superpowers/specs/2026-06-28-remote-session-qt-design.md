# JEF-4 — Wire RakNet remote-session collaboration into the Qt build

**Status:** Design approved 2026-06-28
**Ticket:** JEF-4 (Story) — github.com/danielgollas/jefecheck
**Branch:** `JEF-4-remote-session-qt` (from `qt-experimental`)

## Context

JefeCheck's headline collaboration feature is the **Remote Session** ("it's
multiplayer"): a RakNet client/server where every action is mirrored across
participants, with chat and remote pointers (manual §3 Share). The Qt build has
the dialog (`RemotePanel_Qt`) and GUI skeletons, but the remote-session
*runtime* was never reconnected after the FLTK→Qt migration.

### Key finding: the backend is real but inert

The message structs, BitStream serialization, per-manager **send** hooks, the
client/server **receive** handlers, and the playlist gate all still compile into
the Qt build and are sound. What is missing is the runtime that the deleted
`main.cpp` and `GlViewport.cpp` used to provide. Commit `2002dd9`
("qt: delete FLTK-only TUs") removed:

- **`networkManager.update()`** (old `main.cpp:837`) — the socket pump
  (`server.Update()` / `client.Update()` plus the throttled outbound event
  queue flush). **Not called anywhere in the Qt build today** → nothing is sent
  or received end to end, even though every per-manager hook is present.
- **`networkManager.draw()`** (old `GlViewport.cpp:209`) — the chat + remote
  pointer GL overlay.
- **Chat keyboard UX** (old `GlViewport.cpp:813–1090`) — `gChatMode`, typing
  into `gChatTextString`, Enter-to-send, navigation.
- **`sendPointerInfoMessage()`** on mouse-move (old `GlViewport.cpp:563–583,
  756–769`) — remote pointers.
- **`initializeWidgets()`** (old `main.cpp:619`) — GUI bridge binding.

So JEF-4 is overwhelmingly *reconnection* work: drive the pump from the Qt event
loop, draw the overlay from the Qt viewport, forward viewport input through the
bridge, implement client-side disconnect, surface live state in the panel, and
delete the dead GUI-bridge stubs.

### What already works (do not rebuild)

- Custom packet-ID enum + message structs (`gfcNetStructures.h`).
- BitStream serialize/deserialize for transforms, color correction, play/pause,
  other-states, FX add/common/attrib/stack, playlist, chat, pointer
  (`gfcnetworkclient.cpp` / `gfcnetworkserver.cpp`).
- Send hooks in the shared managers (`gfcPlaybackManager`, `gfcPlateManager`,
  `gfcPlaylistManager`) via `notifyEvent(...)` / `send*`.
- Server rebroadcast-to-all-except-sender path.
- Throttling/event time-gate (`setEventSendDelay`, `events[...]` queue) flushed
  inside `update()`.
- Echo prevention guard (`setTakeNotifications(false/true)`) around inbound apply.
- Playlist gate: `sendRemoteLoadRequests` / `currentContentIsPlaylistItem()`
  enforces manual §3 ("only Playlist-loaded items participate").
- `startServer` / `stopServer` / `startConnection` (server-side disconnect works).

## Requirements (from ticket)

- Wire host/start-server and connect/join through `gfcNetworkManager` end to end.
- Mirror playback, plate/CC, FX, and playlist actions across the session.
- Surface connection status, participant list, and errors in `RemotePanel_Qt`.
- Honor the manual's rule that only Playlist-loaded items participate.

## Acceptance criteria

- [ ] A user can start a server and another can connect (LAN) from the Qt UI.
- [ ] Playback / CC / FX / playlist actions on one client mirror to the others.
- [ ] Connection status + participant list shown; disconnect handled cleanly.
- [ ] Text chat send/receive works in the Qt build.
- [ ] Verified with two app instances over localhost/LAN.

## Scope decisions

| Decision | Choice |
|----------|--------|
| Remote pointers | **In scope** for JEF-4 (basic send/receive + overlay render). JEF-5 becomes the richer drawing/annotation layer on top. |
| Chat presentation | **GL viewport overlay** (faithful port of the original type-over-the-image UX) **plus** a collapsible full chat-log view in `RemotePanel_Qt` for scrollback. |
| Status + participant list + errors | Native Qt widgets in `RemotePanel_Qt`. |
| Verification | **`--remote-test` headless self-test** (durable, CI-runnable) **plus** manual two-instance verification documented in `developer_notes.md`. |
| Runtime/state architecture | **Approach A — poll-based bridge** (below). |

## Architecture — Approach A (poll-based bridge)

All manager access stays behind `jefe::qt::*` accessors in
`SequenceLoadBridge_qt.cpp`, the only TU permitted to include the rendering-chain
managers (developer_notes §1 — glad + `QOpenGLWidget` cannot share a TU on
macOS, and `gfcNetworkManager` is on the rendering-chain side). The runtime is
reconnected through three seams:

1. **The pump** — a single always-on `QTimer` owned by `MainWindow_Qt` (~60 Hz,
   matching the original main-loop cadence) calls `jefe::qt::pumpNetwork()`,
   which calls `networkManager.update()` (services `server.Update()` /
   `client.Update()` and flushes the throttled outbound queue) and returns a
   "dirty" flag so the panel refreshes only on change. Runs whenever the app is
   up, independent of playback (must receive while idle).

2. **The overlay** — `GlViewport_Qt::paintGL` calls
   `jefe::qt::drawNetworkOverlay(w, h)` → `networkManager.draw(w, h)` for chat +
   remote pointers, after the plates draw.

3. **Input forwarding** — `GlViewport_Qt` mouse-move and key events call bridge
   accessors (`sendRemotePointer`, chat-entry forwarders). No manager headers in
   the viewport TU.

State reaches the UI through plain, TU-safe **getters** on `gfcNetworkManager`
(no Qt types cross the boundary); `RemotePanel_Qt` rebuilds its widgets from
those getters on the dirty signal. The dead `void*` GUI-bridge stub classes are
**deleted**, not filled in.

### Rejected alternatives

- **B — callback/signal bridge:** callbacks defined in the Qt TU but invoked from
  the network TU is exactly the coupling §1 warns against; needs thread
  marshaling; no real gain at 60 Hz.
- **C — network on a worker thread:** the shared managers are not thread-safe and
  inbound messages mutate them directly; imposes a thread-safety burden across
  the whole rendering chain for a pump the original ran fine on the main loop.

## Components & file changes

| File | Change |
|------|--------|
| `src/gfcnetworkmanager.{h,cpp}` | Implement missing `stopConnection()` (client peer teardown + flag/state reset). Add TU-safe getters: `participantNames()`, `pendingErrors()` (drain a queue), `chatLogSnapshot()`; keep `getConnected` / `getIsServer`. Have `update()` expose/return a changed flag. |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}` | New accessors: `pumpNetwork()`, `drawNetworkOverlay(w,h)`, `remoteParticipants()`, `remoteErrors()`, `remoteChatLog()`, `sendRemotePointer(x,y)`, chat-entry forwarders (`chatBeginMode` / `appendChar` / `backspace` / `submit` / `cancel`). Fix `disconnectRemote()` to call `stopConnection()` for clients. |
| `src/qt/MainWindow_Qt.{h,cpp}` | Own the ~60 Hz pump `QTimer`; on tick call `pumpNetwork()` and, if dirty, signal the remote panel to refresh. |
| `src/qt/RemotePanel_qt.{h,cpp}` | Add a live **status** label, a **participant list** widget, an **error** surface, and a **collapsible full chat-log** view; refresh from getters on the dirty signal. |
| `src/qt/GlViewport_qt.{cpp}` | Call `drawNetworkOverlay` in `paintGL`; forward mouse-move → `sendRemotePointer`; route keyboard chat entry (begin chat mode, type, Enter-send, Esc-cancel, navigation) through the bridge. |
| `src/qt/gfcnetworkclientgui_qt.{h,cpp}`, `src/qt/gfcnetworkservergui_qt.{h,cpp}` | **Delete** (dead `void*` stubs) and drop from `CMakeLists.txt`. Excise `myGUI->setStatus(...)`-style calls in `gfcnetworkclient.cpp` / `gfcnetworkserver.cpp`, replacing with getter-backed state the poll reads. |
| `src/main_qt.cpp` (and/or test entry) | Add `--remote-test` flag handling. |
| `developer_notes.md` | New section documenting the remote-session runtime wiring + TU rules + manual two-instance verification steps. |

## Data flow

**Outbound (local action → peers):** unchanged backend. A user action hits a
shared manager → `notifyEvent(...)` / `send*` → throttled queue flushes inside
`networkManager.update()` on the next pump tick → server rebroadcasts to all
except sender. The playlist gate already ensures only playlist-loaded items
broadcast (manual §3 Share).

**Inbound (peer → local):** `client.Update()` (inside the pump) reads packets,
sets `setTakeNotifications(false)`, applies state to the shared managers
(`plateManager`, `playbackManager`, `playlistManager`), re-enables, marks dirty.
The viewport repaints on its existing schedule; the panel refreshes on the dirty
signal. **Echo prevention** (the `setTakeNotifications` guard) already exists in
the receive handlers; verify it holds for all four action classes (transforms,
color, FX, playback) under the Qt pump.

**Chat:** viewport key events build `gChatTextString` and call
`sendChatMessage()`; inbound chat appends to the log → drawn in the overlay and
mirrored into the panel's collapsible log via `chatLogSnapshot()`.

**Remote pointers:** viewport mouse-move → `sendRemotePointer(x,y)` (only when
connected and the position changed); inbound pointer info renders in the overlay.

## Error handling & disconnect

- **Connection failures** — wrong password, unreachable server, and
  `GFCNETID_NICKALREADYINUSE` (already sent by the server) are captured into the
  manager's error queue, drained by `remoteErrors()`, and shown as a persistent
  line in the panel's status/error surface (e.g. "Nickname already in use",
  "Could not reach server"). Status label reflects
  `Disconnected / Connecting / Hosting / Connected`.
- **Clean disconnect** — `stopConnection()` closes the client RakNet peer, resets
  `connected` / `isServer`, clears participant + chat state. `disconnectRemote()`
  routes clients here and servers to `stopServer()`; UI returns to not-connected.
- **Peer drop** — RakNet `ID_DISCONNECTION_NOTIFICATION` / `ID_CONNECTION_LOST`
  update the server's peer set; the poll reflects the shrunk participant list and
  (for clients) a lost-server error.

## Testing & verification

- **`--remote-test`** (durable, CI-runnable): starts a server and a client over
  localhost in one process invocation, connects, performs a deterministic
  mirrored action (a play/pause toggle and a gamma change), pumps both sides, and
  asserts the receiver's `playbackManager` / `plateManager` state changed. Exits
  non-zero on mismatch or connect failure. Mirrors the `--render-test` /
  `--playlist-test` idiom.
- **Manual two-instance** check during development: two app windows on localhost,
  mirror playback / CC / FX / playlist, exercise chat both directions, confirm
  participant list + clean disconnect. Steps documented in `developer_notes.md`.
- Build smoke on macOS; Linux / Windows via CI as usual.

## Out of scope (future work)

- Rich drawing tools / annotation overlay (JEF-5).
- Save Chat Log export (JEF-10) — this design keeps a chat-log snapshot in
  memory; persisting it is a separate ticket.
- LUT/plugin asset sync beyond what the existing sync handshake already covers.

## Risks & open questions

- **Pump cadence vs. viewport repaint** — at 60 Hz the pump is cheap; if the
  viewport already runs a continuous animation timer, confirm we add a separate
  always-on timer rather than gating on playback (the network must pump while
  idle). To be nailed down in the implementation plan against
  `MainWindow_Qt` / `GlViewport_Qt`.
- **Viewport key focus for chat** — entering chat mode must capture keystrokes in
  `GlViewport_Qt` without stealing global shortcuts; verify focus/modifier
  handling during implementation.
- **`--remote-test` headless GL** — applying inbound state should not require a GL
  context for the assertion (state lives in the managers); confirm the chosen
  actions (play/pause, gamma) are assertable without a rendered frame.
