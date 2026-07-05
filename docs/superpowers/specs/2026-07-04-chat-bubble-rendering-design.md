# Chat bubble rendering — design spec

**Date:** 2026-07-04
**Branch:** `JEF-4-remote-session-qt`
**Status:** Approved (design), pending implementation plan

## Problem

Remote-session chat renders in two places, both crude and incoherent with each
other:

1. **GL viewport overlay** (`gfcNetworkManager::draw`) — builds one flat line
   per message via `gfcChatLogEntry::getFormattedString()` →
   `( Jul  4 14:32:56 2026 ) Alice says: <msg>`, left-aligned, wrapped across
   the **full viewport width**, on a full-width dark bar, fading via
   `chatFadeCounter`. The `asctime()`-derived timestamp is verbose ("too long").
2. **Networking-tab widget** (`RemoteDialog_Qt::chatLogView_`, a `QTextEdit`) —
   appends `sender: message`, full width, no timestamp.

Goal: redesign both to a single **chat-bubble** design language — alternating
alignment (self vs. others), phone-width bubbles (not full screen), short
`HH:MM` timestamps, per-user color — so the panel and the overlay read as the
same feature.

## Decisions (from brainstorming)

- **Layout:** alternate bubbles (iMessage-style). Self right-aligned + accent
  tint; others left-aligned + neutral. Max bubble width ≈ `min(60% of
  container, 320px)`.
- **Timestamp:** short `HH:MM` (24h), small/dim, in each bubble's header. No
  protocol change — reformatted on display.
- **Scope:** both surfaces in this pass, sharing the design language.
- **Per-user color:** reuse each participant's remote-pointer color as the
  sender-name accent, tying chat identity to pointer identity.

Out of scope (YAGNI): message editing, reactions, read receipts, history
persistence, search. GL-overlay corner rounding is a cheap approximation, not
pixel-perfect.

## Data model + plumbing

The chat entry already carries everything needed: `gfcChatLogEntry { unsigned
char type; std::string time; std::string sender; std::string message; }` with
`type ∈ { GFCNETMESSAGETYPE_NORMAL, _SYSTEM, _LOAD }`.

New/changed interfaces:

- **`std::string gfcNetworkClient::getNickName()`** — expose the client's own
  nickname (currently private `nickName`) so self-detection is possible.
- **`shortTime(const std::string& asctimeStr) -> std::string`** — shared helper
  (free function in `gfcNetworkStructures.{h,cpp}`, reused by both surfaces).
  Extracts the `HH:MM` token from the `asctime`-format string (find the first
  `NN:NN` digit run; return it). Falls back to the raw string if no match.
- **`jefe::qt::ChatEntry`** (new struct in `SequenceLoadBridge_qt.h`):
  `{ std::string sender; std::string message; std::string timeHHMM; int type;
  bool isSelf; int color; }` where `color` is the sender's pointer-color index
  (or a default when unknown).
- **`std::vector<ChatEntry> jefe::qt::remoteChatEntries()`** — replaces the
  flattened `remoteChatLog()`/`chatLogLines()` path for the panel. Built in
  `gfcNetworkManager` from `client.getChatLog()`, setting `isSelf =
  (entry.sender == client.getNickName())` and `timeHHMM = shortTime(entry.time)`.
  `chatLogLines()` may be kept only if still needed elsewhere; otherwise removed.
- Per-user color lookup reuses the same nickname→color association the pointer
  path uses. If a message's sender has no known color, fall back to a neutral
  accent. (Self always uses the accent tint regardless.)

Self-detection edge case: the host's loopback client has the server's name as
its nickname, so the host's own messages compare equal to `getNickName()` and
render self-side correctly.

## Surface 1 — networking-tab widget (`RemotePanel_qt`)

Replace the `QTextEdit chatLogView_` with a scrolling bubble list:

- `QScrollArea` (objectName for QSS) containing a content `QWidget` whose
  `QVBoxLayout` is top-packed with a trailing stretch, so bubbles grow downward.
- Each message → a **row**: a `QHBoxLayout` with a stretch on the opposite side
  (`stretch | bubble` for self/right; `bubble | stretch` for others/left).
