---
title: Linux build script and docs fixes
summary: The Linux build script works on a fresh machine again, and the build guide, manual and quick start match the app.
release: "website-1.1.0"
areas: [build]
pr: 129
commit: eb8a318c4d1e712a4ae0af82934ee77f03a786b0
date: 2026-07-04
---

## What's new

This is a cleanup of the build script and the documentation, checked against the application source and the CI workflows.

The Linux build script had two real bugs:

- It didn't install FreeType's development package, which the build requires, so configuring could fail on a fresh machine.
- It created the development symlink from a folder that no longer exists. It now links `src/FX` to `FX` and `src/fonts` to `fonts`, so a build from the source tree finds its effects and fonts.

The build guide's dependency lists had drifted from what CI installs. Linux now lists `libilmbase-dev`. The Windows MSYS2 list installs the full MinGW toolchain and adds Imath and fmt. The macOS release download is now listed as a `.dmg`.

The user manual and quick start had several shortcuts and details that no longer matched the app:

- Scrolling through LUTs is `L+Up` / `L+Down`, not `Ctrl+Up/Down`.
- Reloading a track from a point is `Alt`+left-click (right-click opens the options menu).
- Color corrections are listed in the order they are applied: LUT, Exposure, Gamma, Brightness, Saturation, Contrast.
- 16bpc loads 16-bit integer images, stored as half-float on the GPU, not "16-bit floating point".
- Preferences no longer claims `Ctrl+P`, which is the Playlist Manager.
- Resetting all viewports is `Ctrl+Alt+R`, and Swing playback is `0`.

## How to use it

On Ubuntu, run the script from the repository root:

```
./scripts/build_linux.sh
```

## Under the hood

The fix adds the missing package and points the symlinks at the current source layout:

{{quote scripts/build_linux.sh#L30-L39 "FreeType is now installed, and the FX and fonts symlinks point at src/"}}
