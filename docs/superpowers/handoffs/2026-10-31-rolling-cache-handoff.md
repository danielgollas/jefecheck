# Handoff: rolling frame cache (load ahead of the playhead, evict behind it)

Date: 2026-10-31. Written at the end of the video-loading project (PR #135, `feature/video-loading` → `qt-experimental`).

This is a starting brief for the next project, not a design. The owner has not been interviewed about it yet. Start with a design conversation (`superpowers:brainstorming`), then write a spec in `docs/superpowers/specs/`.

## The problem in one paragraph

Today every track decodes its whole frame range into GPU textures once, front to back, and stops as soon as the RAM check says memory is full. Nothing is ever unloaded, so whatever didn't fit simply isn't there. A 4K ProRes 4444 frame is about 66 MB as RGBA16F, so a 16 GB Mac holds roughly 250 frames (about 10 s). A one-minute dailies clip is 1,440 frames. The owner wants long clips to be reviewable end to end. The fix is to keep loading ahead of the playhead and unload the frames least likely to be needed (least recently used / furthest behind), for movies and image sequences alike. The owner raised it during the video work: "I don't think we've implemented a 'keep loading ahead of the playhead and unload using an LRU'". Video loading deliberately left it out as a separate project.

## What exists today (verified on `feature/video-loading` at `bc00fa4`)

**The load model** (`src/gfcSequence.cpp`)
- `loadSequence()` runs on a per-track loader thread. It is one sequential pass over `[startFrame, endFrame]`, the loop at ~line 1106. Each iteration checks `memoryManager.withinLimits()` (~1114). Once it fails, the remaining frames only get `savedParams` stored (~1131). That is enough to load them later, but nothing ever does.
- Decoded frames go into `rawFrames`, a queue capped by `sett.maximumFramesInQueue` (default 5; **0 is not unlimited**, the wait at ~978 blocks after one frame). The GL main thread drains it in `generateTextures()` (~763), which uploads textures and pushes the index onto `loadedFrames`, a `std::queue<int>`. The loader thread blocks on a condition variable while the queue is full.
- Everything assumes **the loaded frames are one contiguous run**:
  - `rangeBegin`/`rangeEnd` come from `loadedFrames.front()/back()` (~821-843).
  - `getLoadedFrameCount()` returns `loadedFrames.size()` (~2306).
  - `isEmpty()` uses `loadedFrames.empty()` (~2385).
  - Hold-edge mode returns `frames[loadedFrames.front()/back()]` (~1585-1589).
  - The "HOLDING LAST" label indexes `frames[loadedFrames.size()-1]` (~1695).
  - The timeline's loaded fill (`TrackTimelineState::firstLoadedFrame` + `loadedCount`, `src/qt/SequenceLoadBridge_qt.h:1017-1031`; developer_notes §14) draws one contiguous bar.

  An evicting cache breaks every one of these.
- `getFrame(frameNo, forceLoad)` (~1538) just indexes `frames[]`. With `forceLoad` and an unloaded frame it calls `forceLoad(frame)` (~1501), which loads synchronously on the calling (GL) thread. Render uses this. Movies use their own audio-off reader for forced loads (`forcedMovieReader`, ~893, added in the video final-review fixes), so forced reads don't disturb the loader's audio.
- `loadForStreaming()` (~1475) is an **empty `while(true)` stub** with comments describing exactly this feature. Nothing calls it.
- `freeFrames(n)` (~364) pops the oldest entries off `loadedFrames` and deletes their textures. It is only reachable from a dead FLTK `infinitePlaybackToggle` path (~620-640, `#ifdef JEFECHECK_USE_FLTK`). That path freed 5 frames from all four tracks at once "to maintain coherence between tracks". It is prior art from the original author, not working code.

**Memory accounting** (`src/gfcMemoryManager.{h,cpp}`)
- `setLimit(percentage)`, `withinLimits()`, `getFreeRAM()`, `getTotalRAM()`. It checks free system RAM against a percentage. It knows nothing about bytes per frame or VRAM. On Apple Silicon, unified memory makes RAM a reasonable proxy, but not on discrete GPUs (Linux/Windows).

**Movies** (from the video project; developer_notes §38)
- `gfcMovieReader` seeks to any frame in about 12 ms (1080p ProRes/H.264; seek to keyframe + decode forward). Sequential reads are cheaper. The reader is thread-safe, and each track's loader holds its own `shared_ptr` to it.
- Audio is decoded **during loading** into a per-track `gfcAudioClip` (48 kHz stereo float, about 23 MB per minute). Frames that are never loaded have no audio. This interacts with eviction: either audio is decoded for the whole clip independently of picture (cheap, and probably the right call), or audio gets evicted too. Today `appendMovieAudio` assumes decode order equals append order, with a 2 s look-ahead cap and silence fill for gaps.
- Thumbnails are captured at decode time (developer_notes §15) and capped at about 2,000 per sequence. They survive frame eviction only if they stay separate from the texture.

**Related unmerged work**
- `feature/mac-upload-perf` (`832e209..cd830c5`, pushed, not merged) has a texture pool, PBO/mapped-buffer staging, IOSurface binding on macOS, and a Mesa CI upload benchmark. A review on 2026-10-24 blocked it:
  - a staging buffer handed to two decode threads;
  - `releaseMemory()` → `abandon()` freeing a buffer another thread still held;
  - unbounded pool memory (512 MiB per pool, `drainPool()` never called);
  - doubled pixel memory for queued frames.

  A rolling cache needs texture reuse almost by definition, so decide early whether to rebase on, salvage, or replace that branch.
- The 4K 4444 load-speed gap (13–17 fps loading vs 30–37 fps decode-only at about 1 Gb/s) points at the GPU upload step (developer_notes §38). A rolling cache that must keep up with playback inherits that ceiling, so upload speed and the cache are coupled.

## Questions to settle with the owner (only they can answer)

1. **What "reviewable end to end" means.** Play straight through in real time? Or that every frame can be reached, with a short wait after a big jump? This sets how far ahead to load and whether 4K 4444 needs the upload speed-up first.
2. **What to keep.** A window around the playhead (N frames ahead, M behind)? Pin in/out-point regions? Keep the frames that have notes? Is loop playback of a short range expected to become fully resident and stay there?
3. **Four tracks.** One shared memory budget split across tracks, or per-track? The old FLTK code freed all four tracks together "for coherence". Is that still wanted (e.g. A/B compare), or can each track evict independently?
4. **What the user sees.** How the timeline shows a non-contiguous loaded set (several fill segments? a heat strip?). What a not-yet-loaded frame shows during playback: hold the last frame, black, a "loading" overlay, or does playback wait?
5. **Render/export.** Does it keep forcing synchronous loads (fine for offline render), or go through the cache?
6. **Memory budget.** Keep the RAM-percentage preference, or move to an explicit "cache size" in GB (or both)? On discrete-GPU machines, does VRAM matter?
7. **Scope.** Movies and image sequences in the same project (the design should serve both; the loader is shared)? Remote sessions: does a peer's cache state matter? (Probably not.)

## Things the builder should get right regardless

- **Bookkeeping:** replace `loadedFrames`-as-contiguous-run with a real per-frame state (unloaded / queued / decoding / resident), plus a recency or priority structure. Then audit every caller listed above. The timeline needs a bridge accessor that returns loaded segments, not a count (TU separation, developer_notes §1).
- **Threads:** decoding stays off the GL thread. Texture create/delete and upload stay on it (developer_notes §13 tick split: `tickPlaybackTiming` vs gated `generateTextures`). Eviction (`glDeleteTextures`, or returning a texture to a pool) must happen on the GL thread with the context current.
- **Prefetch direction:** follow the playback direction, including reverse and Bounce. Re-target on a seek, and cancel stale requests rather than draining them. Movie seeks cost about 12 ms, so jump handling for movies is cheap. Image sequences cost one file read per frame.
- **Audio:** keep the audio clock and slices working when picture frames are missing. The audio engine already plays slices by frame index from the clip, independent of textures.
- **Pacing and drag performance:** don't regress playback pacing (§13) or the macOS drag playbook (§5–6) with per-tick work.
- **Tests:** keep the headless self-test convention (`--video-load-test` aggregator, `src/gfcMovieSelfTests.cpp`; test media generated by FFmpeg at test time, `src/qt/MovieTestMedia_qt.cpp`). Useful checks:
  - a cache smaller than the clip still serves every frame, sequentially and after random jumps;
  - the resident-byte budget is never exceeded;
  - eviction order follows the policy;
  - reverse prefetch works;
  - no texture leaks after clear/reload;
  - audio stays in sync across evicted regions.

  Headless suites that wait on the loader set `sett.maximumFramesInQueue = 100000` and restore it afterwards. Call `loadPreview()` after `setFilename` in tests.

## Useful context and commands

- Build trees go on `/Volumes/Archive/jefecheck-builds/<name>`, not the internal disk. The video build is at `/Volumes/Archive/jefecheck-builds/video`.
- Headless: `jefecheck --video-load-test` (18 suites), `--notes-test`, `--render-test`, `--video-perf-test <movie>`, `--video-colour-test <dir>`, `--audio-device-test`.
- Test clips:
  - `/Volumes/Archive/jefecheck-builds/testclips/beep_23976_prores.mov`: 20 s, 1080p, beep every second.
  - `/Volumes/Archive/jefecheck-builds/perfmedia/prores4444_4k*.mov`: synthetic 4K 4444, silent.

  Real client footage has not been measured yet. Getting some is still open.
- An Ubuntu 24.04 Docker build (Qt 6.4 / FFmpeg 6.1) was set up during video Task 14 and builds the branch cleanly; the build trees are `/Volumes/Archive/jefecheck-builds/linux*`.
- Start from `qt-experimental` once PR #135 is merged, or branch off `feature/video-loading` if it isn't yet; this work depends on the movie reader.
- Read first: `CLAUDE.md`, then `developer_notes.md` §1, §13, §14, §15, §38. Also the video design, `docs/superpowers/specs/2026-10-31-video-file-loading-design.md` (Non-goals and Performance sections).
- Open items carried over from the video project: the owner's hands-on check of PR #135; a real-footage `--video-perf-test` run; draft-PR CI on Windows/Linux x64 (CI only triggers on PRs to `main`).
