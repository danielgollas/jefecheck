---
title: Render progress bar
summary: Watch a render fill a progress bar frame by frame, and see clearly when it has finished.
release: "2.0"
areas: [render]
pr: 115
commit: 8f406bf36ccee6c5503f434eb53cddda0ee27788
date: 2026-06-27
---

## What's new

The Render dialog now shows a progress bar while it works. Before, a multi-frame render ran with no feedback: the dialog sat still until every frame was written. Now the bar fills as each frame completes, labelled "Rendering… X/Y", and the status line reads "Rendering frame X of Y…".

When the render ends, the bar stays full, turns green and reads "Done — N frame(s) ✓", so you can tell at a glance that the render finished. It stays that way until you start the next render.

The filename preview also has more room. It shows the first and last output filenames on separate lines, and long output paths used to be clipped to about two lines. The preview is now taller, top-aligned and selectable, so you can copy a path out of it, and the dialog opens a little larger.

## How to use it

1. Open **File → Render…** (or press `F6`).
2. Set the output folder, format and frame range, then click **Render**.
3. Watch the bar below the form. It turns green when the render is done.

The render still runs on the main thread, so there is no Cancel button yet. The dialog repaints between frames instead of freezing.

## Under the hood

The render used to hand the whole frame range to the renderer in one call. It now renders one frame per call and reports progress after each one:

{{quote src/qt/SequenceLoadBridge_qt.cpp#L744-L762 "The frame range is rendered one frame at a time, with a progress callback after each"}}

The dialog's callback turns that count into a percentage, updates the bar and status text, and processes pending events so the bar repaints between frames. When the render ends it switches the bar to the green "Done" style. The style is set explicitly because the native macOS progress bar ignores the text setting.

{{quote src/qt/RenderDialog_qt.cpp#L401-L425 "The dialog drives the bar per frame and leaves it full and green when done"}}
