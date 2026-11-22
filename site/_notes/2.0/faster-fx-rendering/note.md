---
title: Less work per frame in the FX engine
summary: Effects and colour correction now cost less on every frame, with the same output as before.
release: "2.0"
areas: [fx]
pr: 133
commit: 073e7bfd6db19034c6042f29091dd2e11f9ed8b0
date: 2026-09-14
---

## What's new

This is a performance change and nothing looks different. Every frame JefeCheck draws now skips three costs it used to pay on every plate:

- looking up each effect's shader settings by name,
- making a full copy of every active effect,
- looking up the colour-correction shader's settings by name, which happened on every plate whether or not any effect was on.

In a 2×2 layout, the last one alone came to as many as about 48 name lookups per frame. The test renders for effects, colour correction and baked crop bars give the same numbers before and after the change. The LUT, brightness/contrast/saturation and channel-mask settings use the same mechanism but aren't covered by an automated render test.

A debug message that printed every frame for every plate with a LUT is also gone.

## Under the hood

A shader's uniform locations stay fixed for as long as the program stays linked. So each effect now resolves its locations once, right after linking, and its per-frame `bind()` reuses them.

{{quote src/gfcfx.cpp#L98-L118 "Each FX resolves its uniform locations once, after the program links"}}

The colour-correction "super shader" does the same thing. It clears its cached locations before every relink, so a failed link can't leave old locations pointing at the wrong slots, and fills them in only once the link succeeds.

{{quote src/gfcPlate.cpp#L601-L637 "Super-shader locations reset before a relink and resolved after a successful one"}}

The render loop used to copy each effect, including all of its parameters, before drawing it. It now works on a reference to the effect in the stack.

{{quote src/gfcPlate.cpp#L1378-L1388 "The FX loop takes a reference instead of a per-frame copy"}}
