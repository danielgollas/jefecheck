# Rolling Frame Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Long clips (movies and image sequences, up to four tracks) are reviewable end to end: frames load ahead of the playhead, the least useful frames are unloaded to make room, any frame can be reached by a jump, and sound stays in sync whatever is loaded.

**Architecture:** A per-track `gfcFrameTable` (plain C++) replaces the contiguous `loadedFrames` run with a per-frame state. One coordinator, `gfcFrameCache`, owns the timeline window, the byte budget (derived from the system-wide RAM percentage) and the priority order; the four per-track loader threads stay and pull one decode job at a time from it. Victims are named by the coordinator at admission and freed on the GL thread in `gfcTrackManager::generateTextures()` before uploads. Movie sound is decoded by a separate audio-only reader thread into a hole-tolerant clip, independent of picture. The Qt timeline draws the resident set as buffer-bar segments through a versioned bridge accessor.

**Tech Stack:** C++20, OpenGL (glad; GL thread only), libav* via `gfcMovieReader`, Qt6 Widgets (timeline, preferences, `QSettings`), headless self-tests (`NAME: pass=N fail=N`), ffmpeg CLI for test media.

**Spec:** `docs/superpowers/specs/2026-11-01-rolling-frame-cache-design.md` (its "Review rulings" section is binding and overrides earlier text where they differ). Background: `docs/superpowers/handoffs/2026-10-31-rolling-cache-handoff.md`. Read `CLAUDE.md` and `developer_notes.md` §1, §5–6, §13, §14, §15, §27, §38, §39 before any task.

## Global Constraints

- Build tree (already configured, RelWithDebInfo): `cmake --build /Volumes/Archive/jefecheck-builds/rcu -j8`. If `/Volumes/Archive/jefecheck-builds/rcu/CMakeCache.txt` is missing: `cmake -S . -B /Volumes/Archive/jefecheck-builds/rcu -DCMAKE_BUILD_TYPE=RelWithDebInfo`. Binary: `/Volumes/Archive/jefecheck-builds/rcu/jefecheck.app/Contents/MacOS/jefecheck` (call it `$J` below; every command is one line, `J=...; $J ...`). Build trees only under `/Volumes/Archive/jefecheck-builds`. Worktree: `/Users/dgollas/projects/jefecheck2/.claude/worktrees/rolling-cache-upload`, branch `feature/rolling-cache-upload`. Never push.
- Never put a newline inside a bash command (use `&&` or `;`).
- After **every** task the app builds and these stay green (run them all before each commit): `$J --video-load-test` (18 suites, exit 0), `$J --notes-test` (13 suites, exit 0), `$J --upload-bench /Volumes/Archive/jefecheck-builds/testclips` (exit 0), `$J --cc-test /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr` (exit 0), `mkdir -p /Volumes/Archive/jefecheck-builds/rcu-tests/colour && $J --video-colour-test /Volumes/Archive/jefecheck-builds/rcu-tests/colour` (exit 0), and the render test: `mkdir -p /Volumes/Archive/jefecheck-builds/rcu-tests/render /Volumes/Archive/jefecheck-builds/rcu-tests/config && printf '[%%General]\nopenLoadWindowAtStartup=0\n' > /Volumes/Archive/jefecheck-builds/rcu-tests/config/jefecheck.ini && $J --config-dir /Volumes/Archive/jefecheck-builds/rcu-tests/config --render-test /Volumes/Archive/jefecheck-builds/rcu-tests/render --open-file /Volumes/Archive/jefecheck-builds/testclips/beep_23976_prores.mov` (exit 0, output files written). From Task 1 on, also `$J --cache-test` (exit 0, every suite `fail=0`). Test clips: `/Volumes/Archive/jefecheck-builds/testclips/beep_23976_prores.mov` (20 s, 1080p, beep each second), `/Volumes/Archive/jefecheck-builds/perfmedia/prores4444_4k*.mov` (4K 4444, silent).
- TU separation (§1): only `src/qt/SequenceLoadBridge_qt.cpp` (and the self-test TUs `src/gfcMovieSelfTests.cpp`, `src/gfcCacheSelfTests.cpp`) include the rendering chain. New plain-C++ headers (`gfcFrameTable.h`, `gfcFrameCache.h`) must not include glad, Qt or libav.
- GL work (texture create/upload/delete, returning textures to the pools) only on the GL thread with the context current; frame textures are freed only through `gfcFrame::deleteTexture()` (§39). Decoding stays off the GL thread.
- No new per-tick work in the 4 ms playback tick beyond atomic reads/stores and a condition-variable notify (§5–6, §13). Nothing in the tick may walk the frame tables.
- Preferences: `sett` fields + `QSettings` keys in `qt_prefs_persist.{h,cpp}` (§27), object names `preferences.<section>.<field>.<role>`, mirrored in `tests/ui/jefecheck/locators.py`.
- Tests: headless, `NAME: pass=N fail=N`, media generated at test time with the ffmpeg CLI, budget injected in bytes, slow decoder injected; suites that wait on the loader set `sett.maximumFramesInQueue = 100000` and restore it. New suites go into `--cache-test` (Task 1 adds the flag).
- Commit after each task; every commit message ends with the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Memory budget: one frame of overshoot per track per admission is acceptable; unbounded growth is not. Pool bytes (staging, texture pool, IOSurface pool) count against the budget.

## Review Focus

1. A track whose footage covers only part of the timeline (large offset or trim): timeline frames with no footage on that track cost nothing and never block the window. Pinned to CACHE-TRACKS (Task 6).
2. A clip that fits entirely in the budget behaves exactly as today: every frame loads, nothing is evicted, no "Loading" label ever shows. Pinned to CACHE-SETTLE (Task 5).
3. A decode error mid-clip (`truncated.mov`): the failed frame is never re-admitted and the loader does not spin on it. Pinned to the loader-error check in Task 5.
4. Lowering the memory-limit preference while frames are loaded unloads nothing by itself; the smaller budget bites at the next admission. Pinned to CACHE-BUDGET (Task 5).
5. Shrinking the in/out loop to a few frames and widening it again: the window follows the limits; nothing is starved by leftovers far ahead. Pinned to CACHE-DIRECTION (Task 6).

## File map

