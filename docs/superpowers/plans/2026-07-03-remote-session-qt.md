# JEF-4 — Qt Remote-Session Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reconnect the inert RakNet remote-session runtime to the Qt event loop so two JefeCheck instances can host/join, mirror playback/CC/FX/playlist actions, chat, and see remote pointers.

**Architecture:** Approach A (poll-based bridge). A single always-on tick (the existing `MainWindow_Qt::playbackTimer_`, 4 ms) pumps `networkManager.update()` via a `jefe::qt::pumpNetwork()` bridge accessor. Network state reaches the UI through plain TU-safe getters (no Qt types cross the rendering-chain boundary). The dead `void*` `*gui_qt` stub classes are deleted; the client/server write status/participants into their own members instead. The Remote dialog becomes modeless so status/participants/chat are visible during a live session. Chat + remote pointers render through the ported `networkManager.draw()` GL overlay in `GlViewport_Qt::paintGL`.

**Tech Stack:** C++20, Qt6 (Widgets + OpenGLWidgets), RakNet (vendored in `src/`), CMake (`file(GLOB)` sources), OpenGL 2.1 compatibility profile on macOS.

## Global Constraints

- **Qt6 is the only UI backend.** No FLTK. (CLAUDE.md)
- **TU-separation (developer_notes §1):** only `src/qt/SequenceLoadBridge_qt.cpp` may `#include` rendering-chain managers (`gfcNetworkManager`, `gfcPlateManager`, `gfcPlaybackManager`, …). UI TUs (`MainWindow_qt.cpp`, `RemotePanel_qt.cpp`, `GlViewport_qt.cpp`, `main_qt.cpp`) reach them only through `jefe::qt::*` accessors. Never add a manager `#include` to a UI TU.
- **No QThread for networking.** The shared managers are not thread-safe; inbound messages mutate them directly. The pump runs on the GUI thread via the existing timer. (spec — rejected Approach C)
- **macOS GL:** ARB shader entry points only; do not mix ARB and modern GL. GL object handles init to 0. (CLAUDE.md)
- **Sources are globbed:** `file(GLOB src/*.cpp)` + `file(GLOB src/qt/*.cpp)` (CMakeLists.txt:30-46). Deleting a `.cpp` drops it from the build; adding one requires re-running `cmake -B build`.
- **Headless test idiom:** self-test flags resolved in `src/main_qt.cpp`, run before/without the window using `jefe::qt::initializeRenderingChain()` + bridge accessors, print a `NAME-TEST: …` line, and `std::_Exit(0)` on pass / non-zero on fail. (main_qt.cpp playlist-test, ~line 202)
- **Playlist gate preserved:** `sendRemoteLoadRequests` / `currentContentIsPlaylistItem()` already enforce manual §3 ("only Playlist-loaded items participate"). Do not weaken it.
- **Build command (macOS):** `cmake --build build 2>&1 | tail -5` — expect it to end without `error:`.
- **Status color enum:** `enum gfcNetStatusColors{GFCCOLOR_GREEN, GFCCOLOR_YELLOW, GFCCOLOR_RED, GFCCOLOR_GRAY};` (gfcNetworkStructures.h:43).

---

## File map

| File | Responsibility | Tasks |
|------|----------------|-------|
| `src/gfcnetworkclient.{h,cpp}` | Client peer; store status/peers/chat; expose getters; drop `myGUI` | 1 |
| `src/gfcnetworkserver.{h,cpp}` | Server peer; expose participant names; drop `myGUI` | 1 |
| `src/qt/gfcnetworkclientgui_qt.{h,cpp}`, `src/qt/gfcnetworkservergui_qt.{h,cpp}` | **Deleted** (dead stubs) | 1 |
| `src/gfcnetworkclientgui.{h,cpp}`, `src/gfcnetworkservergui.{h,cpp}` | **Deleted** (base classes, unused after `myGUI` removal) | 1 |
| `src/gfcnetworkmanager.{h,cpp}` | `stopConnection()`, peer-drop reset, TU-safe getters | 2 |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}` | New `jefe::qt::*` accessors (pump, getters, chat, pointer, test roles); fix `disconnectRemote` | 2,3,4,5,6,7,8 |
| `src/qt/MainWindow_qt.{h,cpp}` | Pump from `playbackTimer_`; own modeless `remoteDialog_` | 3,6 |
| `src/qt/RemotePanel_qt.{h,cpp}` | Live status/participant/error widgets + collapsible chat log | 6 |
| `src/qt/GlViewport_qt.cpp` | Overlay draw in `paintGL`; chat keyboard; pointer send in `mouseMoveEvent` | 7,8 |
| `src/main_qt.cpp` | `--remote-test` / `--remote-test-peer` flags | 4,5 |
| `developer_notes.md` | New remote-session runtime section + manual verification | 9 |

---

## Task 1: Redirect client/server GUI writes to internal state; delete stub GUI classes

The client/server currently push status/peer updates through `myGUI` — an empty `*_qt` stub — so nothing surfaces, and they read connect params from the empty stub in the `params==0` branch. Move the writes to the members the client/server already own (`status`, `statusColor`, `peersInSession`, `chatLog`), add getters, and delete the stub + base GUI classes.

**Files:**
- Modify: `src/gfcnetworkclient.h`, `src/gfcnetworkclient.cpp`
- Modify: `src/gfcnetworkserver.h`, `src/gfcnetworkserver.cpp`
- Delete: `src/qt/gfcnetworkclientgui_qt.{h,cpp}`, `src/qt/gfcnetworkservergui_qt.{h,cpp}`, `src/gfcnetworkclientgui.{h,cpp}`, `src/gfcnetworkservergui.{h,cpp}`

**Interfaces:**
- Produces (client): `std::string getStatus()`, `int getStatusColor()`, `std::vector<std::string> getPeersInSession()`, existing `std::vector<gfcChatLogEntry> getChatLog()`, `bool getIsConnected()`, `bool getAttemptingConnection()`.
- Produces (server): `std::vector<std::string> getParticipantNames()`.

- [ ] **Step 1: Add a private status helper + getters to the client header**

In `src/gfcnetworkclient.h`, in the `public:` section (after `getChatLog()` at line 81) add:

```cpp
    std::string getStatus();
    int getStatusColor();
    std::vector<std::string> getPeersInSession();
