---
title: Cancel a render
summary: Stop a render partway through, and keep using the dialog while frames are written.
release: "2.0"
areas: [render]
pr: 116
commit: 41a7d9d241ab9231f6f170b555f6e7a511610e9d
date: 2026-06-27
---

## What's new

A render no longer locks the Render dialog until the last frame is written. The dialog stays responsive while it works, the progress bar updates after every frame, and you can stop the render whenever you want.

While a render runs, the **Render** button becomes **Cancel**. Click it and the render stops after the frame it is writing. The bar turns amber and reads "Cancelled — N frame(s)", so you know how many frames reached disk. A render that runs to the end still finishes with the green "Done — N frame(s) ✓" bar.

The quadrant, format, frame range and output folder are locked while a render runs. That way the settings on screen always match the files being written. Everything unlocks again when the render finishes or is cancelled.

## How to use it

1. Open **File → Render…** (or press `F6`), set the output and frame range, and click **Render**.
2. To stop, click the same button, now labelled **Cancel**.
3. The frames already written stay in the output folder. Change the settings and render again if you need to.

## Under the hood

Rendering draws with the viewport's OpenGL context, and that context can only be used from the main thread. So instead of moving the render to a worker thread, the dialog renders one frame per step and goes back to the event loop between steps. Each step renders a single frame, updates the bar, and schedules the next one. Because the event loop runs in between, a Cancel click is handled before the next frame starts:

{{quote src/qt/RenderDialog_qt.cpp#L438-L467 "Each step renders one frame, updates progress, then schedules the next step"}}

The button only raises a flag. The next step sees the flag and finishes the render as cancelled:

{{quote src/qt/RenderDialog_qt.cpp#L371-L379 "The Render button doubles as Cancel while a render is running"}}
