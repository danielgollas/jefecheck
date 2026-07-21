# JEF-22: ITransport Seam Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put all RakNet transport usage behind a polled `ITransport` interface (`RakNetTransport` implementation) with zero behavior change; `gfcNetworkManager`'s public API is untouched and `--remote-test` stays green.

**Architecture:** A new `jefe::net::ITransport` (poll-based, matching the existing 4 ms pump) delivers connection events + raw app packets; `PeerId` (packed `u64` of RakNet `SystemAddress` ip+port) replaces `SystemAddress` everywhere outside the transport TU. Serialization (`RakNet::BitStream`, `StringCompressor`, `GFCNETID_*` enums) intentionally REMAINS in the app for this phase — it is removed in JEF-23 (versioned wire format). The seam is transport only: lifecycle, send, receive, peer identity.

**Tech Stack:** C++20, vendored RakNet (unchanged), CMake glob build (new files under `src/` are picked up automatically). No test framework exists; the gates are a compile, a grep audit, and the two-process `--remote-test` harness.

## Global Constraints

- Branch: `JEF-22-itransport-seam` (from `qt-experimental`), worktree `/Users/dgollas/workspaces/JEF-22-itransport-seam/jefecheck2`. Never touch `main`.
- Zero behavior change: no message ordering, timing, or wire-format changes. All sends stay `HIGH_PRIORITY, RELIABLE_ORDERED, channel 0`.
- `gfcNetworkManager.h` public API must not change (Qt bridge and `--remote-test` depend on it).
- After this phase, `RakPeerInterface`, `SystemAddress`, `RakNetworkFactory`, `MessageIdentifiers.h` (the `ID_*` system constants) must appear ONLY in `src/gfcRakNetTransport.{h,cpp}`. Exceptions that stay until JEF-23: `BitStream.h` + `StringCompressor.h` includes in `gfcnetworkclient.cpp` / `gfcnetworkserver.cpp` / `gfcNetworkStructures.{h,cpp}`, and `gfcNetworkStructures.h`'s `GFCNETID_*` enum anchored on `ID_USER_PACKET_ENUM` (move the anchor to a literal constant so MessageIdentifiers.h is not needed there — see Task 1).
- Build command: `cmake --build build_qt -j8` (configure first if `build_qt/` missing: `cmake -B build_qt`).
- Exit gate for every task: build succeeds AND `./build_qt/jefecheck --remote-test` exits 0.
- macOS note: `GLhandleARB` etc. irrelevant here — networking TUs must NOT include any GL or Qt headers (TU separation, developer_notes §1).

---

### Task 1: `ITransport` interface + `PeerId` + de-anchor `GFCNETID_*`

**Files:**
- Create: `src/gfcTransport.h`
- Modify: `src/gfcNetworkStructures.h` (enum anchor only)

**Interfaces:**
- Consumes: nothing.
- Produces: `jefe::net::PeerId`, `jefe::net::packPeerId(uint32_t ip, uint16_t port)`, `jefe::net::TransportEvent`, `jefe::net::TransportEventType`, `class jefe::net::ITransport` — exact signatures below; Tasks 2–4 depend on them verbatim.

- [ ] **Step 1: Write `src/gfcTransport.h`**