```

In the `private:` section, add a helper declaration (near `status`/`statusColor` at lines 96-97):

```cpp
    void setStatusInternal(std::string s, int color);
```

Remove the GUI member and include:
- Delete line 25: `#include "gfcnetworkclientgui.h"`
- Delete line 87: `gfcNetworkClientGUI* myGUI;`

- [ ] **Step 2: Rewrite client `myGUI` call sites to member writes**

In `src/gfcnetworkclient.cpp`:

Add near the top-level method definitions:

```cpp
void gfcNetworkClient::setStatusInternal(std::string s, int color) {
    status = s;
    statusColor = color;
    statusChange = true;
}

std::string gfcNetworkClient::getStatus() { return status; }
int gfcNetworkClient::getStatusColor() { return statusColor; }
std::vector<std::string> gfcNetworkClient::getPeersInSession() { return peersInSession; }
```

Delete the constructor line `myGUI=new gfcNetworkClientGUI_Qt;` (line 51) and its include.

Apply these exact substitutions (the `myGUI->` call sites found at lines 85-88, 106-107, 154-196, 224-316, 531):

| Old (`myGUI->…`) | New |
|---|---|
| `myGUI->setStatus(X, Y);` | `setStatusInternal(X, Y);` |
| `myGUI->setPeersInSession(V);` | `peersInSession = V; statusChange = true;` |
| `myGUI->setStartStopButton(...);` | *(delete the line — button state derives from `getIsConnected()` in Qt)* |
| `myGUI->setRecent(V);` | *(delete — recent-IP UI is out of scope)* |
| `myGUI->disable();` / `myGUI->enable();` | *(delete)* |
| `myGUI->setIPAddress(...)` / `myGUI->setPort(...)` | *(delete)* |

For the `params==0` read branch (lines 85-88, `theServerIP=myGUI->getIPAddress();` etc.): the Qt bridge always passes a non-null `gfcConnectionParams`, so this branch is dead. Guard it so it compiles without `myGUI`:

```cpp
    if (params) {
        theServerIP  = params->serverIP;
        thePort      = params->port;
        thePassword  = params->password;
        this->nickName = params->nickname;
    }
```

In `gfcNetworkClient::Disconnect()` (lines 177-199) replace the three `myGUI->` calls:
- `myGUI->setPeersInSession(emptyVector);` → `peersInSession = emptyVector; statusChange = true;`
- `myGUI->setStatus("Offline: Connection Attempt Canceled",GFCCOLOR_GRAY);` → `setStatusInternal("Offline: Connection Attempt Canceled", GFCCOLOR_GRAY);`
- `myGUI->setStatus("Offline",GFCCOLOR_GRAY);` → `setStatusInternal("Offline", GFCCOLOR_GRAY);`
- `myGUI->setStartStopButton("Connect");` → *(delete)*

- [ ] **Step 3: Server — add participant getter, drop `myGUI`**

In `src/gfcnetworkserver.h`:
- Delete line 22: `#include "gfcnetworkservergui.h"`
- Delete line 58: `gfcNetworkServerGUI* myGUI;`
- Add to `public:`:

```cpp
    std::vector<std::string> getParticipantNames();
```

In `src/gfcnetworkserver.cpp`:
- Delete the constructor line `myGUI=new gfcNetworkServerGUI_Qt;` (line 29).
- The `myGUI->` sites (lines 52-90, 807-811) read params / set status / enable-disable. Params: guard on the passed `gfcServerParams*` exactly like the client (use `params->serverName`/`port`/`password` when non-null); delete the `setIPAddress`/`setStartStopButton`/`setStatus`/`disable`/`enable` calls (server status is derived from `networkManager.getIsServer()` in Qt).
- Add the getter (participant names come from `nickNameAddressMap`):