| File | Responsibility |
|---|---|
| `src/gfcMemoryManager.{h,cpp}` | System memory figures in bytes (`usedBytes`, `totalBytes`); limit from `sett` |
| `src/gfcFrameTable.{h,cpp}` (new) | Per-track per-frame state, bytes, pins, segments, version |
| `src/gfcFrameCache.{h,cpp}` (new) | Coordinator: window, budget, priority, admission, victims, generation |
| `src/gfcSequence.{h,cpp}` | Table owner; pull-loop loader; eviction; display rule; audio thread |
| `src/gfcTrackManager.{h,cpp}` | Victim drain before uploads; retarget plumbing; residency queries |
| `src/gfcPlaybackManager.{h,cpp}` | Transport → cache; lag policy; pre-play buffer |
| `src/gfcAudioClip.{h,cpp}`, `src/gfcMovieReader.{h,cpp}` | Hole-tolerant clip; audio-only reader mode |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}` | Segment/version accessors; env budget; cache-test GL |
| `src/qt/TimelinePanel_qt.{h,cpp}` | Buffer-bar segments |
| `src/qt/PreferencesWindow_qt.cpp`, `src/qt/qt_prefs_persist.cpp` | Two new preferences |
| `src/gfcCacheSelfTests.{h,cpp}` (new), `src/qt/MovieTestMedia_qt.{h,cpp}`, `src/main_qt.cpp` | `--cache-test` suites, media, flag |

`CMakeLists.txt` globs `src/*.cpp` and `src/qt/*.cpp`, so new files need no CMake edit (re-run cmake once so the glob picks them up: `cmake -S . -B /Volumes/Archive/jefecheck-builds/rcu`).

---

### Task 1: Memory measurement in bytes, `--cache-test` scaffold, MEMORY-MEASURE

**Files:**
- Modify: `src/gfcMemoryManager.h`, `src/gfcMemoryManager.cpp` (macOS block ~lines 215–275)
- Modify: `src/gfcStructures.h` (gfcSettings ctor ~line 283 and fields ~line 320), `src/qt/qt_prefs_persist.cpp` (`loadPreferences` Engine block ~line 30, `writePreferences` ~line 125)
- Modify: `src/qt/MovieTestMedia_qt.h`, `src/qt/MovieTestMedia_qt.cpp`
- Create: `src/gfcCacheSelfTests.h`, `src/gfcCacheSelfTests.cpp`
- Modify: `src/main_qt.cpp` (next to `hasVideoLoadTest` ~line 225 and the `--video-load-test` block ~lines 696–757)

**Interfaces:**
- Produces: `uint64_t gfcMemoryManager::usedBytes()`, `uint64_t gfcMemoryManager::totalBytes()` (both lock + `updateData()`), `countInactive` initialised to `false`; `setLimit`/`getLimit`/`withinLimits` kept. `int sett.memoryLimitPercent` (default 90, key `Engine/memoryLimitPercent`). `bool jefe::qt::testmedia::generateCacheMedia(const QString& dir, QString* err)`. `int memoryMeasureSelfTest(const std::string& mediaDir)` and the `--cache-test` runner in `main_qt.cpp` (same shape as `--video-load-test`: `setAudioOutputEnabled(false)`, `initializeRenderingChain()`, `setupOffscreenTestGL()`, generate video media + cache media into `jefe_cache_test_<pid>`, run suites, `removeRecoverySession()`, exit 0/2/3).
- Test media written by `generateCacheMedia` (320×180 unless noted, 24 fps): `cache_beep_24.mp4` (240 frames, h264, testsrc2 picture, sound = silence with a 100 ms 1 kHz beep at the start of every second — the `flashbeep_23976.mov` recipe at 24/1, AAC); `cache_silent_24.mp4` (240 frames, no audio stream); `cache_seq.%04d.png` (120 PNG frames, `-f image2`, numbered 0001…0120); `cache_big_24.mp4` (48 frames, 1920×1080, h264, silent).

- [ ] **Step 1: Write the failing test** — `memoryMeasureSelfTest` in `src/gfcCacheSelfTests.cpp`, printing `MEMORY-MEASURE: pass=N fail=N`:
  - `totalBytes() > 1<<30` and `0 < usedBytes() < totalBytes()`.
  - Allocate and `memset` 256 MiB; `usedBytes()` rises by at least 128 MiB (two samples 50 ms apart; take the max); free it.
  - Load `cache_big_24.mp4` into track A at `GFC_16HALF` (`sett.maximumFramesInQueue = 100000`, `loadPreview()`, `startLoadingSequence(0)`, pump `trackManager.generateTextures()` until `getLoadedFrameCount() == 48` or 20 s), measure the delta of `usedBytes()` from before the load; expected = 48 × 1920 × 1080 × 8 bytes. Print `MEMORY-MEASURE: gl_visible=yes|no` (yes when delta ≥ 50 % of expected); this line is informational and does not fail the suite (ruling 2). Assert `getLoadedFrameCount() == 48`. `unloadAndClear()`, restore the queue cap.
  - `gfcMemoryManager::countInactive == false` at startup.
- [ ] **Step 2: Add `--cache-test` to `main_qt.cpp`** (`hasCacheTest`, runner block) and `generateCacheMedia`; run `$J --cache-test` → fails to link (`usedBytes` undefined).
- [ ] **Step 3: Implement** `usedBytes()`/`totalBytes()`: macOS via `host_statistics64(HOST_VM_INFO64, vm_statistics64_data_t)` with used = (active + wired + compressor_page_count) × pagesize and total from `HW_MEMSIZE`; Linux from `/proc/meminfo` used = MemTotal − MemFree − Cached (keep the parser); Windows `ullTotalPhys − ullAvailPhys`. `updateData()` sets `freeRAM/totalRAM` from the same figures so `withinLimits()` keeps working; the constructor initialises `countInactive=false` and `percentageToUse=90` on every platform; from Task 4 on the coordinator reads `sett.memoryLimitPercent` directly and `setLimit`/`withinLimits` are legacy.
- [ ] **Step 4: Add `sett.memoryLimitPercent`** (ctor default 90; load/write under `Engine/memoryLimitPercent`, clamped 50–100 on load).
- [ ] **Step 5: Run** `cmake -S . -B /Volumes/Archive/jefecheck-builds/rcu && cmake --build /Volumes/Archive/jefecheck-builds/rcu -j8 && $J --cache-test` → `MEMORY-MEASURE: pass=5 fail=0`, exit 0. Run the full green list from Global Constraints.
- [ ] **Step 6: Commit** — `feat(cache): memory figures in bytes and the --cache-test harness`.

---

### Task 2: Per-frame state table replaces `loadedFrames`; every caller audited

**Files:**
- Create: `src/gfcFrameTable.h`, `src/gfcFrameTable.cpp`
- Modify: `src/gfcSequence.h`, `src/gfcSequence.cpp` (`freeFrames` 364, `generateTextures` 763–850, `initializeSequence` 1027, `loadSequence` 1068–1200, `maxFramesReached` 1415, `clearSequence` 1435, `loadForStreaming` 1475, `getFrame` 1545, `frameReadyForRender` 1625, `getFilenameatFrame` 1633 case 3, `getRangeStart/End` 1823, `clearRawQueue` 1852, `getLoadedFrameCount` 2313, `isEmpty` 2391, `setHoldMode` 2405)
- Modify: `src/gfcframe.h`, `src/gfcframe.cpp` (`generateTexture` ~344: record `textureBytes`)
- Modify: `src/gfcTrackManager.h`, `src/gfcTrackManager.cpp` (`getFirstFirstLoaded` … `getLastLastLoaded` 325–380)
- Modify: `src/qt/SequenceLoadBridge_qt.h` (`TrackTimelineState` ~1017), `src/qt/SequenceLoadBridge_qt.cpp` (`getTrackTimelineState` 2098)
- Modify: `src/gfcMovieSelfTests.cpp` (AUDIO-FORCED-LOAD keeps using `freeFrames(2)`; `seq->getRangeEnd()>=3` at ~line 700 becomes `seq->getLoadedFrameCount()>=4`)
- Test: `src/gfcCacheSelfTests.cpp` (`frameStateSelfTest`)

**Interfaces:**
- Produces (`gfcFrameTable.h`, plain C++, internally mutex-guarded, every method thread-safe):
  ```cpp
  enum class gfcFrameState : uint8_t { Unloaded, Decoding, Resident, Evicting, Failed };
  class gfcFrameTable {
  public:
    void reset(int frameCount);                       // all Unloaded, bytes 0, pins cleared, ++version
    int size() const;
    gfcFrameState state(int i) const;
    bool setDecoding(int i, size_t reservedBytes);    // only from Unloaded; false otherwise
    void setResident(int i, size_t bytes);            // ++version
    bool setEvicting(int i);                          // only from Resident; releases its bytes now; false otherwise
    void setUnloaded(int i);                          // any state; ++version if it was Resident/Evicting
    void setFailed(int i);                            // from Decoding
    void setPinned(int i, bool on);  bool pinned(int i) const;   // never a victim
    void setOnScreen(int i);         int onScreen() const;       // -1 none; never a victim
    int residentCount() const;  size_t residentBytes() const;  size_t reservedBytes() const;  // reserved = Decoding
    uint64_t version() const;
    void segments(std::vector<std::pair<int,int>>& out) const;  // inclusive 0-based runs of Resident|Evicting
    int firstResident() const;  int lastResident() const;      // -1 when none
  };
  ```
- Produces (`gfcSequence`): `gfcFrameTable& frameTable()`, `int residentCount() const`, `uint64_t loadedVersion() const`, `void loadedSegments(std::vector<std::pair<int,int>>& out) const` (0-based source indices), `void evictFrame(int index)` (GL thread: `frames[index].deleteTexture()`, `loaded=false`, delete `theImageLoader`, keep `thumbnail`, `frameTable_.setUnloaded(index)`), `bool framePending(int timelineFrame) const` (in range and state ∉ {Resident, Evicting}), `size_t estimatedFrameBytes() const` (preview frame size after scale/crop × bytes per pixel for `params.compressed`: 8-bit 4, 16/half 8, 32-float 16, S3TC 0.5; 0 without a preview), `int trackIndex() const` (`trackID-'A'`). `rangeBegin/rangeEnd` now hold the trim range from `loadFrameRange()` (set in `initializeSequence`, cleared in `clearSequence`), so `getRangeStart()/getRangeEnd()` describe the footage, not what is resident. `getLoadedFrameCount()` = `residentCount()`. `isEmpty()` = `frames.empty()`. `freeFrames(n)` = evict the first n resident frames in index order (kept for AUDIO-FORCED-LOAD). Removed: `loadedFrames`, `maxFramesToLoad`, `maxFramesReached`, `loadForStreaming`, `StreamingThread`, `StreamingThreadStarterFunc`, `cacheTextures`, `streaming`.
- Produces (`gfcFrame`): `size_t textureBytes` (set in `generateTexture` from `sizeX*sizeY` and `info.internalFormat`; 0 when not uploaded).
- Produces (bridge): `TrackTimelineState` gains `uint64_t loadedVersion`; `loadedCount` = resident count; `firstLoadedFrame` = first segment start (1-based, offset folded) or `rangeStart`. New `std::vector<std::pair<int,int>> getTrackLoadedSegments(int trackIdx)` — 1-based inclusive timeline frames, offset folded; callers take it only when `loadedVersion` changed.
- Hold modes: mode 2 (hold edge) uses `rangeBegin/rangeEnd`; `setHoldMode(2)` pins both (`frameTable_.setPinned`), any other mode unpins (ruling 10). `getFilenameatFrame` case 3 uses `rangeEnd`. Pinned frames are loaded by Task 4's coordinator; in this task they are simply marked.

- [ ] **Step 1: Write the failing test** — `frameStateSelfTest` (`FRAME-STATE`):
  - Table alone: `reset(10)`; `setDecoding(3,100)` → state Decoding, `reservedBytes()==100`; `setResident(3,120)` → `residentBytes()==120`, `reservedBytes()==0`, `residentCount()==1`; `setResident(4,..)`, `setResident(7,..)` → `segments()` is `{(3,4),(7,7)}`; `setEvicting(4)` → bytes drop, segments unchanged; `setUnloaded(4)` → `{(3,3),(7,7)}`; version strictly increases across these; `setDecoding(3,..)` from Resident is false; `setFailed` only from Decoding.
  - Sequence (track B, `cache_seq.0001.png`, 8-bit, queue cap 100000): after a full load `getLoadedFrameCount()==120`, `loadedSegments()=={(0,119)}`, `getRangeStart()==0 && getRangeEnd()==119`; `freeFrames(5)` → segments `{(5,119)}`, count 115, `getThumbnail(0).w > 0` still, `frames[0]` texture gone (`getFrame(1,false).loaded==false`); `setHoldMode(2)` → `frameTable().pinned(0) && pinned(119)`; `getFrame(0,false)` (below range) returns `frames[0]` (unloaded now, so `.loaded==false`) and `getFrame(200,false)` returns frame 119 (`.loaded`); `setHoldMode(0)` unpins; `unloadAndClear()` → `isEmpty()`, count 0, version moved.
  - Bridge: `getTrackTimelineState(1).loadedVersion == seq->loadedVersion()`; `getTrackLoadedSegments(1)` after `freeFrames` + offset 10 == `{(16,130)}`.
- [ ] **Step 2: Run** `$J --cache-test` → FRAME-STATE fails to compile/link.
- [ ] **Step 3: Implement** `gfcFrameTable` and the `gfcSequence` audit above. In `generateTextures`: `setResident(idx, textureBytes)` on upload, `setUnloaded`/`setFailed` for a raw frame that is not loaded (`skipped` → Failed, cancelled → Unloaded). In `loadSequence` (still the sequential pass this task): `frameTable_.setDecoding(i, 0)` before `loadFrame`, `setUnloaded` for frames stored with `savedParams` only. `clearRawQueue` → `setUnloaded` per popped frame. `gfcTrackManager::getFirst*/getLast*` keep their bodies (they read `getRangeStart/End`, now the trim). The loader's `memoryManager.withinLimits()` check stays until Task 5 replaces the loop.
- [ ] **Step 4: Run** `cmake -S . -B /Volumes/Archive/jefecheck-builds/rcu && cmake --build /Volumes/Archive/jefecheck-builds/rcu -j8 && $J --cache-test` → `FRAME-STATE: fail=0`; full green list (note the AUDIO-FORCED-LOAD edit).
- [ ] **Step 5: Commit** — `refactor(cache): per-frame state table replaces the contiguous loaded run`.

---

### Task 3: Timeline buffer bar

**Files:**
- Modify: `src/qt/TimelinePanel_qt.h` (`TimelineTracks_Qt` members: add `std::vector<std::pair<int,int>> segments_[4]`), `src/qt/TimelinePanel_qt.cpp` (`refresh()` cache-compare ~line 200, `paintEvent` loaded fill ~lines 268–277, `paintFilmstrip` loaded bar ~lines 436–440)

**Interfaces:**
- Consumes: `TrackTimelineState::loadedVersion`, `jefe::qt::getTrackLoadedSegments(int)` (Task 2).
- Produces: nothing new outside the widget.

- [ ] **Step 1: Implement** — in `refresh()`, include `loadedVersion` in the change comparison and, only when it moved for a track, refetch `segments_[i] = getTrackLoadedSegments(i)`. In both paint modes replace the single `[firstLoadedFrame, firstLoadedFrame+loadedCount-1]` rectangle with one rectangle per segment (same colour `QColor(150,185,150)`, same bar geometry, `std::max(x1-x0, 1)` wide), drawn over the dark lane so it reads like a video player's buffer bar. Drop the `loadedCount` term from the comparison (the version covers it).
- [ ] **Step 2: Verify** — build; `$J --video-load-test`, `$J --cache-test`, `$J --notes-test` green (no widget test exists; the segment data is covered by FRAME-STATE). Launch once and confirm by eye that the lane still fills left to right while loading (gaps only appear once Task 5 evicts): `timeout 15 $J --config-dir /Volumes/Archive/jefecheck-builds/rcu-tests/config --open-file /Volumes/Archive/jefecheck-builds/testclips/beep_23976_prores.mov; true`.
- [ ] **Step 3: Commit** — `feat(timeline): loaded frames drawn as buffer-bar segments`.

---

### Task 4: `gfcFrameCache` coordinator (policy, budget, admission, victims) with a GL-free unit suite

**Files:**
- Create: `src/gfcFrameCache.h`, `src/gfcFrameCache.cpp` (includes `gfcFrameTable.h`, `gfcplaybackmanager.h` for the static `advanceFrame` only, `gfcmemorymanager.h`, `gfcUploadPool.h`, `gfcIOSurface.h`, `gfcStructures.h` for `sett`)
- Test: `src/gfcCacheSelfTests.cpp` (`cachePolicySelfTest`)

**Interfaces (binding for Tasks 5–10):**
```cpp
class gfcFrameCache {
public:
  struct Job    { int track; int sourceIndex; int timelineFrame; uint64_t generation; size_t reservedBytes; };
  struct Victim { int track; int sourceIndex; };
  struct Victims { std::vector<Victim> frames; bool trimPools = false; };
  struct Stats  { uint64_t admitted, evicted, denied, retargets; };