```cpp
#ifndef GFCTRANSPORT_H
#define GFCTRANSPORT_H

// JEF-22: transport seam. Poll-based to match the existing 4 ms network pump.
// PeerId packs a RakNet SystemAddress (binaryAddress<<16 | port) so no RakNet
// type crosses this boundary. Serialization (BitStream) stays app-side until
// the versioned wire format lands (JEF-23).

#include <cstdint>
#include <string>
#include <vector>

namespace jefe {
namespace net {

using PeerId = uint64_t;
constexpr PeerId kInvalidPeerId = 0;

inline PeerId packPeerId(uint32_t binaryAddress, uint16_t port) {
    return (static_cast<uint64_t>(binaryAddress) << 16) | port;
}

enum class TransportEventType {
    ConnectAccepted,   // client: server accepted us (peer = server)
    ConnectFailed,     // client: attempt failed
    ServerFull,        // client: no free incoming connections
    AlreadyConnected,  // client: duplicate connect (log only)
    Disconnected,      // client: graceful disconnect from server
    ConnectionLost,    // client: dropped
    PeerConnected,     // host: new incoming connection (peer = who)
    PeerDisconnected,  // host: peer left gracefully (peer = who)
    PeerLost,          // host: peer dropped (peer = who)
    Data               // app packet; bytes = full payload incl. leading id byte
};

struct TransportEvent {
    TransportEventType type;
    PeerId peer = kInvalidPeerId;
    std::vector<unsigned char> bytes; // only for Data
};

class ITransport {
public:
    virtual ~ITransport() = default;

    // Host role
    virtual bool startHost(unsigned short port, const std::string& password,
                           int maxClients) = 0;
    virtual void stopHost() = 0;

    // Client role
    virtual bool connect(const std::string& ip, unsigned short port,
                         const std::string& password) = 0;
    virtual void disconnect() = 0;

    // IO. poll() returns false when no event is available this call.
    virtual bool poll(TransportEvent& ev) = 0;
    // Mirrors RakNet Send semantics used app-wide today:
    // broadcastExcluding=false -> send to target only;
    // broadcastExcluding=true  -> send to everyone EXCEPT target
    //   (target==kInvalidPeerId -> everyone).
    virtual void send(const unsigned char* data, int len, PeerId target,
                      bool broadcastExcluding) = 0;

    virtual void closePeer(PeerId peer, bool sendNotification) = 0;
    virtual int connectionCount() = 0;
};

} // namespace net
} // namespace jefe

#endif
```

- [ ] **Step 2: De-anchor `GFCNETID_*` from `MessageIdentifiers.h`**

In `src/gfcNetworkStructures.h`, the enum at line 46 starts from RakNet's `ID_USER_PACKET_ENUM`. Replace that anchor with the literal value so the header no longer needs RakNet's `MessageIdentifiers.h`. Verify the literal first:

Run: `grep -n "ID_USER_PACKET_ENUM" src/MessageIdentifiers.h | head -3`
Expected: the enum position; in this vendored RakNet 3.x, `ID_USER_PACKET_ENUM` = 100 — CONFIRM the actual value by counting the enum or compiling the probe below; if it differs, use the actual value.

Probe (temporary, delete after): add `static_assert(ID_USER_PACKET_ENUM == GFCNET_USER_PACKET_BASE, "anchor drift");` in `gfcRakNetTransport.cpp` (Task 1 creates only the header; put the static_assert there in Task 2 instead — it lives permanently in the transport TU as a tripwire).

```cpp
// gfcNetworkStructures.h — replace:
//   enum gfcNetPacketEnums{ GFCNETID_... = ID_USER_PACKET_ENUM, ...
// with:
constexpr unsigned char GFCNET_USER_PACKET_BASE = 100; // == RakNet ID_USER_PACKET_ENUM; static_assert in gfcRakNetTransport.cpp
enum gfcNetPacketEnums{
GFCNETID_NICKNAMESEND = GFCNET_USER_PACKET_BASE,
/* ...rest of the enum unchanged... */
```

Do NOT remove `#include "BitStream.h"` from `gfcNetworkStructures.h` — the `serializeFX/unserializeFX/serializeLUT/unserializeLUT(RakNet::BitStream*)` helpers stay until JEF-23.

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -5`
Expected: success (header-only addition + enum literal swap).

- [ ] **Step 4: Commit**

```bash
git add src/gfcTransport.h src/gfcNetworkStructures.h
git commit -m "JEF-22: add ITransport interface + PeerId; de-anchor GFCNETID enum from MessageIdentifiers.h"
```

---

### Task 2: `RakNetTransport` implementation

**Files:**
- Create: `src/gfcRakNetTransport.h`, `src/gfcRakNetTransport.cpp`

**Interfaces:**
- Consumes: `jefe::net::ITransport` etc. from Task 1.
- Produces: `class jefe::net::RakNetTransport : public ITransport` — constructed default; Tasks 3–4 instantiate one per role (`gfcNetworkClient` owns one, `gfcNetworkServer` owns one).

- [ ] **Step 1: Write `src/gfcRakNetTransport.h`**

```cpp
#ifndef GFCRAKNETTRANSPORT_H
#define GFCRAKNETTRANSPORT_H

#include "gfcTransport.h"