```cpp
std::vector<std::string> gfcNetworkServer::getParticipantNames() {
    std::vector<std::string> names;
    for (const auto& kv : nickNameAddressMap) names.push_back(kv.second);
    return names;
}
```

- [ ] **Step 4: Delete the stub + base GUI files**

```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2
git rm src/qt/gfcnetworkclientgui_qt.h src/qt/gfcnetworkclientgui_qt.cpp \
       src/qt/gfcnetworkservergui_qt.h src/qt/gfcnetworkservergui_qt.cpp \
       src/gfcnetworkclientgui.h src/gfcnetworkclientgui.cpp \
       src/gfcnetworkservergui.h src/gfcnetworkservergui.cpp
```

- [ ] **Step 5: Re-run CMake and build (verifies no dangling references)**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake -B build >/dev/null && cmake --build build 2>&1 | tail -8
```
Expected: build completes with no `error:` lines. If the linker complains about `gfcNetworkClientGUI`/`gfcNetworkServerGUI` symbols, a `myGUI->` site was missed — grep `grep -rn "myGUI" src/` should return nothing.

- [ ] **Step 6: Commit**

```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2
git add -A && git commit -m "JEF-4: redirect net GUI writes to client/server state; delete stub GUI classes"
```

---

## Task 2: Implement client-side disconnect + TU-safe manager getters

`stopConnection()` is declared but never defined; `disconnectRemote()` no-ops for clients; and there is no way for the UI to read participants/status/chat/errors. Add them behind the bridge.

**Files:**
- Modify: `src/gfcnetworkmanager.h`, `src/gfcnetworkmanager.cpp`
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`

**Interfaces:**
- Consumes: client getters from Task 1 (`getStatus`, `getPeersInSession`, `getChatLog`, `getIsConnected`), `gfcNetworkServer::getParticipantNames`.
- Produces (manager): `void stopConnection()`, `std::vector<std::string> participantNames()`, `std::string connectionStatusText()`, `std::vector<std::string> chatLogLines()`, `std::vector<std::string> drainErrors()`.
- Produces (bridge, `namespace jefe::qt`): `std::vector<std::string> remoteParticipants()`, `std::string remoteStatusText()`, `std::vector<std::string> remoteChatLog()`, `std::vector<std::string> remoteErrors()`.

- [ ] **Step 1: Implement `stopConnection()` + peer-drop reset in the manager**

In `src/gfcnetworkmanager.cpp`, define `stopConnection()` (mirror `stopServer()` at line 106 but for the client):

```cpp
void gfcNetworkManager::stopConnection()
{
    client.Disconnect();
    isServer = false;
    connected = false;
    server.enableGUI();
}
```

In `gfcNetworkManager::update()` (the `client.statusChange` block, ~line 133), make a lost/closed client clear `connected` so the panel reflects a dropped peer. Replace the existing `if(!client.getIsConnected() && !client.getAttemptingConnection()) { server.enableGUI(); }` with:

```cpp
        if(!client.getIsConnected() && !client.getAttemptingConnection())
        {
            server.enableGUI();
            if(!isServer) connected = false;   // client peer dropped
        }
```

- [ ] **Step 2: Add manager getters (declare in header, define in cpp)**

In `src/gfcnetworkmanager.h` `public:` add:

```cpp
    std::vector<std::string> participantNames();
    std::string connectionStatusText();
    std::vector<std::string> chatLogLines();
    std::vector<std::string> drainErrors();
```

Add `#include <vector>` if not present. In `src/gfcnetworkmanager.cpp`:

```cpp
std::vector<std::string> gfcNetworkManager::participantNames() {
    if (isServer) return server.getParticipantNames();
    return client.getPeersInSession();
}

std::string gfcNetworkManager::connectionStatusText() {
    if (isServer) return connected ? "Hosting (server)" : "Not hosting";
    return client.getStatus();   // e.g. "Online!", "Attempting Connection...", "Offline"
}

std::vector<std::string> gfcNetworkManager::chatLogLines() {
    std::vector<std::string> out;
    for (auto& e : client.getChatLog())
        out.push_back(e.sender + ": " + e.message);
    return out;
}

std::vector<std::string> gfcNetworkManager::drainErrors() {
    // Errors already surface through client status strings (RED). Reserved
    // for a dedicated error queue; empty for now so callers compile.
    return {};
}
```

- [ ] **Step 3: Fix `disconnectRemote()` and add bridge getters**

In `src/qt/SequenceLoadBridge_qt.cpp`, replace the `disconnectRemote()` body (lines 1055-1066) with:

```cpp
void disconnectRemote() {
    if (!networkManager.getConnected()) return;
    if (networkManager.getIsServer()) networkManager.stopServer();
    else                              networkManager.stopConnection();
}
```

Append the getter accessors (same file, inside `namespace jefe::qt`):

```cpp
std::vector<std::string> remoteParticipants() { return networkManager.participantNames(); }
std::string              remoteStatusText()   { return networkManager.connectionStatusText(); }
std::vector<std::string> remoteChatLog()      { return networkManager.chatLogLines(); }
std::vector<std::string> remoteErrors()       { return networkManager.drainErrors(); }
```