  void attachTrack(int track, gfcFrameTable* table);
  // Footage geometry: timeline frame F maps to source index F-1-offset, valid when firstIndex <= index <= lastIndex.
  // firstIndex < 0 means "no footage" (an empty track). frameBytes is the per-frame estimate (0 = unknown yet).
  void setTrackGeometry(int track, int offset, int firstIndex, int lastIndex, size_t frameBytes);
  void setTrackOffset(int track, int offset);
  void setTrackFrameBytes(int track, size_t frameBytes);   // corrected from the first real upload
  void clearTrack(int track);                               // no footage; wakes its loader
  void setTransport(int playhead, int direction, int loopMode, int startLimit, int endLimit, float fps);
  void retarget(int playhead);          // a jump: ++generation, wakes loaders
  void advance(int playhead);           // a playback step: no generation bump, wakes loaders
  bool nextJob(int track, Job& out, std::chrono::milliseconds wait);   // decode thread; false on timeout/cancel/no job
  void commit(const Job& job, size_t actualBytes, bool decoded);       // decoded=false: setFailed (or setUnloaded when cancelled)
  void cancel(int track);               // makes nextJob return false now and until the next setTrackGeometry
  Victims drainVictims();               // GL thread, before uploads: frames marked Evicting since the last drain
  void pin(int track, int index, bool on);
  void setTestBudget(int64_t bytes);    // >= 0 replaces the derived budget; -1 restores it
  int64_t budgetBytes() const;  int64_t cacheBytes() const;  uint64_t generation() const;  Stats stats() const;
  size_t costPerTimelineFrame() const;  // sum of frameBytes over tracks with footage
  int score(int track, int sourceIndex) const;   // the priority below, for tests
};
extern gfcFrameCache frameCache;   // defined in gfcFrameCache.cpp
```
Policy (ruling 3, the owner's unload order), all distances in timeline frames along the playback walk (`gfcPlaybackManager::advanceFrame` with the transport's mode/limits/direction; the walk from the playhead is simulated once per `nextJob`/admission, at most N = endLimit−startLimit+1 steps):
- `a(t)` = steps from the playhead forward along the walk to t (0 = playhead); `b(t)` = steps from t forward to the playhead (t is behind by b). A frame is *ahead* when `a ≤ b` (or only `a` exists), else *behind*. ONCE: nothing beyond the end limit is ahead. LOOP: the walk wraps at the end limit to the start limit. BOUNCE: the walk turns at a limit.
- `K` = max(4, round(fps)) ("just behind"); `M` = max(4, round(fps)) (the always-allowed minimum ahead); `W` = max(1, budget / costPerTimelineFrame) (the window being loaded now).
- Score (higher = kept longer): pinned or on-screen → never a victim; ahead with `a ≤ W` → `3N + (N − a)`; near behind (`b ≤ K`) → `2N + (K − b)`; ahead leftover (`a > W`) → `N + (N − a)`; far behind (`b > K`) → `N − b`. Lowest score is evicted first, which yields the owner's order: far behind furthest first, then leftovers far ahead, then just-behind last.
- Candidates (`nextJob` scan order per track): pinned-and-unloaded first, then ahead frames by `a` from 0, then near-behind by `b` from 1. Leftovers and far-behind frames are never candidates. A timeline frame with no footage on the track is skipped. Multi-track: each loader scans the same timeline order, so all tracks load the same timeline frames.
- Admission: budget = `testBudget` if set, else `sett.memoryLimitPercent/100 × totalBytes − (usedBytes − cacheBytes)`, the system figures sampled at most every 100 ms. `cacheBytes` = Σ tables' resident+reserved bytes + `gfcUpload::stats().stagingBytes + pooledTextureBytes` + `gfcSurface::poolStats().pooledBytes`. A candidate with `a ≤ M` or pinned is admitted unconditionally (victims with a lower score are still named if any). Otherwise it is admitted when `cacheBytes + bytes ≤ budget`, or when resident, unpinned, off-screen frames with a strictly lower score exist whose bytes cover the shortfall (taken lowest score first, `setEvicting` on each, recorded for `drainVictims`); else denied (`nextJob` waits). Reservation = the track's `frameBytes` estimate (then `commit` corrects it). `drainVictims().trimPools` is true when pooled bytes exceed `costPerTimelineFrame` (so a 4K/1080p mix can't accumulate mismatched pooled memory); the GL thread then calls `gfcUpload::trimIdle(0)` and `gfcSurface::trimIdle(0)`.
- `nextJob` blocks on a condition variable notified by `retarget`, `advance`, `setTransport`, `commit`, `clearTrack`, `cancel`, `setTestBudget`, `pin` and `drainVictims`; it returns false after `wait` without a job.

- [ ] **Step 1: Write the failing test** — `cachePolicySelfTest` (`CACHE-POLICY`), no GL, a local `gfcFrameCache c; gfcFrameTable t0, t1;` (`frameBytes` 1000 per frame, `cache_*` clips not needed):
  - Window: `t0.reset(240)`, `c.attachTrack(0,&t0)`, `setTrackGeometry(0,0,0,239,1000)`, `setTestBudget(10*1000)`, `setTransport(100,+1,LOOPMODELOOP_ID,1,240,24)`. Ten `nextJob(0,…)` calls yield timeline 100…109 (`sourceIndex` 99…108); mark each `setResident` via `commit(job,1000,true)`. The 11th `nextJob` with a 20 ms wait returns false and `stats().denied ≥ 1`.
  - Advance evicts behind: `advance(105)` → `nextJob` yields timeline 110 (ahead, `a=5`, score `3N+N−5`) and `drainVictims().frames == {{0,99}}`: timeline 100 is now near-behind (`b=5`, score `2N+K−5`, the lowest resident). `t0.state(99)==Evicting`.
  - Order: `retarget(200)` → repeated `nextJob`/`commit`/`drainVictims` → the first victims are timeline 101,102,… in that order (far behind, furthest first), never the on-screen frame after `t0.setOnScreen(108)`.
  - Loop wrap: `setTransport(15,+1,LOOPMODELOOP_ID,1,20,24)` on a fresh table with budget 40 frames: jobs run 15…20 then 1…14; `score` of timeline 14 (behind by 1) > score of timeline 8 (behind by 7).
  - Bounce: `setTransport(19,+1,LOOPMODEBOUNCE_ID,1,20,24)` on a fresh table: jobs 19, 20, 18, 17 … (the walk turns at 20; 19 is already resident). Reverse: `setTransport(10,-1,LOOPMODEONCE_ID,1,20,24)` → jobs 10, 9, 8 … 1 and then false.
  - Minimum ahead: `setTestBudget(0)` → `nextJob` still yields the playhead and the next `M-1` frames (`stats().admitted` grows by M), then denies.
  - Pins: `pin(0, 239, true)` with budget 0 → next job is source 239.
  - Two tracks with offsets: `t1.reset(240)`, `attachTrack(1,&t1)`, `setTrackGeometry(1, 10, 0, 239, 1000)` → for playhead 100 the first job on track 1 has `sourceIndex 89`, `timelineFrame 100`; `costPerTimelineFrame()==2000`.
  - Mixed sizes + pool trim: `setTrackFrameBytes(1, 4000)`, budget 20 000, both tracks resident at timeline 100…103 (`cacheBytes()==20000`); `advance(103)` → the next track-0 job (timeline 104) is admitted, `drainVictims().frames` is non-empty, every victim's `score` is below `score(0,103)` and the victims' bytes total ≥ 1000. `drainVictims().trimPools` is true once simulated pooled bytes exceed `costPerTimelineFrame()` (inject with `setTestPooledBytes(size_t)` — add this test-only setter to `gfcFrameCache`; −1 restores the real pool figures).
  - No footage: `setTrackGeometry(1, 0, -1, -1, 0)` → `nextJob(1,…)` false immediately, `costPerTimelineFrame()==1000`.
  - Generation: `retarget` increments `generation()`, `advance` does not.
- [ ] **Step 2: Run** `$J --cache-test` → CACHE-POLICY fails to compile.
- [ ] **Step 3: Implement** `gfcFrameCache` per the policy above. Internal state: one mutex, one condition variable, `std::atomic<uint64_t> generation_`, per-track `{table, offset, first, last, frameBytes, cancelled}`, transport, pending victims, cached system figures with their sample time, stats. The walk is a local loop calling `gfcPlaybackManager::advanceFrame`, stopping when it returns to the playhead or after N steps.
- [ ] **Step 4: Run** `cmake -S . -B /Volumes/Archive/jefecheck-builds/rcu && cmake --build /Volumes/Archive/jefecheck-builds/rcu -j8 && $J --cache-test` → `CACHE-POLICY: fail=0`; full green list.
- [ ] **Step 5: Commit** — `feat(cache): frame cache coordinator with budget, priority and victims`.

---

### Task 5: Loaders pull from the coordinator; eviction on the GL thread

**Files:**
- Modify: `src/gfcSequence.cpp` (`initializeSequence`, `loadSequence` loop 1106–1200 → pull loop, `stopLoading` 1831, `clearSequence`, `setOffset` 1724, `generateTextures`), `src/gfcSequence.h`
- Modify: `src/gfcTrackManager.cpp` (`generateTextures` 291: drain victims first), `src/gfcTrackManager.h`
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (`initializeRenderingChain`: `frameCache.attachTrack(i, &seq->frameTable())` for the four tracks; read `JEFECHECK_CACHE_BUDGET_MB` env → `frameCache.setTestBudget(mb<<20)`)
- Test: `src/gfcCacheSelfTests.cpp` (`cacheBudgetSelfTest`, `cacheSettleSelfTest`, `cacheLeaksSelfTest`, `cacheHoldSelfTest`)

**Interfaces:**
- Consumes: Task 4's `gfcFrameCache`, Task 2's table.
- Produces: `static void gfcSequence::setTestDecodeDelayMs(int ms)` (slept before each `loadFrame` on the loader thread; 0 default); `static long gfcSequence::loadAnnouncements()` (count of `sendSystemChatMessage(..., GFCNETMESSAGETYPE_LOAD)` calls); `std::thread::id gfcSequence::loaderThreadId() const` (default id when none); `int gfcTrackManager::residentFrameCount()` (Σ tracks); `bool gfcTrackManager::timelineFrameResident(int frame) const` (true when every present track with footage at `frame` has it Resident/Evicting; true when no track has footage there); `bool gfcTrackManager::timelineFramesResident(int from, int count, int direction) const`; `static long gfcFrame::liveTextures()` (atomic: ++ when `generateTexture` creates or takes a texture, −− in `deleteTexture`); `long gfcSurface::liveSurfaces()` (surfaces outside the pool; 0 off macOS).
- Loader thread (`loadSequence`): announces the load once (when `params.loadFromTimeline==0`, as today), then `while (!loadingCanceled) { if (!frameCache.nextJob(trackIndex(), job, 100ms)) continue; decode job.sourceIndex (sleep testDecodeDelayMs first); frameCache.commit(job, bytes-from-the-decoded-frame-info, loaded); wait for queue room (existing cond/`sett.maximumFramesInQueue`); push to rawFrames; }` then `publishAudioPeaks` as today. `memoryManager.withinLimits()` is no longer called here. A frame that fails to decode: `commit(job, 0, false)` → Failed (and `loadingCanceled=true` when `!continueLoadingOnError`).
- `initializeSequence`: `frameTable_.reset(files.size())`; `frameCache.setTrackGeometry(trackIndex(), frameOffset, startFrame, endFrame, estimatedFrameBytes())`; fill `frames[i].savedParams` for every i (ruling 9: `params` with `fileName=files[i]`, `movieFrameIndex=i`, `movieReader=currentMovieReader()`). `clearSequence`: `frameCache.clearTrack(trackIndex())`, `frameTable_.reset(0)`. `stopLoading`: `frameCache.cancel(trackIndex())` before the join. `setOffset`: `frameCache.setTrackOffset`. First successful upload with `textureBytes` ≠ estimate: `frameCache.setTrackFrameBytes`.
- `gfcTrackManager::generateTextures()` (GL thread, context current): `auto v = frameCache.drainVictims(); for (auto& x : v.frames) sequences[x.track].evictFrame(x.sourceIndex); if (v.trimPools) { gfcUpload::trimIdle(0); gfcSurface::trimIdle(0); }` then each sequence's `generateTextures(1)`. `evictFrame` calls `deleteTexture()` which pools or deletes as today.
- `needsPlaybackTick()`/`hasPendingTextureUploads()` unchanged: victims are only named when a frame is admitted, and that frame reaches `rawFrames`, which already wakes the GL half.
- Transport: until Task 6 wires the playback manager, the coordinator's transport is set once per load by `gfcTrackManager::startLoadingSequence` → `frameCache.setTransport(playbackManager.getCurrentFrame(), 1, playbackManager.getPlaybackMode(), 1, getMaxTrackLength(), playbackManager.getTargetFPS())`, so loading starts at the playhead.

- [ ] **Step 1: Write the failing tests** (track loads use `cache_beep_24.mp4` 8-bit unless noted; helper `loadTrack(track, path, compression, scalePct)` = set filename/compression/scale on `myGUI`, `loadPreview()`, `trackManager.startLoadingSequence(track)`; helper `pump(ms)` = loop `{ trackManager.generateTextures(); sleep 2 ms }`; frame bytes at 8-bit 320×180 = 230 400):
  - `CACHE-BUDGET`: `setTestBudget(20 * 230400)`, load track A; pump 3 s; assert `residentFrameCount() == 20 ± 1`, `frameCache.cacheBytes() ≤ budget + 2*costPerTimelineFrame()`; `retarget(120)` then `retarget(230)`, pump 2 s each: every polled sample satisfies the same bound and `residentFrameCount() ≤ 22`; after the retargets frames 229…239 are resident. Mixed sizes: track B at 50 % (57 600 B/frame), budget 12 frames of A: pump 3 s; bound holds, and both tracks have identical resident timeline sets (`loadedSegments` with offsets folded). Lowering: `setTestBudget(5 * 230400)` with nothing else happening → after 1 s `residentFrameCount()` unchanged; `retarget(50)` → within 2 s it is ≤ 7.
  - `CACHE-SETTLE`: budget 40 frames, load A, pump until 40 resident; record `stats()`; pump 2 s → `admitted/evicted` unchanged (paused on a full budget). Loop that fits: `setTransport(1,+1,LOOPMODELOOP_ID,1,30,24)`, drive `advance()` 1…30 ten times round with pumps → `evicted` unchanged. Whole clip fits: budget −1 (derived) with `cache_silent_24.mp4` → all 240 resident, `evicted==0`, `framePending(k)==false` for all k.
  - `CACHE-LEAKS`: baseline `gfcFrame::liveTextures()`, `gfcSurface::liveSurfaces()`; budget 10 frames; 5 cycles of {load A, pump 1.5 s, `retarget(200)`, pump 1 s, `unloadAndClear()`}; then `gfcUpload::trimIdle(0); gfcSurface::trimIdle(0)` on the GL thread → both counters back at baseline, `residentFrameCount()==0`, `frameCache.cacheBytes()==0`, pool bytes 0.
  - `CACHE-HOLD`: budget 10 frames, `cache_seq.0001.png` on track C, `setTransport(60,…)`, `setHoldMode(2)`: within 3 s source 0 and 119 are Resident although outside the window; `retarget(60)` + pump 2 s → still resident; `getFrame(200,false).loaded` and it is frame 119.
  - Loader error: `truncated.mov` (from the video media dir) on track D, budget derived: pump 3 s → some `Failed` states, `stats().denied` stops growing between two samples 500 ms apart, loader thread id unchanged.
- [ ] **Step 2: Run** `$J --cache-test` → the four suites fail.
- [ ] **Step 3: Implement** the pull loop, geometry plumbing, victim drain and counters above.
- [ ] **Step 4: Run** build + `$J --cache-test` → all suites `fail=0`; full green list. `--video-load-test` must still pass unchanged: `AUDIO-SEQUENCE`'s "load from frame 10" still restarts the loader in this task (Task 6 changes that), and `AUDIO-FORCED-LOAD`'s `heldAt()` still sees the loader held by the queue cap.
- [ ] **Step 5: Commit** — `feat(cache): loaders pull jobs from the coordinator; victims freed before uploads`.

---

### Task 6: Retarget from every seek path; direction, loop and four-track tests

**Files:**
- Modify: `src/gfcPlaybackManager.h`, `src/gfcPlaybackManager.cpp` (`update` 63, `setCurrentFrame` 482, `setDirection` 601, `setPlaybackMode` 368, `setInPoint` 563, `setOutPoint`, `setToFrame`, `setLoopPriority` 709, `setTargetFPS` 376/388, `setTargetRate`)
- Modify: `src/gfcTrackManager.cpp` (`startLoadingSequenceAt` 457, `startLoadingAllAt` 475, `startLoadingSequence` 38)
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (`setInPointAndLoad` ~501, `startLoadingTrackAt` 2175, `restartTrackLoadAtFrame` 3381)
- Modify: `src/gfcMovieSelfTests.cpp` (`audioSequenceSelfTest` ~lines 345–352)
- Test: `src/gfcCacheSelfTests.cpp` (`cacheRetargetSelfTest`, `cacheDirectionSelfTest`, `cacheTracksSelfTest`)

**Interfaces:**
- Produces: `void gfcPlaybackManager::cacheSyncTransport()` (private; sends `frameCache.setTransport(currentFrame, currentDirection, playbackMode, getStartLimit(), getEndLimit(), targetFPS)` only when one of those changed since the last send — mirror of `audioSyncTransport`), called from `setDirection`, `setPlaybackMode`, `setInPoint`, `setOutPoint`, `setToFrame`, `setFromFrame`, `setLoopPriority`, `setTargetFPS`, `setTargetRate`. `setCurrentFrame` → `cacheSyncTransport(); frameCache.retarget(frame)`. `update()` → `frameCache.advance(currentFrame)` whenever it changed `currentFrame` (both the audio-led and the paced branch; one atomic store + notify, no table walk).
- Load-from-here paths become jumps (ruling 5): `gfcTrackManager::startLoadingSequenceAt(track, frame)` and `startLoadingAllAt(frame)` → when the track(s) are initialised (`!isEmpty()`) they call `playbackManager.setCurrentFrame(frame + 1)` (timeline 1-based) and return; only an empty track takes the old `startLoading(frame)` path. `sessionManager.writeCrashSession()` is no longer called from these. `setInPointAndLoad` → `setInPoint(cur)` only. `restartTrackLoadAtFrame(track, frame, …)` → returns `!seq->isEmpty()` and does nothing else (every frame can be force-loaded since Task 5). `gfcTrackManager::startLoadingSequence` keeps the Task 5 `setTransport` call (a fresh load starts at the playhead).
- `gfcSequence::startLoading(fromTrack, fromTimeLine)` keeps its signature for the remaining callers (`startLoading(params)` from the Load Window and restores).

- [ ] **Step 1: Write the failing tests:**
  - `CACHE-RETARGET`: budget 20 frames, `cache_beep_24.mp4` on A, wait 20 resident. Random jumps: 30 × `playbackManager.setCurrentFrame(random 1..240)` with 300 ms pumps → after each, the jumped frame is resident within 1 s (`timelineFrameResident`). Seek storm: record `loaderThreadId()`, `audioClip()` pointer, `loadAnnouncements()`, the crash-session file's mtime+size (path from a new `std::string jefe::qt::recoverySessionPath()`; add it to the bridge if absent), `generation()`; 200 × `setCurrentFrame(random)` within 2 s (pump between); assert thread id unchanged (no join), clip pointer unchanged, `loadAnnouncements()` unchanged, crash file unchanged, `generation()` grew by 200, no `Failed` state; then `unloadAndClear()` → `liveTextures()`/`liveSurfaces()` at baseline. Sequential play-through: `setPlaybackMode(LOOPMODEONCE_ID)`, `setCurrentFrame(1)`, `startPlayFwd()`, loop `{ jefe::qt::tickPlaybackTiming(); trackManager.generateTextures(); sleep 1 ms }` until `getCurrentFrame()==240` or 15 s; collect the union of resident indices sampled each loop → every one of 0…239 was resident at some point; budget bound held on every sample; `pause()`.
  - `CACHE-DIRECTION`: budget 20; `setCurrentFrame(200)`, `startPlayRev()` (direction −1, ONCE): after 2 s the resident set is within [180,200] and nothing above 200 was admitted after the retarget (`stats().admitted` vs. resident indices). BOUNCE: `setPlaybackMode(LOOPMODEBOUNCE_ID)`, `setInPoint(230)`, `setOutPoint(240)`, `setCurrentFrame(238)`, direction +1: after 2 s frames 230…240 all resident and `evicted` stays flat for a further 2 s while advancing round twice with `tickPlaybackTiming`. Loop shrink/widen: `setInPoint(100); setOutPoint(104)` → within 2 s those five resident; `setInPoint(1); setOutPoint(240); setCurrentFrame(150)` → within 2 s frames 150…160 resident (no starvation by frames 100…104, which now score as far behind).
  - `CACHE-TRACKS`: budget 60 × 230 400; A = `cache_beep_24.mp4` offset 0, B = `cache_silent_24.mp4` offset +50 (`setTrackOffset`), C = `cache_seq.0001.png` offset −100 (120 frames → footage on timeline −99…20); `setCurrentFrame(60)`; pump 3 s → the resident *timeline* sets of A and B are identical (segments with offsets folded), each ≥ 25 frames, and C's `residentCount()==0` (no footage at 60 and beyond, so C cost nothing there). Then `setCurrentFrame(5)`: within 2 s A and C both have timeline frames 5…14 resident and B still has nothing (its footage starts at 51).
- [ ] **Step 2: Run** `$J --cache-test` → the three suites fail. Edit `audioSequenceSelfTest`: the block `trackManager.startLoadingSequenceAt(0, 10)` … `check(filled10 && …)` + `check(clip && rmsAt(*clip,0)>0.3 && seq->audioPeaksVersion()>v0 …)` becomes: `const std::thread::id tid=seq->loaderThreadId(); trackManager.startLoadingSequenceAt(0, 10); std::this_thread::sleep_for(200ms); check(seq->audioClip()==clip && seq->loaderThreadId()==tid && playbackManager.getCurrentFrame()==11, "a load-from-frame is a jump: same clip, same loader thread, playhead moved");` (drop `v0`, `want10`, `filled10`, `clip10`).
- [ ] **Step 3: Implement** the transport/retarget plumbing above.
- [ ] **Step 4: Run** build + `$J --cache-test` + full green list (`--video-load-test` with the edited AUDIO-SEQUENCE: still 18 suites).
- [ ] **Step 5: Commit** — `feat(cache): every seek retargets the window; load-from-here is a jump`.

---

### Task 7: Preferences — memory limit and "When loading can't keep up"

**Files:**
- Modify: `src/gfcStructures.h` (`int lagPolicy` default 0: 0 Automatic, 1 Keep time, 2 Show every frame)
- Modify: `src/qt/qt_prefs_persist.cpp` (`Engine/lagPolicy`; `Engine/memoryLimitPercent` exists from Task 1)
- Modify: `src/qt/PreferencesWindow_qt.cpp` (General page, "Playback & Engine" section after the "Frames to read ahead" row ~line 352)
- Modify: `tests/ui/jefecheck/locators.py` (~line 258)
- Test: `src/gfcCacheSelfTests.cpp` (`cachePrefsSelfTest`)

**Interfaces:**
- Produces: `sett.memoryLimitPercent` (Task 1) with a `QSpinBox` 50–100, suffix ` %`, object `preferences.engine.memorylimit.spin`, label "Memory limit (% of RAM)", tooltip "Frames load while the whole machine's memory use stays under this share of RAM; above it, a frame is unloaded for each one that loads."; `sett.lagPolicy` with a `QComboBox` object `preferences.engine.lagpolicy.combo`, label "When loading can't keep up", items in order: "Automatic (keep time with sound, show every frame without)", "Keep time (skip frames)", "Show every frame (slow down)". Both mutate `sett` live (the Preferences model, §27). Locators: `PREFS_ENGINE_MEMORY_LIMIT = "preferences.engine.memorylimit.spin"`, `PREFS_ENGINE_LAG_POLICY = "preferences.engine.lagpolicy.combo"`.
- `bool gfcPlaybackManager::keepTime() const` — `sett.lagPolicy==1 || (sett.lagPolicy==0 && trackManager.anyTrackHasAudio())`; `bool gfcTrackManager::anyTrackHasAudio() const`. (Used by Tasks 8 and 10; no behaviour change yet.)

- [ ] **Step 1: Write the failing test** — `cachePrefsSelfTest` (`CACHE-PREFS`): read the two `QSettings` keys first and restore them at the end (the harness writes the user's real settings); defaults of a fresh `gfcSettings` are `memoryLimitPercent==90 && lagPolicy==0`; set `sett` to 75 and 2, `jefe::qt::writePreferences()`, reset both fields to 90/0, `jefe::qt::loadPreferences()` → 75 and 2. `keepTime()` is true with `cache_beep_24.mp4` loaded and `lagPolicy==0`, false with `cache_silent_24.mp4` only, true/false when forced to 1/2.
- [ ] **Step 2: Run** `$J --cache-test` → CACHE-PREFS fails to compile.
- [ ] **Step 3: Implement** the two fields, keys, widgets and `keepTime()`.
- [ ] **Step 4: Run** build + `$J --cache-test` + full green list.
- [ ] **Step 5: Commit** — `feat(prefs): memory limit and when-loading-cannot-keep-up preferences`.

---

### Task 8: What an unloaded frame shows; renders through the cache

**Files:**
- Modify: `src/gfcSequence.h`, `src/gfcSequence.cpp` (`getFrame` 1545, `frameReadyForRender` 1625, `getFilenameatFrame` 1633)
- Modify: `src/gfcPlate.cpp` (`draw()` 1168: the branch where `!theFrame.loaded` and `!showPreview`)
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (`isTrackFrameReady` 3378)
- Test: `src/gfcCacheSelfTests.cpp` (`cacheDisplaySelfTest`); render check via the Global Constraints command with `JEFECHECK_CACHE_BUDGET_MB`

**Interfaces:**
- Produces: `gfcSequence` members `int lastDrawnIndex_ = -1; uint64_t lastDrawnGeneration_ = 0;` (GL thread only). `getFrame(frameNo, forceLoad=false)`, hold mode 0 (and the in-range part of mode 2): if `frames[i].loaded` → `lastDrawnIndex_=i; lastDrawnGeneration_=frameCache.generation(); frameTable_.setOnScreen(i);` return it. Else if `!forceLoad && playbackManager.isPlaying() && playbackManager.keepTime() && lastDrawnIndex_>=0 && frames[lastDrawnIndex_].loaded && lastDrawnGeneration_==frameCache.generation()` → return `frames[lastDrawnIndex_]` (Keep time: the last drawn frame stays on screen). Else → `emptyFrame` (ruling 6). `bool gfcSequence::framePending(int timelineFrame) const` (Task 2) tells the plate to draw the label. `frameReadyForRender` → in range (every frame has `savedParams` since Task 5); `isTrackFrameReady` follows. `getFilenameatFrame` keeps " - NOT IN RAM" for unloaded frames.
- `gfcPlate::draw()`: when `!theFrame.loaded && !showPreview && theSequence && theSequence->framePending(currentFrame)`, draw "Loading" with `gfc_gl_draw` at the viewport's centre (text size from `sett.feedbackMessageSize`, same colour as the status text) over the cleared background; nothing else changes.
- Renders: `forceLoad` already uses `savedParams` + `forcedMovieReader`; nothing in it touches `frameTable_` or `frameCache`.

- [ ] **Step 1: Write the failing test** — `cacheDisplaySelfTest` (`CACHE-DISPLAY`): budget 10 frames, `cache_beep_24.mp4` on A, `lagPolicy=1`; wait frame 1 resident; `getFrame(1)` loaded (sets lastDrawn, `frameTable().onScreen()==0`); `startPlayFwd()`; `getFrame(150)` (unloaded, same generation) returns a `loaded` frame with `indexNumber==0` (the last drawn); `pause()`; `setCurrentFrame(200)` (generation bump) → `getFrame(200).loaded==false` and `framePending(200)==true`; wait ≤ 2 s → `getFrame(200).loaded` and `framePending(200)==false`; `lagPolicy=2`, `startPlayFwd()`, `getFrame(230)` (unloaded) → `.loaded==false` (Show every frame never substitutes); `pause()`. Forced load: `getFrame(120, true)` is `.loaded` with `.fileName==framePathAt(120)`; after `cleanForcedLoad()` `frameTable().state(119)==Unloaded` (the cache was not disturbed); `frameReadyForRender(120)==true`, `frameReadyForRender(500)==false`. The on-screen frame is never a victim: budget 3 frames, `getFrame(1)` drawn, `setCurrentFrame(100)` → after 2 s `frameTable().state(0)==Resident` still.
- [ ] **Step 2: Run** `$J --cache-test` → CACHE-DISPLAY fails.
- [ ] **Step 3: Implement** the display rule, the label and the render readiness.
- [ ] **Step 4: Run** build + `$J --cache-test`; full green list; and the render test on a clip larger than the budget: `JEFECHECK_CACHE_BUDGET_MB=64 $J --config-dir /Volumes/Archive/jefecheck-builds/rcu-tests/config --render-test /Volumes/Archive/jefecheck-builds/rcu-tests/render --open-file /Volumes/Archive/jefecheck-builds/testclips/beep_23976_prores.mov` → exit 0 and the same number of output files as without the variable (`ls | wc -l` both runs).
- [ ] **Step 5: Commit** — `feat(cache): unloaded frames hold the last picture or show Loading; renders force-load anywhere`.

---

### Task 9: Movie audio independent of picture

**Files:**
- Modify: `src/gfcMovieReader.h`, `src/gfcMovieReader.cpp` (`Impl`: packet loop ~lines 195–250, `seekTo` 351, `setAudioEnabled` 512)
- Modify: `src/gfcAudioClip.h`, `src/gfcAudioClip.cpp`
- Modify: `src/gfcSequence.h`, `src/gfcSequence.cpp` (`prepareSequenceFiles` 855 → readers open sound-off; `initializeSequence`; `startLoading` 1766; `stopLoading`; `clearSequence`; remove `appendMovieAudio` from the loader loop)
- Modify: `src/gfcMovieSelfTests.cpp` (`audioSequenceSelfTest` first check message; `audioForcedLoadSelfTest` ~line 700: drop `heldEnd<frameStartSample(40)` from the `check`, keep `loader && seq->getLoadedFrameCount()>=4`)
- Test: `src/gfcCacheSelfTests.cpp` (`audioHolesSelfTest`)

**Interfaces:**
- Produces (`gfcMovieReader`): `void setAudioOnly(bool on)` (takes effect at `open()`: no video decoder; `readFrame` returns false); `bool readAudioUntil(int64_t sampleEnd, std::string* err)` — demuxes forward, drops video packets (never queues them), decodes audio into the FIFO until it covers `sampleEnd` or EOF (false only on error); `bool seekAudio(int64_t frameIndex, std::string* err)` — seeks the audio stream to the first packet at or before `frameStartSample(frameIndex)`, empties the FIFO. `takeAudio` unchanged.
- Produces (`gfcAudioClip`, hole-tolerant, ruling 8): storage becomes chunk slots indexed by `sample / kChunkFrames` with null = hole; `void writeAt(int64_t sample, const float* interleaved, int64_t frames)` (anywhere, overwrites); `bool covers(int64_t fromSample, int64_t frames) const`; `int64_t firstHoleFrom(int64_t sample) const` (−1 when fully filled from `sample` to `endSample()`); `startSample()` = lowest written sample, `endSample()` = highest written + 1; `read` zero-fills holes; `reset(startSample)` and `append` kept (`append` = `writeAt(endSample(), …)` with an empty clip starting at `startSample`). `gfcAudioPeaks::accumulateAt(int64_t firstSample, const float*, int64_t frames)` writes bins at absolute positions, growing the vector with zeros; `gfcAppendAudioAt` uses `writeAt`/`accumulateAt`.
- Produces (`gfcSequence`): `std::thread* audioThread_`, `std::shared_ptr<gfcMovieReader> audioReader_` (opened in `initializeSequence` with `setAudioOnly(true)` when the probe says `hasAudio`; the clip decision stays synchronous there), `std::atomic<int64_t> audioTarget_` (sample of the playhead's source frame, set by `void gfcSequence::setAudioTarget(int timelineFrame)`; `void gfcTrackManager::retargetAudio(int timelineFrame)` fans it out to the four tracks and is called from `gfcPlaybackManager::setCurrentFrame` right after `frameCache.retarget`), `void audioThreadMain()`: fills from `frameStartSample(rangeBegin)` to the end in 1-second blocks (`readAudioUntil`, `takeAudio`, `gfcAppendAudioAt`), sleeping 2 ms between blocks; whenever `audioTarget_` is not covered and lies more than 2 s past the cursor, `seekAudio(target frame)`, fill 2 s ahead, then continue forward to the end, then restart at `firstHoleFrom(start)` until no hole remains; exits on `audioCanceled_`. `stopLoading`/`clearSequence` cancel and join it. The loader's reader (`movieReader_`) and `forcedReader_` open with sound off.
- Audio engine: unchanged. `setTrackClip` is still called from `startLoading` with the same clip.

- [ ] **Step 1: Write the failing test** — `audioHolesSelfTest` (`AUDIO-HOLES`): clip unit: `writeAt(96000, …)` then `writeAt(0, …)`; `covers(0,48000)`, `!covers(48000,48000)`, `firstHoleFrom(0)==48000`, `read` across the hole gives zeros there; `startSample()==0`, `endSample()==144000`. Reader: `setAudioOnly(true)`, `open(cache_beep_24.mp4)`, `readAudioUntil(frameStartSample(48,24,1))` → `takeAudio` returns ≥ 2 s; `seekAudio(200)` + `readAudioUntil(frameStartSample(210,24,1))` → first sample ≤ `frameStartSample(200)`; `readFrame` returns false. Sequence: `setTestBudget(10 frames)`, load A (`cache_beep_24.mp4`); within 6 s `audioClip()->firstHoleFrom(0)==-1` although `residentCount()≤12` (sound complete, picture not); rms at frames 0, 24, 48 … 216 > 0.3 and at 12, 36 … < 0.01 (beeps where the source has them, across unloaded picture). Jump: fresh load, `setCurrentFrame(220)` within 100 ms of `startLoadingSequence` → within 1.5 s `covers(frameStartSample(220), 48000)`; within 6 s no hole. Forced loads leave `loaderMovieReader()->queuedAudioFrames()==0`. `audioPeaksSnapshot().peaks.size() ≥ 10 s × 100 − 5` at the end and a beep bin at 100…103 > 0.7.
- [ ] **Step 2: Run** `$J --cache-test` → AUDIO-HOLES fails. Apply the two `gfcMovieSelfTests.cpp` edits listed under Files.
- [ ] **Step 3: Implement** the reader mode, the clip, the audio thread.
- [ ] **Step 4: Run** build + `$J --cache-test` + full green list (`--video-load-test` 18 suites, `AUDIO-SEQUENCE` and `AUDIO-FORCED-LOAD` with their edits).
- [ ] **Step 5: Commit** — `feat(audio): movie sound decoded independently of picture into a hole-tolerant clip`.

---

### Task 10: Keep time / Show every frame and the pre-play buffer

**Files:**
- Modify: `src/gfcPlaybackManager.h`, `src/gfcPlaybackManager.cpp` (`update` 63–137, `startPlayFwd` 228, `startPlayRev`, `setCurrentFrame` 482, `audioTryLead` 811)
- Test: `src/gfcCacheSelfTests.cpp` (`cacheKeepTimeSelfTest`, `cacheEveryFrameSelfTest`)

**Interfaces:**
- Consumes: `keepTime()` (Task 7), `trackManager.timelineFrameResident/timelineFramesResident` (Task 5), `gfcSequence::setTestDecodeDelayMs` (Task 5).
- Produces (`gfcPlaybackManager`, private): `bool buffering_ = false; std::chrono::steady_clock::time_point bufferingSince_;` `static constexpr double kPrePlaySeconds = 0.25; static constexpr double kPrePlayMaxWait = 0.5;` `int prePlayFrames() const` = `ceil(kPrePlaySeconds × targetFPS)`. `void beginBuffering()` (sets the flag + time) is called from `startPlayFwd`/`startPlayRev` when play starts and from `setCurrentFrame` while playing, **only when `keepTime()`**. In `update()`, right after the `!playing` early return: `if (buffering_) { if (trackManager.timelineFramesResident(currentFrame, prePlayFrames(), currentDirection) || elapsed ≥ kPrePlayMaxWait) { buffering_=false; if (currentDirection>0) audioTryLead(); } else { intraFrameCount=0; return; } }` — the audio lead is deferred until buffering ends (so `audioTryLead` in `startPlayFwd`/`audioSeek` is skipped when `beginBuffering()` was just called).
- Show every frame (ruling 7): in `update()`'s paced branch, when `intraFrameCount >= timePerFrame`: compute `next = advanceFrame(currentFrame, dirCopy, mode, limits)`; if `!keepTime() && next != currentFrame && !trackManager.timelineFrameResident(next)` → hold (`intraFrameCount = timePerFrame`, no advance, no `recordAdvanceForFPS`) and return; the measured-FPS EMA then shows the drop. `audioTryLead()` returns false without starting the engine when `!keepTime()` (the engine never leads; each shown frame plays its own slice through the existing `playSlice` path). Keep time: no change to the audio-led branch (the engine leads, the picture shows `leaderFrame()`, Task 8's display rule holds the last drawn frame). No new per-tick work: the residency check runs only on the tick that would advance a frame.

- [ ] **Step 1: Write the failing tests** (both: budget 20 frames, `cache_beep_24.mp4` on A, `sett.maximumFramesInQueue=100000`, `setTestDecodeDelayMs(80)` → the loader runs at ~12 fps against 24 fps playback; drive with `{ tickPlaybackTiming(); trackManager.generateTextures(); sleep 1 ms }`; fake audio output as AUDIO-PLAYBACK does):
  - `CACHE-KEEPTIME` (`lagPolicy=1`): `setCurrentFrame(1)`, `startPlayFwd()`; sample `getCurrentFrame()` and `audioEngine.leaderFrame()` every loop for 5 s: the frame sequence is monotonic non-decreasing, `audioEngine.isLeading()` becomes true within 0.6 s and stays true, the number of distinct frames shown < frames elapsed (frames were skipped), `getCurrentFrame() ≥ 100` after 5 s (time was kept: ≥ 24 × 5 × 0.8); the clip has no hole in [0, 5 s] (`covers`); `pause()`; `setTestDecodeDelayMs(0)`; skipped frames still load: `setTestBudget(60 frames)`, `setCurrentFrame(60)` → within 3 s frames 60…79 (ahead) and 36…59 (near behind, K=24) are all resident. Pre-play buffer: with delay 80 ms, `setCurrentFrame(200)`, `startPlayFwd()` → `getCurrentFrame()` stays 200 for ≥ 150 ms and `isLeading()` is false during that time; after ≤ 600 ms it advances.
  - `CACHE-EVERYFRAME` (`lagPolicy=2`): same start; for 5 s the frame sequence advances by exactly +1 each change, `audioEngine.isLeading()` never true, `getCurrentFrame()` after 5 s is between 40 and 90 (slowed to the loader), `getCurrentFPS()` reported below 20 at the end; every shown frame was resident when shown (`timelineFrameResident(getCurrentFrame())` true on every sample after the first advance).
- [ ] **Step 2: Run** `$J --cache-test` → both fail.
- [ ] **Step 3: Implement** the buffering state and the show-every-frame hold.
- [ ] **Step 4: Run** build + `$J --cache-test` + full green list. Pacing guard: `$J --video-load-test` (`AUDIO-PLAYBACK`, `AUDIO-SYNC` green) and a 20 s manual play of `beep_23976_prores.mov` at 23.976 with the on-screen fps steady at the target: `timeout 25 $J --config-dir /Volumes/Archive/jefecheck-builds/rcu-tests/config --open-file /Volumes/Archive/jefecheck-builds/testclips/beep_23976_prores.mov; true` (cannot be asserted headless; note the result in the ledger).
- [ ] **Step 5: Commit** — `feat(playback): keep time or show every frame when loading lags; pre-play buffer`.

---

### Task 11: Linux Docker build, docs, developer notes, measurements

**Files:**
- Modify: `developer_notes.md` (new `## 40. Rolling frame cache`; §38 table row for the re-measured 4K rate; §39 note that eviction frees through `deleteTexture` and pools count against the budget)
- Modify: `CLAUDE.md` (index line under "Qt UI code — read developer_notes.md first": `§40 **rolling frame cache** (frame table + coordinator, byte budget from the RAM %, priority order, retarget on every seek, Keep time / Show every frame, audio thread, buffer-bar segments, --cache-test)`; Preferences paragraph: add the two prefs to the General section list; Known Issues: remove nothing, add "`--cache-test` needs ffmpeg")
- Modify: `docs/manual.md` (Preferences Window → Engine Section: replace the "Percentage of RAM to use" and "Use Inactive Memory" bullets with "Memory limit (% of RAM)" and "When loading can't keep up" (the three options); Track Bar paragraph ~line 258: the green segments show the frames in memory right now and move with the playhead; Movies "Limits" paragraph ~line 198: clips longer than memory play to the end, frames load ahead of the playhead and unload behind it)
- Modify: `docs/quick-start.md` (rewrite "How to Start Loading a Sequence from a Certain Point in the Timeline" ~line 78: clicking anywhere on the timeline loads around that frame; Alt+click/Alt+I still work and now just jump; add "How to Review a Clip Longer Than Memory" and "What the green segments under a track mean")

