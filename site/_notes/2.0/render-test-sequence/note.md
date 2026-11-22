---
title: Sequence render smoke test
summary: The headless render test now renders a whole in/out range, not just one frame, so multi-frame render bugs are caught.
release: "2.0"
areas: [render]
pr: 114
commit: 630fd3fadebfa1cc0ae949cd99b16cfde3b2403f
date: 2026-06-27
---

## What's new

This is a change to testing, not a new user-facing feature. JefeCheck has a headless render smoke test, started with `--render-test <dir>`, that loads footage and renders it without anyone clicking through the Render dialog. Until now it rendered only the current frame, once in each output format. That never exercised the path a real render takes when it writes a whole range of frames, and a bug that produced black frames in multi-frame renders got past it.

The test now also renders the full in/out range of the timeline as a numbered PNG sequence (`seq_0001.png`, `seq_0002.png`, …) and prints how many frames it wrote. For users this means a sequence render is now checked the same way it is used, so a broken range render is easier to catch before a release.

## How to use it

Run JefeCheck with the flag and an output folder:

```
jefecheck --render-test <output-dir>
```

Besides the single-frame `rendertest_*` files in each format, the folder now also gets a `seq_NNNN.png` file for every frame between the timeline's in and out points. The console prints a line such as `RENDER-TEST seq: 8 frame(s) [1..8]`.

## Under the hood

The new block follows the existing per-format loop. It asks the timeline for its in and out frames and renders that range to PNG with a `seq_` prefix and four-digit padding:

{{quote src/qt/MainWindow_qt.cpp#L1050-L1067 "The smoke test renders the whole in/out range as a numbered PNG run"}}
