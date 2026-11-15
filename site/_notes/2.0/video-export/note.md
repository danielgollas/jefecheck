---
title: Video export
summary: Render the active plate straight to an H.264 or H.265 MP4, or a ProRes MOV.
release: "2.0"
areas: [render/video]
pr: 117
commit: 54cf47d555aea5ce1e97a923f16f11256c8b0ba8
date: 2026-06-27
---

## What's new

The Render dialog can now write a movie file as well as still frames. The **Format** list has three new entries: **H.264 (MP4)**, **H.265 (MP4)** and **ProRes (MOV)**. Pick one and JefeCheck renders the frame range, then encodes it into a single file in your output folder.

Video formats share one quality page with two settings:
- **FPS**, from 1 to 120 (default 24).
- **Quality**, from 0 to 100 (default 80). For H.264 and H.265 this sets the compression level. For ProRes it picks the profile: Proxy, LT, Standard or HQ.

Sequences whose frames change size still encode cleanly. Every frame is scaled to fit the first frame's size and padded with black, and odd sizes are rounded up to even numbers, which H.264 and H.265 require.

Encoding is done by FFmpeg. Release builds now include a copy of FFmpeg on macOS, Linux and Windows, so video export works out of the box. If no FFmpeg can be found, the video formats are marked "ffmpeg not found" and the Render button stays disabled.

## How to use it

1. Open **File → Render…** (or press `F6`).
2. Choose a video format and set FPS and Quality.
3. Set the output folder. The **Prefix** field names the file, so `shot010` gives `shot010.mp4`. If it is empty, the file is called `render`.
4. Click **Render**. The bar fills to 50% while frames render and from 50% to 100% while FFmpeg encodes. **Cancel** works during both steps.

## Under the hood

The dialog renders a temporary PNG sequence, runs the FFmpeg command-line tool on it, and deletes the temporary frames afterwards. JefeCheck looks for FFmpeg in four places, in this order: the `JEFECHECK_FFMPEG` environment variable, a saved `Render/ffmpegPath` setting, the copy bundled with the app, and finally the system `PATH`:

{{quote src/qt/VideoEncoder_qt.cpp#L12-L49 "FFmpeg is found from the environment, a setting, the app bundle, then PATH"}}

Each codec gets its own FFmpeg arguments. The Quality value becomes a CRF for H.264 and H.265, and a profile for ProRes:

{{quote src/qt/VideoEncoder_qt.cpp#L113-L138 "Quality maps to a CRF for H.264/H.265 and a profile for ProRes"}}
