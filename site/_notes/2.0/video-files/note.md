---
title: Load video files as frames, with sound
summary: Open .mov and .mp4 movies like a frame sequence, with sound, and let a rolling frame cache play clips of any length within a memory limit.
release: "2.0"
areas: [viewer, playlist, build]
pr: 135
commit: 2c42f976fc2ffa42b8d70a863356acf2be6b087c
date: 2026-11-08
---

## What's new

JefeCheck now opens movie files. ProRes (422 and 4444), H.264 and H.265 in `.mov` and `.mp4` load through every load path: drag and drop, Quick Load, the Load Sequence Manager and playlists. A movie behaves like an image sequence. It has a range, trims, offsets, a filmstrip and notes keyed by frame, and the timeline runs from frame 1 to the last frame.

The picture is decoded in memory and nothing is written to disk. The colour conversion from the movie's YUV to RGB runs on the graphics card and follows the file's own matrix, range and chroma siting. The frame rate is exact (24000/1001 stays 24000/1001, not 23.976), and the status bar shows timecode.

Sound comes with the picture:

- All unmuted tracks are mixed. Each track has a speaker toggle and a waveform on its lane.
- At the movie's own frame rate the sound leads and the picture follows it.
- Stepping or scrubbing plays one frame's worth of sound. Reverse playback is silent.

A **rolling frame cache** lets a long clip play from start to end. Frames load around the playhead and unload behind it, and every track keeps the same timeline frames. The timeline shows what is loaded as a buffer bar.

Two new settings in **Preferences > General**:

- **Memory limit** is the share of RAM the cache may use, measured across the whole system. The default is 90%.
- **When loading can't keep up** chooses between *Keep time* (skip frames to stay in sync) and *Show every frame* (slow down). The default, Automatic, keeps time when a clip has sound and shows every frame when it has none.

Smaller changes in the same PR: the playhead stays where it was after a render, quitting no longer crashes, and the Ubuntu 24.04 build errors are fixed. FFmpeg's libraries are now linked for reading, so JefeCheck is licensed GPL v2 or later. The libav libraries ship inside the macOS and Windows packages. On Linux the package lists them as dependencies. Exporting video still uses the FFmpeg command line.

A bad frame no longer stops playback of an image sequence. Playback goes past it.

## How to use it

1. Drag a `.mov` or `.mp4` onto the viewport, or press **Cmd+O** and pick one.
2. Press **Space** to play. The FPS box shows the clip's own rate and the sound stays in sync.
3. Watch the buffer bar on the timeline. It fills ahead of the playhead and empties behind it.
4. Click far along the timeline. The plate shows "Loading" for a moment, then the frame appears and playback carries on.
5. To cap memory, set **Preferences > General > Memory limit**. To choose what happens when loading is slow, set **When loading can't keep up**.

Loading very large frames is still slow. A 4K ProRes 4444 clip loads at about 13 frames a second, so Keep time skips frames until they have loaded once.

## Under the hood

The reader keeps the frame rate as two integers, a numerator and a denominator, along with the colour facts the GPU needs for the conversion:

{{quote src/gfcMovieReader.h#L14-L17 "What the reader knows about a movie"}}

Whether playback skips frames to keep time is one small decision. With the default setting it depends only on whether any track has sound:

{{quote src/gfcplaybackmanager.cpp#L947-L950 "Keep time or show every frame"}}

The cache budget is the memory limit minus what the rest of the system is using. The cache's own bytes are taken out of the system figure, so the cache does not count itself twice:

{{quote src/gfcFrameCache.cpp#L217-L244 "How much the frame cache may hold"}}
