---
title: FFmpeg bundled in source builds
summary: Build JefeCheck from source and get working video export, including when you launch the app from the Finder.
release: "2.0"
areas: [build, render/video]
pr: 118
commit: 837df5f0cfbf080d51751dc4eb8815a6a626c165
date: 2026-06-27
---

## What's new

Release packages already ship with FFmpeg for video export. Builds made from source did not. On macOS that meant video export failed when the app was opened from the Finder or the Dock: apps launched that way get a minimal `PATH` that usually leaves out Homebrew, so JefeCheck could not find an FFmpeg you had installed yourself.

Now the CMake build downloads the same static FFmpeg build that the release packaging uses and copies it into the build output. On macOS it goes into the app's `Contents/Resources/` folder; on Linux and Windows it goes next to the `jefecheck` binary. These are the places JefeCheck checks for a bundled FFmpeg, so video export works from a fresh source build.

## How to use it

Nothing changes in the usual build:

```bash
cmake -B build && cmake --build build
```

- The download happens once, the first time you configure. It is cached in the build directory.
- To use an FFmpeg binary you already have, for example to build offline, pass `-DJEFECHECK_FFMPEG_BIN=/path/to/ffmpeg` or set the `JEFECHECK_FFMPEG_BIN` environment variable.
- To skip bundling, pass `-DJEFECHECK_BUNDLE_FFMPEG=OFF`.
- If the download fails, CMake prints a warning and the build carries on. Video export then falls back to an FFmpeg on your `PATH`.

## Under the hood

At configure time CMake picks the binary for the host platform and downloads it into the build tree, unless a local binary was supplied or the file is already there:

{{quote CMakeLists.txt#L111-L150 "Configure-time fetch of a static FFmpeg, with an override and an off switch"}}

After each build, a post-build step copies the cached binary next to the executable. On macOS a similar step copies it into the app bundle's Resources folder:

{{quote CMakeLists.txt#L208-L220 "Linux and Windows builds copy FFmpeg next to the binary"}}
