---
title: Remote review sessions
summary: Host or join a live session where playback, colour, effects and the playlist stay in sync, with chat and remote pointers on the image.
release: "2.0"
areas: [remote]
pr: 128
commit: 67eba1921bde5423ffbd80893cc1bbd0796722f0
date: 2026-07-06
---

## What's new

Remote sessions work again in the Qt version. One person hosts and the others connect. After that, play and pause, colour correction, effects (including adding, removing and reordering them), EXR layer choices and playlist loads are mirrored to everyone in the session.

- A **Remote Session** panel that stays open: Host and Join forms, a live status, the participant list, errors, a chat view, and a collapsible connection log.
- **Chat** shows both in the panel and as bubbles over the image. Each participant gets a colour from the host when they join.
- **Remote pointers:** Shift + left-drag on a plate shows your pointer to the others, as a fading trail drawn in that plate's image space.

## How to use it

1. Open **File → Remote Session…** (or press `F5`).
2. To host, choose **Host**, enter a session name, a port and an optional password, then **Start hosting**. To connect, choose **Join**, enter your nickname, the host's address, port and password, then **Connect**.
3. Load the same media on each machine, through the Playlist for example.
4. In the viewport, press `Return` to start a chat message, `Return` again to send it, or `Esc` to cancel it. You can also type in the panel's message box.
5. **Leave** disconnects.

## Under the hood

The network is serviced on every tick of the main window's timer, even when nothing is playing. When a message arrives, the panel and the viewport update without anyone touching the receiving machine.

{{quote src/qt/MainWindow_qt.cpp#L317-L325 "The network is pumped on every tick and triggers a repaint when state arrives"}}

While a chat message is being typed, the viewport takes the keystrokes for it, so they don't trigger plate shortcuts.

{{quote src/qt/GlViewport_qt.cpp#L412-L433 "Return starts chat when connected; Return sends and Esc cancels"}}

Pointer updates share a channel with playback and colour messages. So a position that hasn't changed is dropped, and updates are capped at about 60 a second.

{{quote src/qt/SequenceLoadBridge_qt.cpp#L2006-L2016 "Remote pointer updates are de-duplicated and throttled to ~60 Hz"}}