class RakPeerInterface; // fwd-declared; RakNet headers only in the .cpp

namespace jefe {
namespace net {

class RakNetTransport : public ITransport {
public:
    RakNetTransport();
    ~RakNetTransport() override;

    bool startHost(unsigned short port, const std::string& password,
                   int maxClients) override;
    void stopHost() override;
    bool connect(const std::string& ip, unsigned short port,
                 const std::string& password) override;
    void disconnect() override;
    bool poll(TransportEvent& ev) override;
    void send(const unsigned char* data, int len, PeerId target,
              bool broadcastExcluding) override;
    void closePeer(PeerId peer, bool sendNotification) override;
    int connectionCount() override;

private:
    RakPeerInterface* peer_;
    bool hosting_ = false;
};

} // namespace net
} // namespace jefe

#endif
```

- [ ] **Step 2: Write `src/gfcRakNetTransport.cpp`**

Behavior must replicate the existing call patterns exactly (inventory refs: client Startup `gfcnetworkclient.cpp:78-85`, Connect `:87-133`, Disconnect `:191-213`; server start `gfcnetworkserver.cpp:79-111`, stop `:116-120`; both pumps' `Receive/DeallocatePacket` loops).

```cpp
#include "gfcRakNetTransport.h"

#include "RakPeerInterface.h"
#include "RakNetworkFactory.h"
#include "MessageIdentifiers.h"
#include "RakNetTypes.h"
#include "gfcNetworkStructures.h"

static_assert(ID_USER_PACKET_ENUM == GFCNET_USER_PACKET_BASE,
              "GFCNET_USER_PACKET_BASE must equal RakNet ID_USER_PACKET_ENUM");