- **Bubble** = a `QFrame` (`objectName "chat_bubble"`, property `self`/`other`)
  with rounded corners and `maximumWidth` ≈ `min(60% of viewport, 320px)`,
  containing:
  - a **header** `QLabel` (`"Name · 14:32"`, small, dim; name colored with the
    sender's per-user color) — omitted or reduced for consecutive same-sender
    messages is *not* required (grouping was not selected), so show it per bubble;
  - a **message** `QLabel` with `wordWrap = true`.
  - Self bubbles: accent-tinted background (`--accent-tint-bg`), header may read
    "You". Other bubbles: `--surface-2` background.
- **System/LOAD** messages (`type != NORMAL`): a centered, dim, borderless label
  (e.g. "Alice loaded plateX", "— Bob joined —"), no bubble.
- **Incremental update:** keep the existing `shownChatLines_` cache — append
  only new entries' bubble rows (do not clear+rebuild). Auto-scroll the
  `QScrollArea` to the bottom on new messages unless the user has scrolled up
  (detect via the vertical scrollbar being near max before append).
- Styling lives in the panel's scoped stylesheet (`kRemoteStyle` /
  `jefecheck_dark.qss` tokens) so it matches the discreet theme.

The chat **input** (`chatInput_` line edit + Send) is unchanged.

## Surface 2 — GL viewport overlay (`gfcNetworkManager::draw`)

Rework only the "DRAW CHAT STUFF" block (the sync-wait box and pointer drawing
are untouched). Instead of one wrapped flat string:

- Take the last N visible entries (respecting the existing `chatDisplayLines` /
  `chatLineOffset` scroll state).
- For each entry, laying out **bottom-up** from the lower-left/right:
  - `isSelf` from `sender == client.getNickName()`.
  - `bubbleMaxW = min(0.6 * w, cap)`.
  - Word-wrap `message` to `bubbleMaxW` using the text renderer's width
    measurement (`gfc_gl_width` or equivalent; verify the exact symbol during
    implementation), producing display lines.
  - `bubbleH = headerLineH + lines * lineH + 2*padding`; `bubbleW = min(maxW,
    widestLine + 2*padding)`.
  - `x`: left margin for others; right-aligned (`w - margin - bubbleW`) for self.
  - Draw a filled rounded-ish rect (accent tint for self / dark neutral for
    others) at `chatFadeCounter`-scaled opacity, with a thin border in the
    discreet language. Rounding = cheap corner approximation (skip/clip a few
    corner pixels), not a perfect arc.
  - Draw the dim `Name · HH:MM` header (name in the user's pointer color), then
    the wrapped message lines.
- **Typing indicator** (`gChatMode == 1`): render the in-progress
  `gChatTextString` (+ blinking cursor at `chatPosOffset`) as a self-side bubble
  pinned at the bottom.
- **System/LOAD**: a centered dim line (no bubble).
- Opacity for the whole stack continues to follow `chatFadeCounter` vs
  `chatOpacity` exactly as today.

Coordinates: `draw(w, h)` receives framebuffer pixels (caller passes
`width()*dpr, height()*dpr`), and the ortho projection is already set to
`[-w/2, w/2] × [-h/2, h/2]`; bubble math uses that space as the current code
does.

## Error handling / robustness

- `shortTime()` returns the raw string on parse failure — never throws, never
  empties a valid timestamp.
- Empty `message` or `sender` render as empty bubbles/labels, not crashes.
- Unknown sender color → neutral fallback.
- Panel bubble widgets are parented into the scroll content widget (Qt-owned);
  the incremental cache guarantees no duplicate rows and a single rebuild on a
  log reset (source shrank), mirroring the existing `appendNewLogLines` logic.

## Testing / verification

- `--remote-test` remains green (no protocol/state change; only rendering +
  read-side accessors).
- Build clean on the touched TUs.
- Two-instance manual check, on **both** the panel and the overlay:
  - self messages right/accent, others left/neutral;
  - bubble width capped (~phone width), long messages wrap inside the bubble;
  - timestamps read `HH:MM`;
  - sender name color matches that user's pointer color;
  - system/load messages centered and dim;
  - panel auto-scrolls to newest but preserves position when scrolled up;
  - overlay fades as before and the typing line shows as a self bubble.

## Affected files

- `src/gfcNetworkStructures.{h,cpp}` — `shortTime()` helper.
- `src/gfcnetworkclient.{h,cpp}` — `getNickName()`.
- `src/gfcnetworkmanager.{h,cpp}` — structured chat entries; overlay bubble draw.
- `src/qt/SequenceLoadBridge_qt.{h,cpp}` — `ChatEntry`, `remoteChatEntries()`.
- `src/qt/RemotePanel_qt.{h,cpp}` — bubble-list widget replacing `chatLogView_`.
- `src/qt/theme/jefecheck_dark.qss` and/or the panel's scoped style — bubble QSS.
