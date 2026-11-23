---
title: Render output resolution
summary: Render at the source size, at 75%, 50% or 25%, or at any width and height you type in.
release: "2.0"
areas: [render]
pr: 120
commit: 243b7b28d6f9a5bcfd0dbcdc813419890fe95874
date: 2026-06-27
---

## What's new

The Render dialog has a new **Resolution** row that controls the size of the frames it writes. It replaces the old **Scale** box, which did nothing: renders always came out at the source size whatever it said.

The row has a preset menu and two size fields:
- **Source (100%)**, **75%**, **50%** and **25%** fill in the width and height from the plate's source frame. A 877×876 source at 50% becomes 439×438.
- Type a width or height (1 to 16384 pixels) and the menu switches to **Custom**. The two fields are independent, so a custom size can have a different shape from the source.

The size fields are what the render actually uses. When you pick a different quadrant, they are filled in again from that plate's source. Video export uses the same size, which also keeps the size constant when the source frames vary.

## How to use it

1. Open **File → Render…** (`F6`) with footage loaded. The fields start at the source size.
2. Choose a preset, or type the exact width and height you want.
3. Click **Render**.

## Under the hood

The render settings gained an output width and height, where 0 means "use the source". When rendering, the plate sizes its offscreen buffer to that target instead of the source texture. The source image is drawn across the buffer, so it is scaled to the requested size:

{{quote src/gfcPlate.cpp#L1245-L1256 "The render buffer is sized to the requested output, or to the source when none is set"}}

In the dialog, a preset multiplies the active plate's source size by its fraction and fills in the two fields:

{{quote src/qt/RenderDialog_qt.cpp#L392-L405 "Presets compute width and height from the source size"}}
