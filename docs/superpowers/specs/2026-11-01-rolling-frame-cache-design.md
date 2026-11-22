# Rolling frame cache: design

Date: 2026-11-01. Background and code map: `developer_notes.md` §38–§40 (movie loading, frame upload, rolling frame cache).

## Why

Today each track decodes its frame range front to back into textures and stops for good once the machine's memory is full. Nothing is ever unloaded, so a 4K clip longer than about 10 s on a 16 GB Mac simply can't be reviewed past that point. The owner wants long clips (a one-minute dailies clip is 1,440 frames) reviewable end to end, for movies and image sequences alike.

## What "reviewable end to end" means (owner)

Both of these:

1. **Play straight through.** Press play on a clip of any length and it plays to the end. Frames load ahead of the playhead and frames behind it are unloaded to make room.
2. **Reach any frame.** Click or jump anywhere on the timeline and that frame appears after a short wait, then playback carries on from there.

## Goals

### What stays loaded (owner)

- A loaded frame stays loaded until its memory is needed for a frame that has to load. Nothing is unloaded on a timer or in the background.
- A short range scrubbed back and forth, or an in/out loop that fits in memory, therefore loads once and stays resident.
- When room is needed, unload in this order (my call, owner shown it without objection):
  1. frames behind the playhead in the playback direction, furthest first;
  2. frames far ahead of the playhead that are outside the window being loaded now (leftovers from an earlier jump);
  3. frames just behind the playhead last, so a quick step back stays instant.
  "Behind" follows the current direction: forward, reverse and Bounce.
- The frame on screen is never unloaded to make room (my call).

### Four tracks (owner)

- Optimise for **the same timeline frames being loaded on every track**: the cache works in timeline frames. For each timeline frame in its window it loads, on every track, the source frame that sits there once that track's offset is applied. So an A/B compare never has one side loaded and the other not.
- My calls that follow from it:
  - one memory budget shared by all tracks;
  - a timeline frame costs the sum of its tracks' frame sizes, so a 4K track narrows the window for the 1080p track beside it;
  - a track with no footage at a timeline frame costs nothing there;
  - unloading takes a timeline frame off all tracks together.

### Memory budget (owner)

- Preference: **memory limit, percentage of the machine's RAM, default 90%**. The check is system-wide (all processes, not just JefeCheck), the way `gfcMemoryManager::withinLimits()` measures it today. Today's limit is hard-coded (95% macOS/Windows, 85% Linux) and `setLimit()` has no caller; the preference replaces both.
- The check runs **only when a frame is about to load**. If the machine is over the limit, unload one frame (by the order above) first, then load. Nothing is unloaded while nothing is loading, even if another app pushes the machine over the limit: the cache keeps what it has and only swaps one for one from then on.
- A minimum is always allowed even over the limit: the frame at the playhead plus about one second ahead (my call), so footage can still be viewed on a busy machine.

### When loading can't keep up with playback (owner)