- [ ] **Step 1: Linux build** (image `jefecheck-u2404d`, amd64, worktree mounted at `/src`, build tree `/Volumes/Archive/jefecheck-builds/linux-rcu` at `/build` — already configured with `CMAKE_CXX_FLAGS=-idirafter /shim`, shim at `/Volumes/Archive/jefecheck-builds/linux-shim`): `docker run --rm --platform linux/amd64 -v /Users/dgollas/projects/jefecheck2/.claude/worktrees/rolling-cache-upload:/src:ro -v /Volumes/Archive/jefecheck-builds/linux-rcu:/build -v /Volumes/Archive/jefecheck-builds/linux-shim:/shim:ro jefecheck-u2404d bash -c "cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS='-idirafter /shim' && cmake --build /build -j8"` → `[100%] Built target jefecheck`. Fix any GCC-only errors (missing returns, `const`, `mutable`, `<cmath>`), rebuild macOS too.
- [ ] **Step 2: Linux suites** as uid 501 with `HOME=/tmp/w/home`: `mkdir -p /Volumes/Archive/jefecheck-builds/linux-rcu-out && docker run --rm --platform linux/amd64 -u 501:20 -e HOME=/tmp/w/home -e QT_QPA_PLATFORM=xcb -v /Users/dgollas/projects/jefecheck2/.claude/worktrees/rolling-cache-upload:/src:ro -v /Volumes/Archive/jefecheck-builds/linux-rcu:/build:ro -v /Volumes/Archive/jefecheck-builds/linux-rcu-out:/out jefecheck-u2404d bash -c "mkdir -p /tmp/w/home && cd /tmp/w && ln -sfn /src/src/FX FX && ln -sfn /src/src/fonts fonts && for t in --video-load-test --cache-test --notes-test; do timeout 900 xvfb-run -a -s '-screen 0 1920x1080x24' /build/jefecheck \$t > /out/\$t.log 2>&1; echo \$t exit=\$?; done"` → all three `exit=0`; `grep -h 'fail=' /Volumes/Archive/jefecheck-builds/linux-rcu-out/*.log | grep -v 'fail=0'` prints nothing. If `/build/jefecheck` refuses to start as uid 501, chown the build tree inside the container first (`-u 0` step with `chown -R 501:20 /build`).
- [ ] **Step 3: Measure** the 4K rate on this branch (spec non-goal, to be recorded): `$J --video-perf-test /Volumes/Archive/jefecheck-builds/perfmedia/prores4444_4k.mov` and `…/prores4444_4k_clean.mov`; put the decode+upload fps into the §38 table (a new "with IOSurface upload (2026-11)" column) and into §40 with the derived Keep-time skip share at 24 fps (`1 − loadFps/24`).
- [ ] **Step 4: Write** `developer_notes.md` §40 covering: the table/coordinator split and why the coordinator is GL-free; the score formula and the constants K/M/W; budget derivation and what counts as cache bytes; admission on the decode thread, victims freed in `gfcTrackManager::generateTextures()`; `retarget` vs `advance` vs `setTransport` and the list of callers; load-from-here is now a jump; the display rule (`lastDrawnIndex_`, generation); the audio thread and hole-tolerant clip; `--cache-test` suites and the injection points (`setTestBudget`, `JEFECHECK_CACHE_BUDGET_MB`, `setTestDecodeDelayMs`, `setTestPooledBytes`); the no-per-tick-work rule; the Linux Docker commands above. Then the `CLAUDE.md`, `docs/manual.md`, `docs/quick-start.md` edits listed under Files.
- [ ] **Step 5: Verify** — `cmake --build /Volumes/Archive/jefecheck-builds/rcu -j8` and the full green list once more on macOS; `git status` shows only the intended files.
- [ ] **Step 6: Commit** — `docs(cache): developer notes §40, manual and quick start; Linux build verified`.