namespace jefe {
namespace net {

static PeerId toPeerId(const SystemAddress& a) {
    return packPeerId(a.binaryAddress, a.port);
}
static SystemAddress toSystemAddress(PeerId id) {
    SystemAddress a;
    a.binaryAddress = static_cast<unsigned int>(id >> 16);
    a.port = static_cast<unsigned short>(id & 0xFFFF);
    return a;
}

RakNetTransport::RakNetTransport()
    : peer_(RakNetworkFactory::GetRakPeerInterface()) {}

RakNetTransport::~RakNetTransport() {
    // Matches legacy behavior: peers were never destroyed (empty dtors,
    // gfcnetworkclient.cpp:75 / gfcnetworkserver.cpp:76). Keep identical
    // lifetime semantics for zero behavior change; revisit in JEF-23.
}

bool RakNetTransport::startHost(unsigned short port, const std::string& password,
                                int maxClients) {
    stopHost();
    peer_->SetIncomingPassword(password.c_str(), (int)password.size());
    SocketDescriptor socketDescriptor(port, 0);
    bool ok = peer_->Startup((unsigned short)maxClients, 15, &socketDescriptor, 1);
    if (ok) peer_->SetMaximumIncomingConnections((unsigned short)maxClients);
    hosting_ = ok;
    return ok;
}

void RakNetTransport::stopHost() {
    peer_->Shutdown(30);
    hosting_ = false;
}

bool RakNetTransport::connect(const std::string& ip, unsigned short port,
                              const std::string& password) {
    peer_->Shutdown(30);
    SocketDescriptor socketDescriptor;
    socketDescriptor.port = 0;
    peer_->Startup(1, 15, &socketDescriptor, 1);
    return peer_->Connect(ip.c_str(), port, password.c_str(),
                          (int)password.size(), 0);
}

void RakNetTransport::disconnect() {
    peer_->CloseConnection(peer_->GetSystemAddressFromIndex(0), true, 0);
    peer_->Shutdown(30);
}

bool RakNetTransport::poll(TransportEvent& ev) {
    Packet* p = peer_->Receive();
    if (!p) return false;
    ev.peer = toPeerId(p->systemAddress);
    ev.bytes.clear();
    switch (p->data[0]) {
    case ID_CONNECTION_REQUEST_ACCEPTED: ev.type = TransportEventType::ConnectAccepted; break;
    case ID_CONNECTION_ATTEMPT_FAILED:   ev.type = TransportEventType::ConnectFailed;   break;
    case ID_NO_FREE_INCOMING_CONNECTIONS:ev.type = TransportEventType::ServerFull;      break;
    case ID_ALREADY_CONNECTED:           ev.type = TransportEventType::AlreadyConnected;break;
    case ID_NEW_INCOMING_CONNECTION:     ev.type = TransportEventType::PeerConnected;   break;
    case ID_DISCONNECTION_NOTIFICATION:
        ev.type = hosting_ ? TransportEventType::PeerDisconnected
                           : TransportEventType::Disconnected;
        break;
    case ID_CONNECTION_LOST:
        ev.type = hosting_ ? TransportEventType::PeerLost
                           : TransportEventType::ConnectionLost;
        break;
    case ID_MODIFIED_PACKET:
        // Legacy client only logged this; surface as AlreadyConnected-style
        // log-only event? No: keep fidelity — deliver as Data would be wrong.
        // Drop it exactly like the server's default case ignored unknown ids,
        // after logging in the caller is impossible; so map to a dedicated
        // no-op by skipping: deallocate and recurse for the next packet.
        peer_->DeallocatePacket(p);
        return poll(ev);
    default:
        // App packet (>= GFCNET_USER_PACKET_BASE) or unknown system id.
        // Deliver raw bytes; unknown ids fall through app switches' default
        // cases exactly as before.
        ev.type = TransportEventType::Data;
        ev.bytes.assign(p->data, p->data + p->length);
        break;
    }
    peer_->DeallocatePacket(p);
    return true;
}

void RakNetTransport::send(const unsigned char* data, int len, PeerId target,
                           bool broadcastExcluding) {
    SystemAddress addr = (target == kInvalidPeerId)
                             ? UNASSIGNED_SYSTEM_ADDRESS
                             : toSystemAddress(target);
    peer_->Send((const char*)data, len, HIGH_PRIORITY, RELIABLE_ORDERED, 0,
                addr, broadcastExcluding);
}

void RakNetTransport::closePeer(PeerId peer, bool sendNotification) {
    peer_->CloseConnection(toSystemAddress(peer), sendNotification);
}

int RakNetTransport::connectionCount() {
    return (int)peer_->NumberOfConnections();
}

} // namespace net
} // namespace jefe
```

Implementation notes for the engineer (verify against the vendored headers, don't assume):
- `peer_->Send` has a `(const char*, int, ...)` overload in RakNet 3.x (`RakPeerInterface.h`); if only the `BitStream*` overload exists, wrap: `RakNet::BitStream bs((unsigned char*)data, len, false); peer_->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, addr, broadcastExcluding);` (add `#include "BitStream.h"` to the .cpp — allowed, it's the transport TU).
- `SocketDescriptor(port, 0)` constructor: verify signature in `RakNetTypes.h`; legacy server code sets fields directly — mirror whatever the legacy `start()` did (`gfcnetworkserver.cpp:79-111`), including the password call ordering.
- `ID_MODIFIED_PACKET` recursion: bounded (one per tampered packet); legacy behavior was log-and-continue. Acceptable to convert to a `while` loop instead of recursion — do the loop form if the reviewer prefers.
- `CloseConnection(addr, sendNotification)` third arg (channel) defaults to 0 in 3.x; legacy client passed `(addr, true, 0)`.

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -5`
Expected: success. The glob picks up the new .cpp on reconfigure; if it doesn't appear, run `cmake -B build_qt` first.

- [ ] **Step 4: Commit**

```bash
git add src/gfcRakNetTransport.h src/gfcRakNetTransport.cpp
git commit -m "JEF-22: RakNetTransport — ITransport implementation wrapping RakPeerInterface"
```

---

### Task 3: Port `gfcNetworkClient` onto `ITransport`

**Files:**
- Modify: `src/gfcnetworkclient.h`, `src/gfcnetworkclient.cpp`

**Interfaces:**
- Consumes: `ITransport`/`RakNetTransport`/`PeerId`/`TransportEvent` (Tasks 1–2).
- Produces (for Task 4): `jefe::net::PeerId gfcNetworkClient::getServerPeerId()` replaces `SystemAddress getServerSystemAddress()`. Everything else on the class keeps its exact signature.

- [ ] **Step 1: Rewrite `gfcnetworkclient.h` includes + members**

Remove ALL RakNet includes (`RakPeerInterface.h`, `Rand.h`, `RakNetStatistics.h`, `RakNetworkFactory.h`, `MessageIdentifiers.h`, `GetTime.h`, `RakAssert.h`, `RakSleep.h`, `BitStream.h` — the .cpp re-includes `BitStream.h` itself, see Step 2). Add `#include "gfcTransport.h"` and `#include <memory>`. Member changes:

