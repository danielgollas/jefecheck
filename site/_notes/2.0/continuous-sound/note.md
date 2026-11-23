---
title: Continuous sound, offset tracks and a rolling cache switch
summary: Sound stays smooth under Show every frame, offset tracks that no longer overlap keep playing, and the rolling cache can be turned off to fill memory from the in point.
release: "2.0"
areas: [viewer, preferences]
pr: 136
commit: a6c897fc97a908e692534a335bd05fb6f3385c62
date: 2026-11-12
---

## What's new

**Smooth sound under Show every frame.** With *When loading can't keep up* set to *Show every frame*, each frame used to play its own 1/24 s of sound, fading in and out. The sound dipped 24 times a second, even at full speed. Now, once the next quarter second is loaded, the sound leads and the picture follows it one frame at a time. If the next frame is still loading, the sound pauses, the picture holds, and per-frame sound returns until the buffer has refilled.

**Offset tracks no longer stop playback.** Loop priority *Shortest* loops over the frames that every track has. If you dragged one track so far that the tracks no longer overlap, that range was empty and playback stuck on one frame. In that case *Shortest* now behaves like *Longest*.

**A rolling cache switch.** The new circular-arrow button sits at the right of the Timeline transport row. The setting is remembered between launches and is on by default.

- **On:** frames load around the playhead and unload behind it.
- **Off:** each track loads from the in point toward the end until memory is full, and keeps what it loaded. Seeking does not move the load. *Shortest* and *Longest* measure what is loaded, so Loop, Bounce and Once work over that range. *Timeline* still uses the in and out points.
- **Alt-click** the timeline ruler or a track lane, or press **Alt+I**, to reload starting from that frame.

There is also a new launch option, `--open-playlist <file.jpl>`, which opens a playlist at startup the way File > Open Playlist does.

## How to use it

1. Load a movie with sound. In **Preferences > General**, set *When loading can't keep up* to *Show every frame* and play a stretch that is already loaded. The sound is continuous.
2. Load two clips and drag track B's lane so the clips do not overlap. With loop priority *Shortest*, playback still runs.
3. Click the circular-arrow button to turn the rolling cache off. Loading fills from the in point and stops when memory is full, and the buffer bars stay put when you seek. Alt-click further along the timeline to start loading from there.

## Under the hood

Under Show every frame the sound only leads while the next frame is loaded. Otherwise the lead stops and the normal frame pacing holds the picture:

{{quote src/gfcplaybackmanager.cpp#L102-L121 "The sound leads one frame at a time"}}

The offset fix is a small check on the loop priority. It compares the latest track start with the earliest track end:

{{quote src/gfcplaybackmanager.cpp#L521-L527 "Shortest falls back to Longest"}}

With the rolling cache off, the cache is given one forward walk from a fixed starting frame. It ignores the playhead, so what loaded first stays:

{{quote src/gfcplaybackmanager.cpp#L1005-L1016 "Load until full, never follow the playhead"}}