---

## Self-review notes

- Spec coverage: goals (window ahead, eviction order, on-screen pin) → Tasks 4–5, 8; four tracks → 4, 6; budget → 1, 4, 7; Keep time / Show every frame + buffer → 10; timeline segments → 2–3; scope (render bypass, remote local, loading while paused) → 8, 6 (`setCurrentFrame` is the remote path), 4; constraints (GL thread, pools, audio, thumbnails, prefs, tests, Linux) → 5, 9, 2, 7, 1, 11; rulings 1–13 → 4 (1, 3, 4, 12 via version), 1 (2), 5–6 (5), 8 (6, 9), 10 (7), 9 (8), 2 (10, 11), 5–10 (13).
- Type consistency: `gfcFrameTable`/`gfcFrameCache` names in Tasks 5–10 match Task 2/4 signatures; `keepTime()` defined in Task 7 and used in 8 and 10; `timelineFrameResident`/`timelineFramesResident` defined in Task 5, used in 10; `setTestDecodeDelayMs` defined in 5, used in 10; `loaderThreadId`/`loadAnnouncements` defined in 5, used in 6.
- Ordering: measurement (1) and bookkeeping (2) precede eviction (4–5); every task leaves the loader working (2–3 sequential pass, 5+ pull loop) and names its test edits so the 18 `--video-load-test` suites stay green.