```cpp
// remove:  RakPeerInterface *peer;  RakNetTime nextSendTime;  RakNetTime flipConnectionTime;
// remove:  SystemAddress serverSystemAddress;
// remove:  SystemAddress getServerSystemAddress();
// add:
public:
    jefe::net::PeerId getServerPeerId();
private:
    std::unique_ptr<jefe::net::ITransport> transport_;
    jefe::net::PeerId serverPeerId_ = jefe::net::kInvalidPeerId;
```

(`nextSendTime`/`flipConnectionTime` are dead members — inventory §5 — delete them.)

- [ ] **Step 2: Rewrite `gfcnetworkclient.cpp` transport calls**

At the top: keep `#include "BitStream.h"` and `#include "StringCompressor.h"` (serialization stays); remove `RakPeerInterface.h` / `RakNetworkFactory.h` / `MessageIdentifiers.h` includes; construct `transport_ = std::make_unique<jefe::net::RakNetTransport>();` in the ctor (`#include "gfcRakNetTransport.h"`).

Mechanical transformation rules (apply to every site listed in the inventory):

1. `Startup()` body → delete RakNet calls (transport connect() self-starts); keep the flag resets.
2. `Connect()` (`:87-133`) → `attemptingConnection = transport_->connect(theServerIP, (unsigned short)thePort, thePassword);` keep all surrounding status/flag logic identical.
3. `Disconnect()` (`:191-213`) → `transport_->disconnect();` keep all status/flag logic.
4. The pump (`Update()`, `:229-1012`): replace

```cpp
Packet *p = peer->Receive();
while (p) { gotMessages=true; switch (p->data[0]) { ... }
            peer->DeallocatePacket(p); p = peer->Receive(); }
```

with

```cpp
jefe::net::TransportEvent ev;
while (transport_->poll(ev)) {
    gotMessages = true;
    switch (evKind(ev)) { ... }
}
```

where the six RakNet `ID_*` cases become `TransportEventType` cases carrying the same bodies (ConnectAccepted → old `ID_CONNECTION_REQUEST_ACCEPTED` body with `serverPeerId_ = ev.peer;` replacing `serverSystemAddress = p->systemAddress;`; ConnectFailed / ServerFull / AlreadyConnected / Disconnected / ConnectionLost likewise — bodies at `:235,:251,:265,:270,:278,:287` verbatim). Concretely: outer `switch (ev.type)` with a `case TransportEventType::Data:` whose body is the old `GFCNETID_*` inner switch on `ev.bytes[0]`, with every `RakNet::BitStream bs(p->data, p->length, false/true/0)` becoming `RakNet::BitStream bs((unsigned char*)ev.bytes.data(), (unsigned int)ev.bytes.size(), false)` (always non-copying `false` — the copies at `:311/:670` were defensive; `ev.bytes` is already an owned copy).
5. Every `peer->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, serverSystemAddress, 0/false)` (23 sites, inventory §4 client table) → `transport_->send(bs.GetData(), (int)bs.GetNumberOfBytesUsed(), serverPeerId_, false);`
6. `getServerSystemAddress()` → `getServerPeerId()` returning `serverPeerId_`.

- [ ] **Step 3: Fix the one external caller of the renamed getter**

`gfcNetworkManager.cpp:371,385,399` call `client.getServerSystemAddress()` into `server.startFXSinc/...`. Task 4 changes the server signatures; to keep THIS task compiling, temporarily update those three calls to `client.getServerPeerId()` and change `gfcnetworkserver.h` signatures `startFXSinc/startLUTSinc/startStackSinc/startPlaylistMerge(SystemAddress, bool)` → `(jefe::net::PeerId, bool)` with a one-line conversion inside their bodies (`SystemAddress sysaddress = /* unpack */`) — Task 4 finishes the job. Simpler alternative if preferred: do Tasks 3+4 as one commit. The subagent may choose either; the build must be green at the commit point.

