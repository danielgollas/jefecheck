---
title: 16-bit PNG and TIFF renders with real precision
summary: Render PNG and TIFF stills at 16 bits per channel and get genuine precision beyond 8 bits, not 8-bit values padded out.
release: "2.0"
areas: [render/stills]
pr: 122
commit: 2bf2b2dc6cb009343b9f3b9cbddecf31b3cd66bc
date: 2026-06-27
---

## What's new

The PNG and TIFF pages of the Render dialog now have an **8-bit / 16-bit** depth choice.

A 16-bit file is only worth having if the pixels in it carry more than 8 bits of information. Until now, the offscreen buffer that renders are drawn into was 8 bits per channel. A 16-bit file would only have held 8-bit values, and EXR output had the same limit. When you ask for a 16-bit PNG or TIFF, or for an EXR, the render now draws into a half-float buffer and reads the result back as floating point. Gradients and graded images keep values that fall between the 8-bit steps.

On-screen drawing and 8-bit renders use the same 8-bit buffer as before.

For the extra precision to mean anything, the source must be high bit depth too. EXR, or footage decoded at 16-half (the default decode depth), is what you want here.

## How to use it

1. Open **File → Render…** (F6).
2. Choose **PNG** or **TIFF** as the format.
3. Set **Depth** to **16-bit**, alongside the format's existing compression control, then render.

## Under the hood

When a render wants 16 bits or EXR, the plate recreates its render buffer as a float buffer. It also tracks the buffer's current format, so the next request rebuilds it again if the format flips back:

{{quote src/gfcPlate.cpp#L1253-L1265 "Choosing a float FBO for 16-bit and EXR renders"}}

The image saver then reads back floats for these formats:

{{quote src/gfcimagesaver.cpp#L37-L45 "16-bit PNG/TIFF and EXR read back as float"}}

It writes 16-bit PNG and TIFF as unsigned 16-bit integers:

{{quote src/gfcimagesaver.cpp#L87-L95 "Choosing the file's pixel type"}}