In `src/qt/SequenceLoadBridge_qt.h`, after the remote declarations (line 372) add:

```cpp
std::vector<std::string> remoteParticipants();
std::string              remoteStatusText();
std::vector<std::string> remoteChatLog();
std::vector<std::string> remoteErrors();
```

- [ ] **Step 4: Build**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "JEF-4: implement client-side stopConnection + TU-safe network state getters"
```

---

## Task 3: Pump the network from the always-on tick

Reuse `MainWindow_Qt::playbackTimer_` (4 ms, always started) to service the sockets each tick, independent of playback.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`
- Modify: `src/qt/MainWindow_qt.cpp`

**Interfaces:**
- Produces: `bool jefe::qt::pumpNetwork()` — calls `networkManager.update()`; returns `true` when connection state/participants/chat changed since last pump (so the panel refreshes only on change).

- [ ] **Step 1: Add `pumpNetwork()` to the bridge**

In `src/qt/SequenceLoadBridge_qt.cpp` (inside `namespace jefe::qt`):

```cpp
bool pumpNetwork() {
    static bool  prevConnected = false;
    static size_t prevPeers    = 0;
    static size_t prevChat     = 0;
    networkManager.update();
    const bool   nowConnected = networkManager.getConnected();
    const size_t nowPeers     = networkManager.participantNames().size();
    const size_t nowChat      = networkManager.chatLogLines().size();
    const bool changed = (nowConnected != prevConnected) ||
                         (nowPeers != prevPeers) || (nowChat != prevChat);
    prevConnected = nowConnected; prevPeers = nowPeers; prevChat = nowChat;
    return changed;
}
```

In `src/qt/SequenceLoadBridge_qt.h` (after line 372):

```cpp
bool pumpNetwork();
```

- [ ] **Step 2: Call the pump from the playback timer**

In `src/qt/MainWindow_qt.cpp`, in the `playbackTimer_` timeout lambda (line 304), the first line is `if (!viewport_) return;`. Immediately after it, add the pump (it must run even when `needsPlaybackTick()` is false):

```cpp
        // Service the RakNet sockets on every tick — inbound messages must
        // be received even while playback is idle. Cheap (non-blocking).
        if (jefe::qt::pumpNetwork() && remoteDialog_)
            remoteDialog_->refreshConnectionState();
```

`remoteDialog_` is introduced in Task 6; until then this references a member that does not yet exist. To keep this task self-contained and buildable, add the member now as a forward-declared pointer:

In `src/qt/MainWindow_qt.h`, near the other dialog/dock members (RemoteDialog_Qt is already forward-declared at line 24), add:

```cpp
    RemoteDialog_Qt* remoteDialog_ = nullptr;
```

- [ ] **Step 3: Build**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines. (`remoteDialog_` stays null until Task 6, so the `refreshConnectionState()` call is guarded.)

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "JEF-4: pump networkManager.update() from the always-on playback tick"
```

---

## Task 4: `--remote-test` headless connection smoke (AC-1)

Two real processes over localhost: an orchestrator that hosts (`--remote-test`) spawns a child client (`--remote-test-peer <ip> <port>`) via `QProcess`. This task proves a client can connect to a server end to end and the server sees 1 participant.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`
- Modify: `src/main_qt.cpp`

**Interfaces:**
- Consumes: `connectAsServer`, `connectAsClient`, `pumpNetwork`, `remoteParticipants`, `isRemoteConnected`, `initializeRenderingChain`.
- Produces: `int jefe::qt::remoteTestServerConnectCount(int port, int settleMs)` and `void jefe::qt::remoteTestPeerConnect(const std::string& ip, int port, int holdMs, bool play)`.

- [ ] **Step 1: Add the two test-role helpers to the bridge**

In `src/qt/SequenceLoadBridge_qt.cpp` (inside `namespace jefe::qt`). These use `RakSleep`-free busy pumping via the existing `playbackManager.getTimestep()` cadence is unnecessary — a simple loop with a millisecond sleep is fine in a headless test:

