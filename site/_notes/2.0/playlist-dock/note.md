---
title: Playlist dock with auto-advance
summary: Build a playlist from your current setup or from files, see each item's tracks at a glance, and play straight through it.
release: "2.0"
areas: [playlist]
pr: 126
commit: 512e1ef7080fb86718a2f9cc92055836e0d436b2
date: 2026-06-29
---

## What's new

The Playlist dock now has everything the old interface had, plus automatic playback from one item to the next.

- **Add Current** saves what's on screen right now as an item: every track, its effects and the full playback state.
- **Add Files…** builds an item from files you pick, one track (A–D) per file.
- **Item cards** fold open to show each track's file, frame range, scale, filter, crop and bit depth. **Compact** and **Full paths** change how much each card shows, and both are remembered.
- **Scale override** (100, 50 or 25%) applies one decode scale to every track when an item loads.
- **Auto-advance** moves to the next item and keeps playing when an item that plays once reaches its end. **Loop playlist** goes back to the first item after the last. Auto-advance only reacts to content that was loaded from the playlist, so it won't jump away from something you opened another way.

## How to use it

- Double-click an item or press `Enter` to load it. `Delete` or `Backspace` removes it, and `Shift+↑` / `Shift+↓` moves it. Right-click for the same actions.
- Drag a `.jpl` file onto the list to replace the playlist. Drop media on a card to add tracks to that item, or on empty space to make a new item.
- Turn on **Auto-advance**, set playback to play once, and press play.

## Under the hood

The playback timer watches for the moment playback in play-once mode reaches the end frame, and records it once.

{{quote src/qt/SequenceLoadBridge_qt.cpp#L302-L313 "End of play-once playback is caught once per pass"}}

The panel then loads and plays the next item, or the first one again if Loop is on. It does nothing unless Auto-advance is on and the current content came from the playlist.

{{quote src/qt/PlaylistPanel_qt.cpp#L396-L410 "Advancing to the next item, wrapping when Loop is on"}}

A drop is handled according to what was dropped and where it landed.

{{quote src/qt/PlaylistPanel_qt.cpp#L475-L500 "A .jpl replaces the list; media appends to a card or makes a new item"}}
