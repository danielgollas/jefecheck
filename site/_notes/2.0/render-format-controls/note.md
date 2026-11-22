---
title: More control over JPEG, EXR and video output
summary: Pick JPEG chroma subsampling and progressive encoding, any of the standard EXR compressions, and a target bitrate and encoder preset for H.264/H.265 video.
release: "2.0"
areas: [render]
pr: 121
commit: ebbb5cd45f0463710a9f02b7e72d06fc10f1eff7
date: 2026-06-27
---

## What's new

The Render dialog's per-format settings go further for three kinds of output.

- **JPEG**: next to Quality there is a **Progressive** checkbox and a **Chroma** choice: 4:4:4 (no subsampling, the default), 4:2:2 or 4:2:0. 4:2:0 makes noticeably smaller files. 4:4:4 keeps full colour detail.
- **EXR**: the compression list now has every standard OpenEXR option: None, RLE, ZIPS, ZIP (the default), PIZ, PXR24, B44, B44A, DWAA and DWAB. Before, it offered only ZIP, PIZ and None.
- **Video**: a **Rate** choice switches H.264/H.265 between **Quality** (constant quality, as before) and **Bitrate** (a target in Mbps). A **Preset** list picks the encoder speed/size trade-off, from ultrafast to placebo, with medium as the default. ProRes still picks its profile from the quality value and ignores the bitrate and preset.

## How to use it

1. Open **File → Render…** (F6) and choose an output format.
2. The settings row under the format changes to match it:
   - JPEG: set Quality, tick Progressive if you want it, and pick Chroma.
   - EXR: pick Depth and Compression.
   - H.264 / H.265: set FPS and choose a Rate mode. In **Quality** mode the quality value is used and the Mbps box is greyed out. In **Bitrate** mode it is the other way round. Then pick a Preset.
3. Render as usual.

## Under the hood

The saver turns the dialog's choices into OpenImageIO output attributes. The EXR list is a table whose order matches the dialog's combo, and an index outside the table falls back to ZIP:

{{quote src/gfcimagesaver.cpp#L143-L180 "JPEG subsampling/progressive and the EXR compression table"}}

For video, the preset is passed to FFmpeg on every H.264 encode. A bitrate set in Bitrate mode replaces the constant-quality `-crf` flag with `-b:v`. H.265 follows the same pattern:

{{quote src/qt/VideoEncoder_qt.cpp#L113-L132 "Preset and bitrate-or-CRF for H.264"}}