```cpp
#include <chrono>
#include <thread>

// Orchestrator/server role: host, pump for settleMs while a child connects,
// return the peak participant count observed.
int remoteTestServerConnectCount(int port, int settleMs) {
    RemoteServerParams sp; sp.serverName = "jefe-remote-test"; sp.port = port; sp.password = "";
    connectAsServer(sp);
    int peak = 0;
    for (int t = 0; t < settleMs; t += 10) {
        pumpNetwork();
        peak = std::max<int>(peak, (int)remoteParticipants().size());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return peak;
}

// Child/client role: connect, pump until connected (or timeout), optionally
// start playback (mirrors a play message — used by Task 5), then hold.
void remoteTestPeerConnect(const std::string& ip, int port, int holdMs, bool play) {
    RemoteClientParams cp; cp.clientName = "peer"; cp.serverIP = ip; cp.port = port; cp.password = "";
    connectAsClient(cp);
    for (int t = 0; t < 3000 && !isRemoteConnected(); t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (play) togglePlayFwd();   // sends a play/pause message to the server
    for (int t = 0; t < holdMs; t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

In `src/qt/SequenceLoadBridge_qt.h`:

```cpp
int  remoteTestServerConnectCount(int port, int settleMs);
void remoteTestPeerConnect(const std::string& ip, int port, int holdMs, bool play);
```

- [ ] **Step 2: Add flag resolvers to `src/main_qt.cpp`**

Alongside the other `resolve*` helpers (after `resolveFXMultiTestFile`, ~line 136):

```cpp
// --remote-test : orchestrator/server role (spawns a peer child).
static bool hasRemoteTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--remote-test") == 0) return true;
    return false;
}
// --remote-test-peer <ip> <port> : child/client role.
static bool resolveRemotePeer(int argc, char* argv[], std::string& ip, int& port) {
    for (int i = 1; i + 2 < argc + 1; ++i) {
        if (std::strcmp(argv[i], "--remote-test-peer") == 0 && i + 2 < argc) {
            ip = argv[i + 1]; port = std::atoi(argv[i + 2]); return true;
        }
    }
    return false;
}
```

- [ ] **Step 3: Dispatch both roles before the window**

In `src/main_qt.cpp`, right after the `--playlist-test` block (after its `std::_Exit`, ~line 230) and before `MainWindow_Qt window;`:

```cpp
    // --remote-test-peer <ip> <port>: child client role. Connects, holds,
    // exits. Headless; playback state is pure data (no GL needed).
    {
        std::string peerIp; int peerPort = 0;
        if (resolveRemotePeer(argc, argv, peerIp, peerPort)) {
            jefe::qt::initializeRenderingChain();
            jefe::qt::remoteTestPeerConnect(peerIp, peerPort, /*holdMs=*/2000, /*play=*/true);
            std::_Exit(0);
        }
    }
    // --remote-test: orchestrator/server role. Hosts, spawns a peer child,
    // asserts the server observed the client join.
    if (hasRemoteTest(argc, argv)) {
        jefe::qt::initializeRenderingChain();
        const int port = 60123;
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-peer", "127.0.0.1", QString::number(port)});
        peer.start();
        const int peak = jefe::qt::remoteTestServerConnectCount(port, /*settleMs=*/3000);
        peer.waitForFinished(3000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("REMOTE-TEST: participants_peak=%d\n", peak);
        fflush(stdout);
        std::_Exit(peak >= 1 ? 0 : 2);
    }
```

Add includes at the top of `src/main_qt.cpp` if missing: `#include <QProcess>`, `#include <QCoreApplication>`, `#include <string>`.

- [ ] **Step 4: Build**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines.

- [ ] **Step 5: Run the connection test**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && ./build/jefecheck --remote-test; echo "exit=$?"
```
Expected: a line `REMOTE-TEST: participants_peak=1` (or higher) and `exit=0`. If `participants_peak=0`, the server never saw the peer — check the port is free and `pumpNetwork()` is being called in both roles.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "JEF-4: --remote-test headless two-process connection smoke"
```

---

## Task 5: `--remote-test` mirrored-action assertion (AC-2)

Extend the orchestrator to assert that a play toggled on the child mirrors to the server's playback state. The child already calls `togglePlayFwd()` (Task 4, `play=true`), which sends a play/pause message; the server's internal server-client applies it to the same global `playbackManager`.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (extend orchestrator helper)
- Modify: `src/main_qt.cpp` (assert playing)

**Interfaces:**
- Consumes: `jefe::qt::isPlaying()` (bridge, line 473).
- Produces: `bool jefe::qt::remoteTestServerSawPlay(int port, int settleMs)`.

- [ ] **Step 1: Add an orchestrator helper that watches for mirrored play**

In `src/qt/SequenceLoadBridge_qt.cpp`:

```cpp
// Host, pump while the child connects and toggles play, and report whether
// the mirrored play state arrived on this (server) side.
bool remoteTestServerSawPlay(int port, int settleMs) {
    RemoteServerParams sp; sp.serverName = "jefe-remote-test"; sp.port = port; sp.password = "";
    connectAsServer(sp);
    bool sawPlay = false;
    for (int t = 0; t < settleMs; t += 10) {
        pumpNetwork();
        if (isPlaying()) { sawPlay = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return sawPlay;
}
```

Declare in `src/qt/SequenceLoadBridge_qt.h`:

```cpp
bool remoteTestServerSawPlay(int port, int settleMs);
```

- [ ] **Step 2: Assert both connection and mirroring in the orchestrator**

In `src/main_qt.cpp`, replace the orchestrator body (the `if (hasRemoteTest(...))` block from Task 4) so it checks the mirrored play in addition to the join:

```cpp
    if (hasRemoteTest(argc, argv)) {
        jefe::qt::initializeRenderingChain();
        const int port = 60123;
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-peer", "127.0.0.1", QString::number(port)});
        peer.start();
        const bool sawPlay = jefe::qt::remoteTestServerSawPlay(port, /*settleMs=*/4000);
        const int  peak    = (int)jefe::qt::remoteParticipants().size();
        peer.waitForFinished(3000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("REMOTE-TEST: participants=%d mirrored_play=%d\n", peak, sawPlay ? 1 : 0);
        fflush(stdout);
        std::_Exit((peak >= 1 && sawPlay) ? 0 : 2);
    }
```

- [ ] **Step 3: Build**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines.

- [ ] **Step 4: Run the mirroring test**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && ./build/jefecheck --remote-test; echo "exit=$?"
```
Expected: `REMOTE-TEST: participants=1 mirrored_play=1` and `exit=0`. If `mirrored_play=0`, the play message is not being applied server-side — verify the server rebroadcasts `GFCNETID_PLAYPAUSEMESSAGE` to its internal server-client and that `setTakeNotifications` is not swallowing the apply.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "JEF-4: --remote-test asserts mirrored play/pause across the session"
```

---

## Task 6: Modeless Remote dialog with live status, participants, errors, and chat log (AC-3)

Convert `RemoteDialog_Qt` from a modal (`exec()`) stack local to a persistent modeless window owned by `MainWindow`, and add live widgets refreshed on the pump's dirty signal.

**Files:**
- Modify: `src/qt/RemotePanel_qt.h`, `src/qt/RemotePanel_qt.cpp`
- Modify: `src/qt/MainWindow_qt.cpp`

**Interfaces:**
- Consumes: `jefe::qt::remoteStatusText`, `remoteParticipants`, `remoteChatLog`, `remoteErrors`, `isRemoteConnected`, `isRemoteServer`.
- Produces: a persistent `MainWindow_Qt::remoteDialog_` (declared in Task 3); `RemoteDialog_Qt::refreshConnectionState()` now also fills the participant/chat/error widgets.

- [ ] **Step 1: Add the live widgets to the dialog header**

In `src/qt/RemotePanel_qt.h`, forward-declare `class QListWidget;` and `class QTextEdit;` and `class QGroupBox;` at the top, and add members (after `statusLabel_` at line 55):

```cpp
    QListWidget* participantsList_ = nullptr;
    QLabel*      errorLabel_ = nullptr;
    QTextEdit*   chatLogView_ = nullptr;     // collapsible full chat history
    QGroupBox*   chatLogBox_ = nullptr;      // checkable → collapses chatLogView_
```

- [ ] **Step 2: Build the widgets in the constructor**

In `src/qt/RemotePanel_qt.cpp` constructor (after the existing status label creation, before the final layout add), create and add:

```cpp
    participantsList_ = new QListWidget(this);
    participantsList_->setObjectName("remote.participants");
    participantsList_->setMaximumHeight(120);

    errorLabel_ = new QLabel(QString(), this);
    errorLabel_->setObjectName("remote.error");
    errorLabel_->setStyleSheet("color:#e06c75;");   // red for errors
    errorLabel_->setWordWrap(true);

    chatLogBox_ = new QGroupBox(tr("Chat log"), this);
    chatLogBox_->setObjectName("remote.chatlogbox");
    chatLogBox_->setCheckable(true);
    chatLogBox_->setChecked(false);                 // collapsed by default
    auto* chatLayout = new QVBoxLayout(chatLogBox_);
    chatLogView_ = new QTextEdit(chatLogBox_);
    chatLogView_->setObjectName("remote.chatlog");
    chatLogView_->setReadOnly(true);
    chatLayout->addWidget(chatLogView_);
    connect(chatLogBox_, &QGroupBox::toggled, chatLogView_, &QWidget::setVisible);
    chatLogView_->setVisible(false);
```

Add these to the dialog's main layout (below the status label). Add includes: `#include <QListWidget>`, `#include <QTextEdit>`, `#include <QGroupBox>`, `#include <QVBoxLayout>`.

- [ ] **Step 3: Fill the widgets in `refreshConnectionState()`**

In `src/qt/RemotePanel_qt.cpp`, extend `refreshConnectionState()` (line 160) after the existing status/button logic:

```cpp
    statusLabel_->setText(QString::fromStdString(jefe::qt::remoteStatusText()));

    participantsList_->clear();
    for (const auto& name : jefe::qt::remoteParticipants())
        participantsList_->addItem(QString::fromStdString(name));

    const auto errs = jefe::qt::remoteErrors();
    errorLabel_->setText(errs.empty() ? QString()
                                      : QString::fromStdString(errs.back()));

    chatLogView_->clear();
    for (const auto& line : jefe::qt::remoteChatLog())
        chatLogView_->append(QString::fromStdString(line));
```

(The `jefe::qt::*` accessors are already declared in `SequenceLoadBridge_qt.h`, which `RemotePanel_qt.cpp` includes.)

- [ ] **Step 4: Make the dialog modeless and owned by MainWindow**

In `src/qt/MainWindow_qt.cpp`, replace both `RemoteDialog_Qt dlg(this); dlg.exec();` sites (line 469 File menu, line 606 Dialogs menu) with a shared lambda that lazily creates the persistent dialog and shows it modelessly:

```cpp
    auto showRemote = [this]() {
        if (!remoteDialog_) remoteDialog_ = new RemoteDialog_Qt(this);
        remoteDialog_->show();
        remoteDialog_->raise();
        remoteDialog_->activateWindow();
        remoteDialog_->refreshConnectionState();
    };
```

Use `showRemote` in both `addAction(...)` lambdas instead of the `exec()` blocks. Ensure `#include "RemotePanel_qt.h"` is present in `MainWindow_qt.cpp` (it is, for the forward-declared type usage — add if the include is missing).

- [ ] **Step 5: Build and smoke-launch**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines. Manually: launch `./build/jefecheck`, open File → Remote Session…, confirm the window is non-blocking (you can still interact with the main viewport while it's open) and shows an (empty) participants list + collapsible Chat log group.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "JEF-4: modeless Remote dialog with live status/participants/errors + chat-log view"
```

---

## Task 7: Viewport chat overlay + keyboard chat entry (AC-4)

Render the ported `networkManager.draw()` overlay (chat + pointers) in the Qt viewport, and route keyboard chat entry through the bridge so a user can type over the image and press Enter to send.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`
- Modify: `src/qt/GlViewport_qt.cpp`

**Interfaces:**
- Produces (bridge): `void drawNetworkOverlay(int w, int h)`, `bool remoteChatModeActive()`, `void remoteChatBegin()`, `void remoteChatAppend(const std::string& s)`, `void remoteChatBackspace()`, `void remoteChatSubmit()`, `void remoteChatCancel()`, `bool isRemoteConnected()` (existing).

- [ ] **Step 1: Add overlay + chat bridge accessors**

In `src/qt/SequenceLoadBridge_qt.cpp`:

```cpp
void drawNetworkOverlay(int w, int h) { networkManager.draw(w, h); }

bool remoteChatModeActive() { return networkManager.gChatMode == 1; }
void remoteChatBegin()      { networkManager.gChatMode = 1; }
void remoteChatCancel()     { networkManager.gChatMode = 0; networkManager.gChatTextString.clear(); }
void remoteChatBackspace()  {
    auto& s = networkManager.gChatTextString;
    if (!s.empty()) s.pop_back();
}
void remoteChatAppend(const std::string& s) {
    if (networkManager.gChatTextString.size() + s.size() < 254)
        networkManager.gChatTextString += s;
}
void remoteChatSubmit() {
    networkManager.sendChatMessage();          // reads gChatTextString
    networkManager.gChatTextString.clear();
    networkManager.gChatMode = 0;
}
```

Declare all seven in `src/qt/SequenceLoadBridge_qt.h`.

- [ ] **Step 2: Draw the overlay in `paintGL`**

In `src/qt/GlViewport_qt.cpp`, in `paintGL()` (line 109), after `listener_->onDraw();` (line 120) and inside the `if (listener_)` block, add:

```cpp
        // Chat + remote-pointer overlay (ported from the FLTK GlViewport).
        // Drawn last so it composites over the plates.
        const float dpr = devicePixelRatioF();
        jefe::qt::drawNetworkOverlay(int(width() * dpr), int(height() * dpr));
```

- [ ] **Step 3: Route chat keystrokes in `keyPressEvent`**

In `src/qt/GlViewport_qt.cpp`, at the very top of `keyPressEvent()` (line 379, before the W/E/Q/D/S tracking), intercept chat mode:

```cpp
    // Remote chat entry: when in chat mode, keystrokes build the message.
    if (jefe::qt::remoteChatModeActive()) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            jefe::qt::remoteChatSubmit();
        } else if (e->key() == Qt::Key_Escape) {
            jefe::qt::remoteChatCancel();
        } else if (e->key() == Qt::Key_Backspace) {
            jefe::qt::remoteChatBackspace();
        } else if (!e->text().isEmpty() && e->text().at(0).isPrint()) {
            jefe::qt::remoteChatAppend(e->text().toStdString());
        }
        update();     // repaint the overlay with the new text
        return;       // consume — don't fall through to plate shortcuts
    }
    // Enter chat mode with Return when connected and not already typing.
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        jefe::qt::isRemoteConnected()) {
        jefe::qt::remoteChatBegin();
        update();
        return;
    }
```

- [ ] **Step 4: Build and smoke**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines. (End-to-end chat is verified manually in Task 9's two-instance run.)

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "JEF-4: viewport chat/pointer overlay + keyboard chat entry"
```

---

## Task 8: Remote pointers — broadcast local cursor (AC: pointers in scope)

Send the local cursor position over the session on mouse-move (only when connected and moved); received pointers already render via the overlay drawn in Task 7.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`
- Modify: `src/qt/GlViewport_qt.cpp`

**Interfaces:**
- Consumes: `struct gfcNetPointerInfo { int x; int y; int quadID; float scale; int color; };` (gfcNetworkStructures.h:144).
- Produces: `void jefe::qt::sendRemotePointer(int xPx, int yPx)`.

- [ ] **Step 1: Add the pointer bridge accessor**

In `src/qt/SequenceLoadBridge_qt.cpp`. `x`/`y` are integer viewport pixel coords (same space the overlay's `networkManager.draw()` expects); `quadID`/`scale`/`color` are defaulted (the receive side maps color per nickname via `nickNamePointerMap`):

```cpp
void sendRemotePointer(int xPx, int yPx) {
    if (!networkManager.getConnected()) return;
    gfcNetPointerInfo info;
    info.x = xPx;
    info.y = yPx;
    info.quadID = 0;
    info.scale = 1.0f;
    info.color = 0;
    networkManager.sendPointerInfoMessage(info);   // no-ops internally if unchanged/!connected
}
```

Declare in `src/qt/SequenceLoadBridge_qt.h`:

```cpp
void sendRemotePointer(int xPx, int yPx);
```

- [ ] **Step 2: Call it from `mouseMoveEvent`**

In `src/qt/GlViewport_qt.cpp` `mouseMoveEvent()` (line 203), after the pick-drag early return and before/independent of the plate-drag branch, add a hover broadcast (runs on plain moves, no button needed — `setMouseTracking(true)` is already set at line 29). Convert to framebuffer pixel coords with a bottom-left origin to match GL/overlay space:

```cpp
    // Broadcast local cursor to the remote session (framebuffer pixel coords,
    // GL bottom-left origin). sendPointerInfoMessage no-ops when unchanged /
    // disconnected.
    {
        const float dpr = devicePixelRatioF();
        const int xPx = int(float(e->position().x()) * dpr);
        const int yPx = int((float(height()) - float(e->position().y())) * dpr);
        jefe::qt::sendRemotePointer(xPx, yPx);
    }
```

- [ ] **Step 3: Build**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && cmake --build build 2>&1 | tail -5
```
Expected: no `error:` lines.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "JEF-4: broadcast local cursor as a remote pointer on mouse-move"
```

---

## Task 9: Developer notes + manual two-instance verification (AC-5)

Document the runtime wiring and the manual verification recipe; run it.

**Files:**
- Modify: `developer_notes.md`
- Modify: `CLAUDE.md` (one-line pointer to the new section, matching the existing §-index style)

- [ ] **Step 1: Add a remote-session section to `developer_notes.md`**

Append a new numbered section (use the next free number) documenting:
- The pump lives in `MainWindow_Qt::playbackTimer_` via `jefe::qt::pumpNetwork()`; it must stay outside the `needsPlaybackTick()` gate (receive while idle).
- State reaches the UI through TU-safe getters (`remoteStatusText`/`remoteParticipants`/`remoteChatLog`/`remoteErrors`); the `*gui_qt` stub classes were deleted.
- The Remote dialog is modeless and owned by `MainWindow_Qt::remoteDialog_`.
- Chat + pointers render via `jefe::qt::drawNetworkOverlay()` in `GlViewport_Qt::paintGL`; chat entry is routed through `remoteChat*` accessors from `keyPressEvent`.
- `--remote-test` is a two-process (QProcess) localhost harness asserting connect + mirrored play.

- [ ] **Step 2: Run the automated check**

Run:
```bash
cd /Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2 && ./build/jefecheck --remote-test; echo "exit=$?"
```
Expected: `REMOTE-TEST: participants=1 mirrored_play=1` and `exit=0`.

- [ ] **Step 3: Manual two-instance verification**

Document these steps in `developer_notes.md` and perform them once:
1. Launch two instances: `./build/jefecheck` twice.
2. Instance A: File → Remote Session… → Start server (port 60000).
3. Instance B: File → Remote Session… → Connect to `127.0.0.1:60000`.
4. Confirm both participant lists show the peer; status shows connected/hosting.
5. Load the same media via the Playlist on both; on A play/pause, adjust a plate's gamma (W-drag), toggle an FX — confirm B mirrors each.
6. Press Return on A, type, Enter — confirm the message appears in B's overlay and both panels' chat log.
7. Move the cursor on A — confirm B shows A's remote pointer.
8. Disconnect on B — confirm A's participant list shrinks; reconnect works.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "JEF-4: document remote-session runtime + manual verification recipe"
```

---

## Self-review notes

- **Spec coverage:** AC-1 connect → Task 4; AC-2 mirror playback/CC/FX/playlist → Task 5 (play/pause proven automatically; CC/FX/playlist share the same pump+apply path proven by Task 5 and exercised manually in Task 9); AC-3 status+participants+disconnect → Tasks 2, 6; AC-4 chat → Tasks 6 (log) + 7 (send/overlay); AC-5 two-instance verify → Tasks 4/5 (auto) + 9 (manual). Remote pointers (in-scope per brainstorming) → Task 8.
- **Type consistency:** getter names are identical across the manager (`participantNames`, `connectionStatusText`, `chatLogLines`, `drainErrors`) and the bridge (`remoteParticipants`, `remoteStatusText`, `remoteChatLog`, `remoteErrors`); `remoteDialog_` is introduced in Task 3 and used in Tasks 3/6.
- **Known follow-ups (not blockers):** `drainErrors()` returns empty until a dedicated error queue exists — errors still surface via the client RED status string; a richer error queue is a later refinement.