- Preference: **"When loading can't keep up"**, with the options *Keep time (skip frames)* and *Show every frame (slow down)*. Default: keep time when the clip has sound, show every frame when it has none. (The owner asked for this to be a preference; the per-sound default is their stated behaviour.)
- *Keep time:* sound is the clock and never waits. If the next frame isn't loaded when due, the last loaded frame stays on screen and playback skips ahead. Skipped frames still load.
- *Show every frame:* every frame is shown in order and playback runs only as fast as frames load. The on-screen FPS indicator shows the drop.
- With *Keep time*, playback waits for a short buffer (a fraction of a second, builder's choice) at play start and after a jump, so it doesn't open on a frozen frame (my call).

### Timeline (owner)

- Each track row shows its loaded frames as **highlighted segments over the empty lane, like a video player's buffer bar**, updating as frames load and unload. Today's single `firstLoadedFrame` + `loadedCount` fill becomes a list of segments, delivered through a bridge accessor (TU separation, developer_notes §1).

### Scope (my calls)

- Movies and image sequences both go through the cache (they share the loader).
- Render and export keep loading each frame on demand (`forceLoad`), bypassing the cache, and must not disturb what the cache holds beyond the frame they force.
- Remote sessions: a peer's cache is local business. Nothing about it goes over the wire.
- Loading carries on while paused, filling ahead of the playhead.

## Non-goals

- Faster decode or upload. The 4K ProRes 4444 load rate (13–17 fps before the upload work, not yet re-measured with it) sets how often *Keep time* skips frames; improving it is separate work. Measure it with this branch and record the number in developer_notes §38/§39.
- A cache size in GB, or VRAM-aware budgeting on discrete GPUs. The percentage-of-RAM preference is the only knob.
- Keeping frames on disk (a disk cache).
- Pinning frames (notes, in/out ranges) beyond what the unload order gives.

## Anti-goals (would make this a failure even if it technically works)

- Playback pacing regressions (developer_notes §13) or new per-tick work that hurts the macOS drag playbook (§5–6).
- Sound drifting out of sync across unloaded stretches, or audio dropping out where picture is missing. The audio clock and slices must keep working regardless of which frames are loaded.
- The memory budget being blown: one frame of overshoot per check is acceptable; unbounded growth is not.
- Texture or IOSurface leaks after clear, reload or many load/unload cycles.
- The cache thrashing on a loop that fits in memory.

## Constraints

- Decoding stays off the GL thread; texture create/upload/delete and returning textures to the pool stay on it, with the context current. Frame textures are freed only through `gfcFrame::deleteTexture()` (developer_notes §39).
- Reuse the upload work on this branch: the texture pool, staging buffers and the IOSurface pool (developer_notes §39). Unloading should hand textures back to the pools.
- Audio for movies: the owner was not asked directly. My call: decode the whole clip's audio independently of picture (about 23 MB per minute), so unloaded picture never means missing sound.
- Thumbnails (developer_notes §15) are kept separately from textures and survive unloading.
- Preferences go through `qt_prefs_persist` / `sett` like every other (developer_notes §27), in Preferences → General beside the existing read-ahead queue cap.
- Tests follow the headless self-test convention (`NAME: pass=N fail=N`, aggregated into `--video-load-test` or a new `--cache-test`), with media generated at test time. Checks the handoff lists: a cache smaller than the clip still serves every frame sequentially and after random jumps; the budget holds; unload order follows the policy; reverse and Bounce prefetch; no leaks after clear/reload; audio stays in sync across unloaded regions. A test can't fill the real machine's RAM, so the budget must be injectable (a test-only byte budget or a fake memory source).
- Build on macOS and Linux (Docker Ubuntu 24.04 at `/Volumes/Archive/jefecheck-builds`); Windows via CI.

## Left to the builder

- The per-frame state and recency/priority structures that replace `loadedFrames`, and the audit of every caller that assumes one contiguous run (listed in the handoff).
- Whether the per-track loader threads stay one per track with a shared coordinator, or become one scheduler; how stale requests are cancelled on a jump.
- The window-ahead logic when loop is on (wrapping to the in point) and the exact pre-play buffer length.
- Where the skipped-frame and slow-down decisions live in the playback tick.
- How the timeline segments are drawn, within "buffer bar" styling and the existing lane look.
- Whether the dead `freeFrames` / `loadForStreaming` code is reused or deleted.

## Review rulings (2026-11-01, made by Claude for the overnight build; the owner will check these)

A design review checked this document against the code (`/private/tmp/claude-501/-Users-dgollas-projects-jefecheck2/22876155-740f-44c6-bc1d-47a56b2afa71/scratchpad/cache-design-review.md`, copied below in substance). These rulings are binding on the plan; where they refine an owner decision above, the owner's intent is kept and the mechanism changes.

1. **Budget = bytes, derived from the system-wide percentage.** The owner's rule ("load while the whole machine is under 90%") stays the trigger, but it is turned into a byte budget so it works across threads and with the pools: `budget = limit% x totalRAM - (systemUsed - cacheBytes)`, where `cacheBytes` counts resident + in-flight (queued/decoding) frames + bytes held in the texture/staging/IOSurface pools. It is recomputed whenever a frame is admitted for decode (not per tick). Admission happens on the decode side before the frame's memory is committed, so the overshoot is at most one frame per track. This also caps VRAM on discrete GPUs at no extra cost. Nothing is unloaded unless a frame is being admitted and `cacheBytes + frameBytes > budget`, which is the owner's "only unload when another frame needs loading and we're over" rule.
2. **Fix the measurement first.** Initialise `gfcMemoryManager::countInactive` (false), use `host_statistics64` with used = active + wired + compressor on macOS, keep the Linux/Windows sources. A test loads N frames of known size and checks the measured delta is plausible (GL/IOSurface memory visible). If GPU memory turns out to be invisible to the system figure, the byte accounting in (1) still bounds it.
3. **Never evict a frame to load one less useful.** Every frame has a priority from its timeline distance to the playhead in the playback direction (the unload order above, reversed). A frame is admitted only if it outranks the lowest-priority resident frame it would evict. So a paused player, or a machine over the limit because of another app, settles and stops: no thrash. A loop that fits never evicts. The window ahead is therefore finite and set by the budget.
4. **Victims are chosen by the scheduler, freed on the GL thread.** The decode side reserves bytes and names victims; `generateTextures` deletes the victims' textures (through `gfcFrame::deleteTexture()`, back to the pools) before uploading. Pool bytes count against the budget (1), so a 4K/1080p mix can't cascade evictions.
5. **One coordinator, four loaders.** A track-manager-level cache coordinator owns the timeline window, the budget and the priority order, and translates to each track's source frames using its offset and trim. The per-track loader threads stay (one per track, own reader) and pull work from the coordinator. A jump calls a new `retarget(timelineFrame)` that bumps a generation counter; loaders drop stale work. It does not go through `startLoading()` (no thread join, no texture clear, no audio reset, no crash-session write, no network LOAD message). Remote `setCurrentFrame`, timeline clicks, keys and playlist advance all retarget. `loadingCanceled` keeps meaning "stop for good".
6. **What an unloaded frame shows.** Each sequence remembers the last frame it actually drew. In Keep time that frame stays on screen. After a jump to an unloaded frame, the plate shows black with a small "Loading" label until the frame arrives.
7. **Show every frame and sound.** Only Keep time lets the audio engine lead. Show every frame stops leading and plays each frame's audio slice as that frame is shown (sound will be choppy when loading is slow; it is the owner's chosen behaviour for clips without sound, and an explicit opt-in otherwise).
8. **Movie audio independent of picture.** The loader's readers run with sound off. A third, audio-only reader mode (demux, discard video packets, decode audio) runs on a low-priority thread and fills the clip's audio from the trim start. `gfcAudioClip` becomes hole-tolerant (filled ranges tracked); on a jump past its fill point the audio reader seeks there and fills ahead, then backfills. Holes play silence. A far jump can be briefly silent until audio catches up.
9. **`savedParams` for every frame** are filled when the sequence is initialised, so `forceLoad` (render/export) works anywhere in the clip.
10. **Hold-edge** uses the trim range, and while hold mode is on the two edge frames are pinned.
11. **No per-cycle leaks.** Unloading deletes the frame's image loader but keeps its thumbnail; a reload that captures no thumbnail does not blank the old one.
12. **Timeline segments are versioned**, so the widget only rebuilds when the loaded set changed.
13. **Tests** added to the list above: measurement validation; mixed-size frames under an injected budget (resident + pooled <= budget + one frame per track); a seek storm (200 random retargets in 2 s: still four loader threads, no GL-thread join, texture/surface counts back to baseline after clear, audio clip never reset, no crash-session writes or LOAD messages); `--render-test` on a clip larger than the budget; four tracks with offsets have identical resident timeline-frame sets; Keep time with an injected slow decoder (audio contiguous, leader frame monotonic, skipped frames later resident); Show every frame (every frame in order, engine not leading); paused on a full budget (zero loads/evictions over 2 s); a loop that fits (zero evictions over 10 loops); hold-edge with the edges outside the window.

Expectation to tell the owner: at today's 13–17 fps 4K 4444 load rate, Keep time skips roughly 30–45% of frames on the first pass by design; the follow-up upload work for the movie path is what closes that.
