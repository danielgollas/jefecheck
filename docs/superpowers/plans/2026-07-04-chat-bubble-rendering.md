# Chat Bubble Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render remote-session chat as alternating-alignment bubbles (self vs. others, phone-width, short HH:MM timestamps, per-user color) coherently in both the networking-tab widget and the GL viewport overlay, with server-assigned per-user colors at join time.

**Architecture:** The chat protocol is unchanged in shape; one additive field (the sender's server-assigned color) is appended to the chat broadcast. The server assigns a distinct color at join (prefer-then-disambiguate). Both render surfaces consume a new structured accessor. The panel replaces its `QTextEdit` with a `QScrollArea` bubble list; the GL overlay reworks only its chat-drawing block into per-message bubbles.

**Tech Stack:** C++20, Qt6 Widgets, RakNet (vendored), immediate-mode OpenGL 2.1 (macOS ARB), `GfcTextRenderer` (FreeType).

## Global Constraints

- **No automated test framework exists.** Verification per task = clean build (`cmake --build build -j8` → `Built target jefecheck`) + `--remote-test` (→ `REMOTE-TEST: participants=2 mirrored_play=1`) where protocol/state is touched + manual two-instance checks for rendering. There is no pytest/gtest; do not invent one.
- **TU separation:** only `src/qt/SequenceLoadBridge_qt.cpp` may `#include` the rendering-chain managers. Qt UI TUs (`RemotePanel_qt.cpp`) use `jefe::qt::*` accessors only.
- **RakNet encode/decode symmetry:** every field added to an encoder MUST be added to its decoder in the same order/type/compression. Chat uses `EncodeString`/`DecodeString` and `Write`/`Read`.
- **GL handles init to 0; no new QThread; overlay draws only inside `paintGL`** (context already current). macOS packed pointer color format is `((r&0xff)<<24)|((g&0xff)<<16)|((b&0xff)<<8)` (top 3 bytes R,G,B).
- **Build dir** is `build/` in the worktree `/Users/dgollas/workspaces/JEF-4-remote-session-qt/jefecheck2`. Run the app binary at `build/jefecheck.app/Contents/MacOS/jefecheck`.
- Commit after each task. End commit messages with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

### Task 1: `shortTime()` helper + `gfcChatLogEntry.color` field

**Files:**
- Modify: `src/gfcNetworkStructures.h` (add field + declare helper)
- Modify: `src/gfcNetworkStructures.cpp` (implement helper)

**Interfaces:**
- Produces: `std::string shortTime(const std::string& asctimeStr)` — returns the `HH:MM` token, or the input unchanged if none found. `gfcChatLogEntry::color` (int, packed RGB, default 0).

- [ ] **Step 1: Add the `color` field to `gfcChatLogEntry`**

In `src/gfcNetworkStructures.h`, in `class gfcChatLogEntry`:

```cpp
class gfcChatLogEntry{
public:
gfcChatLogEntry() : type(0), color(0) {}   // color 0 = unset
unsigned char type;
std::string time;
std::string sender;
std::string message;
int color;   // sender's server-assigned packed-RGB color (0 = unset -> neutral)

std::string getFormattedString();
};
```

(If the class has no existing constructor, add the one above; if it does, add `color(0)` to its init list and the `int color;` member.)

- [ ] **Step 2: Declare `shortTime` in the header**

Near the top-level free-function declarations in `src/gfcNetworkStructures.h` (where `asciiTime` is declared, or add if absent):

```cpp
// Extracts the HH:MM token from an asctime-style string
// ("Jul  4 14:32:56 2026" -> "14:32"). Returns the input unchanged
// if no NN:NN run is found. Never throws.
std::string shortTime(const std::string& asctimeStr);
```

- [ ] **Step 3: Implement `shortTime` in the cpp**

In `src/gfcNetworkStructures.cpp`:

```cpp
std::string shortTime(const std::string& asctimeStr) {
    // Find the first "NN:NN" run and return those 5 chars.
    for (size_t i = 0; i + 4 < asctimeStr.size(); ++i) {
        if (std::isdigit((unsigned char)asctimeStr[i]) &&
            std::isdigit((unsigned char)asctimeStr[i + 1]) &&
            asctimeStr[i + 2] == ':' &&
            std::isdigit((unsigned char)asctimeStr[i + 3]) &&
            std::isdigit((unsigned char)asctimeStr[i + 4])) {
            return asctimeStr.substr(i, 5);
        }
    }
    return asctimeStr;   // fallback: unrecognized format
}
```

Ensure `#include <cctype>` is present at the top of the cpp (add if missing).

- [ ] **Step 4: Build**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"`
Expected: `[100%] Built target jefecheck`, no `error:`.

- [ ] **Step 5: Commit**

```bash
git add src/gfcNetworkStructures.h src/gfcNetworkStructures.cpp
git commit -m "JEF-4: shortTime() helper + gfcChatLogEntry.color field

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Server-side join-time color assignment + distinct palette

**Files:**
- Modify: `src/gfcnetworkserver.h` (declare palette + assign helper)
- Modify: `src/gfcnetworkserver.cpp` (implement + call in NICKNAMESEND and SENDREMOTEPOINTERCOLOR)

**Interfaces:**
- Consumes: existing `std::map<SystemAddress,int> colorAddressMap` and the packed-RGB format.
- Produces: `int gfcNetworkServer::assignColor(int preferred)` — returns a color to store: `preferred` if non-default and unused, else the first free palette color, else `preferred` (dup allowed).

- [ ] **Step 1: Declare the helper**

In `src/gfcnetworkserver.h`, in the `gfcNetworkServer` class private/public section:

```cpp
// Picks a color for a joining/recoloring participant: the preferred color
// when it's non-default and not already in use, otherwise the first unused
// color from a fixed distinct palette, otherwise the preferred color
// (duplicate allowed when the palette is exhausted).
int assignColor(int preferred);
```

- [ ] **Step 2: Implement the palette + assignment**

In `src/gfcnetworkserver.cpp` (near the top, after includes/externs):

```cpp
namespace {
// Distinct, VFX-friendly palette in the packed-RGB format
// ((r&0xff)<<24)|((g&0xff)<<16)|((b&0xff)<<8).
inline int packRGB(int r, int g, int b) {
    return ((r & 0xff) << 24) | ((g & 0xff) << 16) | ((b & 0xff) << 8);
}
const int kColorPalette[] = {
    packRGB(0xE0, 0x83, 0x6C), // coral
    packRGB(0x5B, 0xB0, 0x7A), // green
    packRGB(0x6C, 0x9C, 0xE0), // blue
    packRGB(0xD4, 0xA0, 0x1E), // amber
    packRGB(0xB0, 0x7A, 0xD4), // violet
    packRGB(0x4C, 0xC0, 0xC0), // teal
    packRGB(0xE0, 0x6C, 0xB0), // pink
    packRGB(0xA0, 0xC0, 0x4C), // lime
    packRGB(0xE0, 0xB0, 0x6C), // sand
    packRGB(0x8C, 0x8C, 0xE0), // periwinkle
};
const int kColorPaletteSize = sizeof(kColorPalette) / sizeof(kColorPalette[0]);
// Default "no preference" sentinel: gray (128,128,128), matching gfcStructures.h.
const int kDefaultColor = packRGB(128, 128, 128);
}  // namespace

int gfcNetworkServer::assignColor(int preferred) {
    auto inUse = [this](int c) {
        for (const auto& kv : colorAddressMap)
            if (kv.second == c) return true;
        return false;
    };
    if (preferred != kDefaultColor && !inUse(preferred))
        return preferred;
    for (int i = 0; i < kColorPaletteSize; ++i)
        if (!inUse(kColorPalette[i]))
            return kColorPalette[i];
    return preferred;   // palette exhausted -> allow a duplicate
}
```

- [ ] **Step 3: Use it in the NICKNAMESEND handler**

In `src/gfcnetworkserver.cpp`, in the `GFCNETID_NICKNAMESEND` case, replace:

```cpp
            colorAddressMap[p->systemAddress]=theColor;
```

with:

```cpp
            colorAddressMap[p->systemAddress]=assignColor(theColor);
```

- [ ] **Step 4: Use it in the mid-session color-change handler**

In `src/gfcnetworkserver.cpp`, in the `GFCNETID_SENDREMOTEPOINTERCOLOR` case, replace:

```cpp
						colorAddressMap[p->systemAddress]=theInt;
```

with:

```cpp
						colorAddressMap[p->systemAddress]=assignColor(theInt);
```

- [ ] **Step 5: Build + remote-test**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"`
Expected: `Built target jefecheck`.
Run: `./build/jefecheck.app/Contents/MacOS/jefecheck --remote-test 2>&1 | tail -1`
Expected: `REMOTE-TEST: participants=2 mirrored_play=1`

- [ ] **Step 6: Commit**

```bash
git add src/gfcnetworkserver.h src/gfcnetworkserver.cpp
git commit -m "JEF-4: server assigns per-user color at join (prefer then disambiguate)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Carry the assigned color on the chat broadcast + `getNickName()`

**Files:**
- Modify: `src/gfcnetworkserver.h`, `src/gfcnetworkserver.cpp` (add color param to `sendChatMessage`, pass at call site, encode)
- Modify: `src/gfcnetworkclient.h`, `src/gfcnetworkclient.cpp` (decode color; add `getNickName()`)

**Interfaces:**
- Consumes: `colorAddressMap`, `gfcChatLogEntry.color` (Task 1).
- Produces: `std::string gfcNetworkClient::getNickName()`; chat entries now populated with `color`.

- [ ] **Step 1: Add a color parameter to the server encoder**

In `src/gfcnetworkserver.h`, update the declaration:

```cpp
void sendChatMessage(unsigned char type, std::string sender, std::string message, int color = 0);
```

In `src/gfcnetworkserver.cpp`, update the definition to encode the color after the message:

```cpp
void gfcNetworkServer::sendChatMessage(unsigned char type, std::string sender, std::string message, int color) {
    RakNet::BitStream outBS2;
    outBS2.Write ( ( unsigned char ) GFCNETID_CHATBROADCASTMESSAGE );
    outBS2.Write((unsigned char) type);
    StringCompressor::Instance()->EncodeString ( asciiTime(true).c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS2 ); //time
    StringCompressor::Instance()->EncodeString ( sender.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS2 ); //sender
    StringCompressor::Instance()->EncodeString ( message.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS2 ); //message
    outBS2.WriteCompressed ( ( int ) color ); //sender's assigned color (0 for system msgs)
    peer->Send ( &outBS2,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true );
}
```

- [ ] **Step 2: Pass the sender's color at the user-message call site**

In `src/gfcnetworkserver.cpp`, in the `GFCNETID_CHATMESSAGE` case, replace:

```cpp
            sendChatMessage(messageType,nickNameAddressMap[p->systemAddress],tempChatMessage);
```

with:

```cpp
            sendChatMessage(messageType,nickNameAddressMap[p->systemAddress],tempChatMessage,colorAddressMap[p->systemAddress]);
```

(Server-generated system/join/leave/load calls to `sendChatMessage` keep the default `color = 0` — those render centered without a per-user color.)

- [ ] **Step 3: Decode the color on the client**

In `src/gfcnetworkclient.cpp`, in the `GFCNETID_CHATBROADCASTMESSAGE` case, after the `message` DecodeString + `tmpEntry.message=message;`, add:

```cpp
            int chatColor = 0;
            bs.ReadCompressed ( chatColor );
            tmpEntry.color = chatColor;
```

(Insert this before `chatLog.push_back ( tmpEntry );`.)

- [ ] **Step 4: Add `getNickName()` to the client**

In `src/gfcnetworkclient.h`, in the public section:

```cpp
std::string getNickName() { return nickName; }
```

- [ ] **Step 5: Build + remote-test**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"`
Expected: `Built target jefecheck`.
Run: `./build/jefecheck.app/Contents/MacOS/jefecheck --remote-test 2>&1 | tail -1`
Expected: `REMOTE-TEST: participants=2 mirrored_play=1`

- [ ] **Step 6: Commit**

```bash
git add src/gfcnetworkserver.h src/gfcnetworkserver.cpp src/gfcnetworkclient.h src/gfcnetworkclient.cpp
git commit -m "JEF-4: carry sender color on chat broadcast + client getNickName()

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Structured chat entries (manager + bridge accessor)

**Files:**
- Modify: `src/gfcnetworkmanager.h`, `src/gfcnetworkmanager.cpp` (add `chatEntries()`)
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp` (add `ChatEntry` + `remoteChatEntries()`)

**Interfaces:**
- Consumes: `client.getChatLog()`, `client.getNickName()` (Task 3), `shortTime()` (Task 1).
- Produces: `struct jefe::qt::ChatEntry { std::string sender, message, timeHHMM; int type; bool isSelf; int color; }` and `std::vector<ChatEntry> jefe::qt::remoteChatEntries()`.

- [ ] **Step 1: Add a manager-level structured accessor**

In `src/gfcnetworkmanager.h`, public section:

```cpp
struct ChatEntryData { std::string sender, message, timeHHMM; int type; bool isSelf; int color; };
std::vector<ChatEntryData> chatEntries();
```

In `src/gfcnetworkmanager.cpp`:

```cpp
std::vector<gfcNetworkManager::ChatEntryData> gfcNetworkManager::chatEntries() {
    std::vector<ChatEntryData> out;
    const std::string me = client.getNickName();
    for (auto& e : client.getChatLog()) {
        ChatEntryData d;
        d.sender   = e.sender;
        d.message  = e.message;
        d.timeHHMM = shortTime(e.time);
        d.type     = e.type;
        d.isSelf   = (!e.sender.empty() && e.sender == me);
        d.color    = e.color;
        out.push_back(d);
    }
    return out;
}
```

- [ ] **Step 2: Define the bridge `ChatEntry` struct**

In `src/qt/SequenceLoadBridge_qt.h`, in `namespace jefe::qt`, near the other remote structs:

```cpp
struct ChatEntry {
    std::string sender;
    std::string message;
    std::string timeHHMM;
    int  type;     // GFCNETMESSAGETYPE_NORMAL / _SYSTEM / _LOAD
    bool isSelf;
    int  color;    // packed RGB, 0 = unset
};
std::vector<ChatEntry> remoteChatEntries();
```

- [ ] **Step 3: Implement the bridge accessor**

In `src/qt/SequenceLoadBridge_qt.cpp`, in `namespace jefe::qt` (near `remoteChatLog`):

```cpp
std::vector<ChatEntry> remoteChatEntries() {
    std::vector<ChatEntry> out;
    for (auto& d : networkManager.chatEntries()) {
        ChatEntry e;
        e.sender   = d.sender;
        e.message  = d.message;
        e.timeHHMM = d.timeHHMM;
        e.type     = d.type;
        e.isSelf   = d.isSelf;
        e.color    = d.color;
        out.push_back(e);
    }
    return out;
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"`
Expected: `Built target jefecheck`.

- [ ] **Step 5: Commit**

```bash
git add src/gfcnetworkmanager.h src/gfcnetworkmanager.cpp src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "JEF-4: structured remoteChatEntries() accessor (sender/time/self/color)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Networking-tab chat bubble widget

**Files:**
- Modify: `src/qt/RemotePanel_qt.h` (swap `chatLogView_` for a scroll area + content layout; declare bubble builder)
- Modify: `src/qt/RemotePanel_qt.cpp` (build the scroll area; incremental bubble append; QSS)

**Interfaces:**
- Consumes: `jefe::qt::remoteChatEntries()` (Task 4), `GFCNETMESSAGETYPE_*`.
- Produces: bubble rendering in the panel; `shownChatLines_` still tracks appended count.

- [ ] **Step 1: Header — replace the chat view members**

In `src/qt/RemotePanel_qt.h`, add includes if missing (`class QScrollArea; class QVBoxLayout;`). Replace `QTextEdit* chatLogView_` usage by adding:

```cpp
    QScrollArea* chatScroll_ = nullptr;     // replaces chatLogView_
    QWidget*     chatContent_ = nullptr;    // scroll content
    QVBoxLayout* chatLayout_ = nullptr;     // top-packed; bubbles appended here
```

Keep `chatLogView_` declared for now only if other code references it; otherwise remove it. Declare the builder:

```cpp
    // Appends one chat message as a bubble row (alternating alignment,
    // per-user color, HH:MM). System/LOAD types render centered without a bubble.
    void appendChatBubble(const jefe::qt::ChatEntry& e);
```

Add `#include "SequenceLoadBridge_qt.h"` to the .cpp (not the .h) for the `ChatEntry` type in the signature — instead, to avoid pulling the bridge header into the panel header, forward-declare in the header:

```cpp
namespace jefe::qt { struct ChatEntry; }
```

- [ ] **Step 2: Build the scroll area in the constructor**

In `src/qt/RemotePanel_qt.cpp`, replace the `chatLogView_` creation block (lines ~222-225) with:

```cpp
    chatScroll_ = new QScrollArea(this);
    chatScroll_->setObjectName("remote.chatscroll");
    chatScroll_->setWidgetResizable(true);
    chatScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatScroll_->setFrameShape(QFrame::NoFrame);
    chatContent_ = new QWidget(chatScroll_);
    chatContent_->setObjectName("remote.chatcontent");
    chatLayout_ = new QVBoxLayout(chatContent_);
    chatLayout_->setContentsMargins(6, 6, 6, 6);
    chatLayout_->setSpacing(6);
    chatLayout_->addStretch(1);   // keeps bubbles packed to the top
    chatScroll_->setWidget(chatContent_);
    chatLogBox_ = nullptr;
```

Replace the later `sessionLayout->addWidget(chatLogView_, 1);` (line ~265) with:

```cpp
    sessionLayout->addWidget(chatScroll_, /*stretch*/ 1);
```

- [ ] **Step 3: Implement `appendChatBubble`**

In `src/qt/RemotePanel_qt.cpp` add (and `#include "SequenceLoadBridge_qt.h"`, `#include <QScrollArea>`, `#include <QScrollBar>`, `#include <QFrame>`, `#include <QLabel>`, `#include <QHBoxLayout>`, `#include <QVBoxLayout>` at top):

```cpp
namespace {
QString colorToHex(int packed) {   // packed = (r<<24)|(g<<16)|(b<<8)
    if (packed == 0) return QStringLiteral("#9a9a9a");  // neutral fallback
    int r = (packed >> 24) & 0xff, g = (packed >> 16) & 0xff, b = (packed >> 8) & 0xff;
    return QString::asprintf("#%02x%02x%02x", r, g, b);
}
}  // namespace

void RemoteDialog_Qt::appendChatBubble(const jefe::qt::ChatEntry& e) {
    // System / load messages: centered dim line, no bubble.
    if (e.type != 0 /* GFCNETMESSAGETYPE_NORMAL */) {
        auto* sys = new QLabel(QString::fromStdString(e.message), chatContent_);
        sys->setObjectName("chat_system");
        sys->setAlignment(Qt::AlignHCenter);
        sys->setWordWrap(true);
        chatLayout_->insertWidget(chatLayout_->count() - 1, sys);  // before stretch
        return;
    }

    auto* row = new QWidget(chatContent_);
    auto* rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 0);

    auto* bubble = new QFrame(row);
    bubble->setObjectName("chat_bubble");
    bubble->setProperty("self", e.isSelf);
    bubble->setMaximumWidth(320);
    auto* bLay = new QVBoxLayout(bubble);
    bLay->setContentsMargins(9, 5, 9, 6);
    bLay->setSpacing(1);

    const QString name = e.isSelf ? QStringLiteral("You")
                                  : QString::fromStdString(e.sender);
    auto* header = new QLabel(bubble);
    header->setObjectName("chat_header");
    header->setText(QString("<span style='color:%1'>%2</span> · %3")
                        .arg(colorToHex(e.color), name.toHtmlEscaped(),
                             QString::fromStdString(e.timeHHMM)));
    auto* msg = new QLabel(QString::fromStdString(e.message), bubble);
    msg->setObjectName("chat_message");
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bLay->addWidget(header);
    bLay->addWidget(msg);

    if (e.isSelf) { rowLay->addStretch(1); rowLay->addWidget(bubble); }
    else          { rowLay->addWidget(bubble); rowLay->addStretch(1); }

    chatLayout_->insertWidget(chatLayout_->count() - 1, row);  // before stretch
}
```

- [ ] **Step 4: Feed bubbles from `refreshConnectionState`**

In `src/qt/RemotePanel_qt.cpp`, replace the chat-log refresh line (was `appendNewLogLines(chatLogView_, jefe::qt::remoteChatLog(), shownChatLines_);`, line ~389) with incremental bubble append + smart auto-scroll:

```cpp
    {
        const auto entries = jefe::qt::remoteChatEntries();
        const int total = (int)entries.size();
        if (total < shownChatLines_) {   // log reset (reconnect): rebuild
            QLayoutItem* it;
            while ((it = chatLayout_->takeAt(0)) != nullptr) {
                if (it->widget()) it->widget()->deleteLater();
                delete it;
            }
            chatLayout_->addStretch(1);
            shownChatLines_ = 0;
        }
        auto* bar = chatScroll_->verticalScrollBar();
        const bool atBottom = bar->value() >= bar->maximum() - 4;
        for (int i = shownChatLines_; i < total; ++i)
            appendChatBubble(entries[i]);
        shownChatLines_ = total;
        if (atBottom) {
            // Defer so the layout has sized the new bubbles before we scroll.
            QMetaObject::invokeMethod(this, [bar]{ bar->setValue(bar->maximum()); },
                                      Qt::QueuedConnection);
        }
    }
```

- [ ] **Step 5: Add bubble QSS**

In `src/qt/RemotePanel_qt.cpp`, append to the `kRemoteStyle` string (before its closing `)"`):

```cpp
QFrame#chat_bubble { background:#242424; border:1px solid #3d3d3d; border-radius:8px; }
QFrame#chat_bubble[self="true"] { background:#2e2620; border-color:#7a4a1e; }
QLabel#chat_header { color:#9a9a9a; font-size:10px; }
QLabel#chat_message { color:#dcdcdc; font-size:12px; background:transparent; border:none; }
QLabel#chat_system { color:#6a6a6a; font-size:10px; font-style:italic; }
QScrollArea#remote.chatscroll, QWidget#remote.chatcontent { background:#1a1a1a; border:none; }
```

- [ ] **Step 6: Build**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"`
Expected: `Built target jefecheck`.

- [ ] **Step 7: Manual check (note in commit)**

Launch two instances (`open -n build/jefecheck.app` twice, or run the binary directly twice), host on one, join on the other, exchange messages. Confirm in the **networking tab**: your messages right-aligned/accent, theirs left/neutral, name colored + `HH:MM`, width capped, long text wraps, join/leave lines centered, auto-scrolls to newest.

- [ ] **Step 8: Commit**

```bash
git add src/qt/RemotePanel_qt.h src/qt/RemotePanel_qt.cpp
git commit -m "JEF-4: chat bubble widget in the networking tab (alt-align, color, HH:MM)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: GL viewport overlay bubbles

**Files:**
- Modify: `src/gfcnetworkmanager.cpp` (rework only the "DRAW CHAT STUFF" block in `draw`)

**Interfaces:**
- Consumes: `client.getChatLog()`, `client.getNickName()`, `shortTime()`, `gfc_gl_font/gfc_gl_height/gfc_gl_draw`, `textRenderer().textWidth()`, `gl_rectf`, `GFCNETMESSAGETYPE_*`, `chatFadeCounter`, `chatOpacity`, `gChatMode`, `gChatTextString`, `chatPosOffset`, `blinkerOn`, `chatDisplayLines`, `chatLineOffset`.
- Produces: per-message bubble overlay.

- [ ] **Step 1: Add a word-wrap helper**

In `src/gfcnetworkmanager.cpp`, in an anonymous namespace near the top (add `#include <sstream>` and ensure the text-renderer header is included):

```cpp
namespace {
std::vector<std::string> wrapToWidth(const std::string& text, int maxW) {
    std::vector<std::string> lines;
    std::istringstream iss(text);
    std::string word, cur;
    while (iss >> word) {
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (cur.empty() || (int)textRenderer().textWidth(trial.c_str()) <= maxW)
            cur = trial;
        else { lines.push_back(cur); cur = word; }
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.push_back("");
    return lines;
}
inline void unpackRGB(int packed, float& r, float& g, float& b) {
    if (packed == 0) { r = g = b = 0.6f; return; }   // neutral
    r = ((packed >> 24) & 0xff) / 255.0f;
    g = ((packed >> 16) & 0xff) / 255.0f;
    b = ((packed >>  8) & 0xff) / 255.0f;
}
}  // namespace
```

- [ ] **Step 2: Replace the chat-draw block**

In `src/gfcnetworkmanager.cpp` `gfcNetworkManager::draw`, replace the body of the `if ( chatFadeCounter>0 || gChatMode!=0 )` block (the projection setup stays; the string-building + single `gfc_gl_draw` are what change) with the bubble layout below. Keep the existing `glPushAttrib`, ortho/modelview push, `glEnable(GL_BLEND)`, `glBlendFunc`, and the matching pops.

```cpp
        const float alpha = chatFadeCounter > chatOpacity ? chatOpacity : chatFadeCounter;
        gfc_gl_font(FL_HELVETICA, chatFontSize);
        const int lineH   = (int)gfc_gl_height();
        const int pad     = 6;
        const int margin  = 12;
        const int gap     = 8;
        const int maxW    = (int)(0.6 * w);
        const std::string me = client.getNickName();

        std::vector<gfcChatLogEntry> log = client.getChatLog();
        int logSize = (int)log.size();
        int begin = logSize - chatDisplayLines - chatLineOffset;
        if (begin < 0) begin = 0;
        int end = logSize - chatLineOffset;
        if (end > logSize) end = logSize;

        // Layout bottom-up: y is the current baseline stack cursor from the bottom.
        int y = -h / 2 + margin;

        // Typing bubble first (lowest), if composing.
        if (gChatMode == 1) {
            std::string typed = gChatTextString;
            if (blinkerOn) typed.insert(std::min((size_t)chatPosOffset, typed.size()), "|");
            if (typed.empty()) typed = " ";
            std::vector<std::string> lines = wrapToWidth(typed, maxW - 2 * pad);
            int bw = 0;
            for (auto& l : lines) bw = std::max(bw, (int)textRenderer().textWidth(l.c_str()));
            bw += 2 * pad;
            int bh = (int)lines.size() * lineH + 2 * pad;
            int x = w / 2 - margin - bw;   // self = right
            glColor4f(0.18f, 0.15f, 0.12f, alpha);      // accent-tint
            gl_rectf(x, y, bw, bh);
            textRenderer().setColor(0.91f, 0.72f, 0.52f, alpha);
            std::string joined; for (auto& l : lines) { joined += l; joined += "\n"; }
            gfc_gl_draw(joined.c_str(), x + pad, y + pad, bw - 2 * pad, bh - pad,
                        FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
            y += bh + gap;
        }

        // Messages, newest just above the typing bubble, older stacking upward.
        for (int i = end - 1; i >= begin; --i) {
            const gfcChatLogEntry& e = log[i];
            if (e.type != GFCNETMESSAGETYPE_NORMAL) {
                // System/load: centered dim single line.
                std::string s = e.message;
                int tw = (int)textRenderer().textWidth(s.c_str());
                int x = -tw / 2;
                textRenderer().setColor(0.5f, 0.5f, 0.5f, alpha);
                gfc_gl_draw(s.c_str(), x, y, tw + 4, lineH,
                            FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
                y += lineH + gap;
                continue;
            }
            const bool self = (!e.sender.empty() && e.sender == me);
            std::string header = (self ? "You" : e.sender) + " \xC2\xB7 " + shortTime(e.time); // "·"
            std::vector<std::string> lines = wrapToWidth(e.message, maxW - 2 * pad);
            int bw = (int)textRenderer().textWidth(header.c_str());
            for (auto& l : lines) bw = std::max(bw, (int)textRenderer().textWidth(l.c_str()));
            bw += 2 * pad;
            int bh = (int)(lines.size() + 1) * lineH + 2 * pad;   // +1 header line
            int x = self ? (w / 2 - margin - bw) : (-w / 2 + margin);

            if (self) glColor4f(0.18f, 0.15f, 0.12f, alpha);       // accent-tint
            else      glColor4f(0.14f, 0.14f, 0.14f, alpha);       // neutral
            gl_rectf(x, y, bw, bh);

            float cr, cg, cb; unpackRGB(e.color, cr, cg, cb);
            textRenderer().setColor(cr, cg, cb, alpha);
            gfc_gl_draw(header.c_str(), x + pad, y + bh - pad - lineH, bw - 2 * pad, lineH,
                        FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
            textRenderer().setColor(0.86f, 0.86f, 0.86f, alpha);
            std::string joined; for (auto& l : lines) { joined += l; joined += "\n"; }
            gfc_gl_draw(joined.c_str(), x + pad, y + pad, bw - 2 * pad,
                        (int)lines.size() * lineH,
                        FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
            y += bh + gap;
        }
```

Note: the coordinate system inside this block is the ortho `[-w/2,w/2] × [-h/2,h/2]` with y **up** and `gl_rectf(x, y, width, height)` drawing upward from `(x, y)`. Header is drawn at the top of the bubble (`y + bh - pad - lineH`), message lines below it.

- [ ] **Step 3: Remove the now-dead flat-string code**

Delete the old `chatDisplayString` construction, the `linesToDraw` counting loop, the old full-width `chatTextBG` `gl_rectf`, and the final single `gfc_gl_draw(chatDisplayString...)` within this block — they are replaced by Step 2. Leave the surrounding `glPushAttrib`/matrix push/pop, `glEnable(GL_BLEND)`, `glBlendFunc`, `glDisable(GL_BLEND)`, and pops intact.

- [ ] **Step 4: Build**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"`
Expected: `Built target jefecheck`.

- [ ] **Step 5: Manual check (note in commit)**

Two instances, exchange chat during a session. Confirm on the **viewport overlay**: newest at the bottom stacking upward, self right/accent + others left/neutral, name in the user's color + `HH:MM`, width capped and wrapping, the composing line shows as a self bubble with a blinking cursor, system/load lines centered, and the whole stack fades as before.

- [ ] **Step 6: Commit**

```bash
git add src/gfcnetworkmanager.cpp
git commit -m "JEF-4: GL overlay chat bubbles (alt-align, per-user color, HH:MM, typing bubble)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Final regression + push

- [ ] **Step 1: Full build + remote-test**

Run: `cmake --build build -j8 2>&1 | grep -iE "error:|Built target jefecheck$"` → `Built target jefecheck`.
Run: `./build/jefecheck.app/Contents/MacOS/jefecheck --remote-test 2>&1 | tail -1` → `REMOTE-TEST: participants=2 mirrored_play=1`.

- [ ] **Step 2: Two-client same-color check**

Launch two instances, set both to the same preferred pointer color in Preferences, host+join, send a message from each. Confirm the two participants render with **different** colors (server disambiguation) and each user's chat color matches their pointer color.

- [ ] **Step 3: Push**

```bash
git push origin HEAD
```

---

## Notes for the implementer

- `GFCNETMESSAGETYPE_NORMAL` is `0` in the enum; the panel task compares `type != 0` and the overlay uses the named constant — both mean the same thing. Prefer the named constant where the network headers are in scope (overlay); the panel only has the bridge `ChatEntry` so `!= 0` is used with a comment.
- The `·` middle-dot in the overlay is written as the UTF-8 bytes `\xC2\xB7`; the text renderer draws UTF-8.
- If `textRenderer().textWidth` returns logical (not framebuffer) pixels while `draw()` works in framebuffer pixels, scale by the device pixel ratio the same way `gfc_gl_measure` does internally — verify against `gfc_gl_height()`'s units at implementation time and keep both in the same space.
- Do not touch the sync-wait box or pointer-drawing parts of `draw()`; only the chat block changes.