- [ ] **Step 4: Build + remote test**

Run: `cmake --build build_qt -j8 2>&1 | tail -5 && ./build_qt/jefecheck --remote-test; echo "exit=$?"`
Expected: build success, `exit=0`.

- [ ] **Step 5: Commit**

```bash
git add -A src/
git commit -m "JEF-22: port gfcNetworkClient to ITransport (PeerId replaces SystemAddress)"
```

---

### Task 4: Port `gfcNetworkServer` onto `ITransport`

**Files:**
- Modify: `src/gfcnetworkserver.h`, `src/gfcnetworkserver.cpp`, `src/gfcNetworkManager.h`, `src/gfcNetworkManager.cpp`

**Interfaces:**
- Consumes: Tasks 1–3 (`ITransport`, `getServerPeerId()`).
- Produces: `gfcNetworkServer` public methods `startFXSinc/startLUTSinc/startStackSinc/startPlaylistMerge` take `(jefe::net::PeerId sysaddress, bool broadcast)`; all maps keyed by `jefe::net::PeerId`. `gfcNetworkManager` headers RakNet-free.

- [ ] **Step 1: Rewrite `gfcnetworkserver.h`**

Remove RakNet includes; `#include "gfcTransport.h"` + `<memory>`. Replace `RakPeerInterface* peer;` with `std::unique_ptr<jefe::net::ITransport> transport_;`. Re-key every map (inventory §3): `std::map<jefe::net::PeerId, std::string> nickNameAddressMap;` etc. (`colorAddressMap`, `clientsMissingFXsMap`, `clientsMissingLUTsMap`, `clientsReadyMap`; DELETE the dead `clientsSentPlaylistMap`). Method signatures: `SystemAddress` → `jefe::net::PeerId`.

- [ ] **Step 2: Rewrite `gfcnetworkserver.cpp`**

Same rules as Task 3, plus the server-specific patterns:

1. `start()` → `transport_->startHost((unsigned short)port, password, GFCNET_MAX_CLIENTS)`; `stop()` → `transport_->stopHost(); nickNameAddressMap.clear();`
2. Pump: `ID_NEW_INCOMING_CONNECTION`→`PeerConnected`, `ID_DISCONNECTION_NOTIFICATION`→`PeerDisconnected`, `ID_CONNECTION_LOST`→`PeerLost`, bodies verbatim (`:607,:165,:128`) with `p->systemAddress` → `ev.peer`.
3. Targeted sends (`p->systemAddress, 0/false`) → `transport_->send(bs.GetData(), (int)bs.GetNumberOfBytesUsed(), ev.peer, false);`
4. Broadcast-to-all sends (`UNASSIGNED_SYSTEM_ADDRESS, true` — `:158,:209,:568,:592,:598,:670,:766,:932`) → `transport_->send(..., jefe::net::kInvalidPeerId, true);`
5. Exclude-sender forwards (`:803`: `BitStream bs(p->data, p->length, false); Send(..., p->systemAddress, true)`) → `transport_->send(ev.bytes.data(), (int)ev.bytes.size(), ev.peer, true);` (verbatim forward needs no BitStream at all).
6. `:687` (`GFCNETID_NEWPEERINSESSION` to `p->systemAddress, true` — RakNet semantics: broadcast-except-target) → `transport_->send(..., ev.peer, true);` — PRESERVE the flag exactly; it excludes the new client, matching handleNewPlayer firing on existing peers only.
7. `startFXSinc/startLUTSinc/startStackSinc/startPlaylistMerge(PeerId, bool)`: keep each body's exact target/broadcast combination (inventory §4: `startFXSinc` swaps in broadcast-all via `kInvalidPeerId` when `broadcast`, the other three pass the peer + flag through unchanged — preserve the asymmetry, it is load-bearing for all-except-that-peer semantics). No-arg overloads used `peer->GetSystemAddressFromIndex(0)`: replicate by having the no-arg overload call the parameterized one with the first entry of `nickNameAddressMap` (or `kInvalidPeerId` if empty) — CHECK all call sites of the no-arg overloads first (`grep -n "startFXSinc()\|startLUTSinc()\|startStackSinc()\|startPlaylistMerge()" src/gfcNetworkManager.cpp src/gfcnetworkserver.cpp`) and verify each is only ever invoked in broadcast mode; if so the seed address is irrelevant (RakNet broadcast-except-addr where addr is "some client" vs "index 0" — behavior differs only when index-0 client should have received it; preserving legacy exactly means seeding with the internal server-client's peer id, which IS index 0 on the server. Use `nickNameAddressMap` iteration order only if it matches; otherwise store the internal client's PeerId at `GFCNETID_NICKNAMESEND` time when `isServerClient` — flag any doubt to the reviewer rather than guessing).
8. `peer->CloseConnection(p->systemAddress, true)` (`:639`) → `transport_->closePeer(ev.peer, true);`
9. `ConnectionCount()` → `transport_->connectionCount();`
10. `gfcNetworkManager.{h,cpp}`: remove the four leftover RakNet includes (`gfcNetworkManager.h:9-14`); finish the `:371,:385,:399` call sites from Task 3 Step 3.

- [ ] **Step 3: Build + remote test**

Run: `cmake --build build_qt -j8 2>&1 | tail -5 && ./build_qt/jefecheck --remote-test; echo "exit=$?"`
Expected: build success, `exit=0` (server saw mirrored play, peak participants >= 1).

- [ ] **Step 4: Commit**

```bash
git add -A src/
git commit -m "JEF-22: port gfcNetworkServer + gfcNetworkManager to ITransport; PeerId-keyed peer maps"
```

---

### Task 5: Seam audit, dead-code sweep, docs

**Files:**
- Modify: `developer_notes.md` (append §31), possibly small fixes from audit.

- [ ] **Step 1: Grep audit — RakNet confined to the transport TU**

Run:
```bash
grep -rn "RakPeerInterface\|SystemAddress\|RakNetworkFactory\|MessageIdentifiers\|UNASSIGNED_SYSTEM_ADDRESS\|ID_CONNECTION\|ID_NEW_INCOMING\|ID_DISCONNECTION\|ID_NO_FREE\|ID_MODIFIED_PACKET\|ID_ALREADY_CONNECTED" \
  src/gfcnetworkclient.h src/gfcnetworkclient.cpp src/gfcnetworkserver.h src/gfcnetworkserver.cpp \
  src/gfcNetworkManager.h src/gfcNetworkManager.cpp src/gfcNetworkStructures.h
```
Expected: zero hits. (`BitStream`/`StringCompressor` hits are allowed — JEF-23 scope.) Fix any stragglers.

- [ ] **Step 2: Dead-include sweep**

In the two ported .cpp files, remove now-unused includes (`Rand.h`, `RakNetStatistics.h`, `GetTime.h`, `RakAssert.h`, `RakSleep.h` — inventory §1 found them all dead even before the port). Build must stay green.

- [ ] **Step 3: Full gates**

Run: `cmake --build build_qt -j8 2>&1 | tail -3 && ./build_qt/jefecheck --remote-test; echo "remote=$?"`
Expected: `remote=0`.
Also run the unrelated-regression spot checks: `./build_qt/jefecheck --cc-test; echo $?` and `./build_qt/jefecheck --playlist-test; echo $?` — both 0.

- [ ] **Step 4: Document the seam**

Append to `developer_notes.md` a short §31 "Transport seam (JEF-22)": ITransport/PeerId contract, poll-based pump preserved, RakNet confined to `gfcRakNetTransport.{h,cpp}`, BitStream intentionally app-side until JEF-23, the `:687` broadcast-except-target subtlety, and the startXSinc broadcast asymmetry. ~30 lines, match the file's existing tone.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "JEF-22: seam audit, dead RakNet includes removed, developer_notes §31"
```

---

## Self-review notes (done at plan time)

- Spec coverage: Phase 1 scope only — interface (T1), impl (T2), client port (T3), server+manager port (T4), audit/docs (T5). Serialization untouched (JEF-23). ✔
- The `:687` broadcast-except-target and startXSinc no-arg-overload seed address are the two real behavior-fidelity traps; both called out with instructions to verify call sites rather than guess. ✔
- Type consistency: `getServerPeerId()` (T3) matches T4's manager call sites; `startFXSinc(jefe::net::PeerId, bool)` consistent T3-step3/T4. ✔
- No test framework exists; TDD is approximated by the harness gate at every commit. ✔
