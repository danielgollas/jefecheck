# Loading video files as frames, with sound

Date: 2026-10-31. Branch base: `qt-experimental`.

## Why

Clients send dailies and review cuts as a single movie — a ProRes `.mov`, an H.264 `.mp4` — not as image sequences. Today JefeCheck cannot review them: the file dialogs and folder-drop filters exclude movie extensions, and a movie that does reach the loader (any unknown extension falls through to `gfcImageLoaderOIIO`, `gfcframe.cpp:84-105`) shows only its first frame as a still. The only video code is the export encoder (`VideoEncoder_Qt`, FFmpeg CLI). Reviewers end up converting to frames outside the app before a session. The point of this feature is that a client's movie opens like any other shot and is reviewed frame by frame, with its sound, in the same player.

## What success looks like

- A client's ProRes `.mov` or H.264 `.mp4` opens through every existing load path and behaves like an image sequence: frame numbers, step, scrub, trim, notes per frame, any track on any plate.
- Stepping, scrubbing and jumping across loaded frames feel instant (they are resident textures, as for sequences). Loading starts quickly and runs at least as fast as real time.
- One 4K ProRes 4444 track loads at least as fast as real time on an M3 (8 cores), and plays at full rate once loaded.
- Sound plays in sync for every track that has it, mixed together, and never drifts over a long clip.
- A movie and a frame sequence of the same shot look the same in JefeCheck.

## Goals

1. **Formats, first version:** `.mov` and `.mp4` containers; the codecs clients actually send in them (ProRes 422/422 HQ/4444/4444 XQ, H.264, H.265). Other containers (MXF with DNxHD/DNxHR first) come later.
2. **Decoded in memory, never written to disk** (owner's decision). No extraction to an image sequence, no temp files.
3. **Behaves like a sequence.** A movie is one file holding many frames. It goes through the same track/sequence model, the same frame range, trims, decode scale, hold mode, offsets, filmstrip and loaded-fill as an image sequence.
4. **Colour like a VFX player, not like QuickTime** (owner's decision). YUV is converted to RGB using the file's own tags (matrix: Rec.709 / Rec.601 / Rec.2020; range: video or full) and the resulting code values are shown straight, the way RV and Nuke do and the way JefeCheck already shows DPX/EXR. Any look is applied with the existing LUTs and colour correction.
5. **Precision kept.** 10- and 12-bit sources reach the screen through the existing 16-bit path when the track's depth setting is 16-bit, as for sequences; movies follow the track's depth setting rather than forcing one — my call. The existing 16-bit path uploads into `GL_RGBA16F_ARB` (half float, 11 significant bits, `gfcimageloaderoiio.cpp:427-431`): exact for 10-bit, losing the lowest bit of 12-bit values in the upper range. Accepted for review — my call; colour tests run at 16-bit depth.
6. **Sound, in sync** (owner's decision). Every track whose movie has audio plays it; all tracks mix together.
7. **Mute per track on the timeline lane header** (owner's decision, picked from mockups): a speaker toggle next to the track letter A–D, shown only on tracks whose media has audio, plus a thin waveform strip under that lane's filmstrip that dims when muted.
8. **Audio on step and scrub** (owner's decision): stepping or scrubbing plays that frame's slice of sound (one frame's duration, e.g. 1/24 s), the way Avid and RV do.
9. **Same code on every platform**, ideally (owner's preference; per-platform code is acceptable where it must exist).

## Non-goals

- Matching QuickTime Player's colour (its colour-managed Rec.709 display, the "gamma shift"). Explicitly not wanted.
- Writing extracted frames anywhere.
- MXF / DNxHR, image-sequence audio (WAV beside a sequence), and other containers in the first version.
- Audio in rendered/exported video (Render dialog output stays picture-only in the first version).
- Streaming movie media to remote participants. As with sequences today, each side needs its own copy of the file.
- Hardware decode in the first version (see Performance; it is the planned follow-up).
- **Clips longer than memory** (owner's decision). A movie loads exactly like an image sequence: frames are decoded into memory until the existing memory limit (`gfcMemoryManager::withinLimits`) stops loading, and once loaded they are no different from any other frames. A rolling cache that keeps loading ahead of the playhead and evicts least-recently-used frames does not exist today for any media (`gfcSequence::loadForStreaming` is an empty stub, `gfcSequence.cpp:1286`); it is a separate later project that would serve sequences and movies alike, and this work must not build it.

## Anti-goals — this fails even if it technically works when

- **Colours differ** from the same shot loaded as frames, because tags were ignored (wrong matrix or range → washed-out or shifted colour).
- **Audio drifts** from picture over a long clip, e.g. because 23.976 was treated as 24.
- Loading or an alt-click reload from a frame mid-clip takes visible time to start (this, and the follow-up rolling cache, is why fast seeking still matters and rules out OIIO's FFmpeg reader, below).
- Step/scrub audio clicks, or lags behind the playhead.
- A movie whose name contains digits (`shot_v003.mov`) is treated as frame 3 of an image sequence.

## Decided: how it is built

**Link FFmpeg's libraries** (`libavformat`, `libavcodec`, `libavutil`, `libswresample`; `libswscale` only if a CPU conversion fallback is wanted). This reverses the current "FFmpeg as a CLI subprocess, never linked" rule *for reading*; export keeps using the CLI as it does today. `CLAUDE.md` and `developer_notes.md` (§18–20) state the old rule and must be updated.

Why, measured on this M3 with 20 s 1080p test clips:

| | OIIO's FFmpeg reader | libav, direct seek |
|---|---|---|
| Sequential, ProRes 422 HQ | 15.7 ms/frame | ~12 ms |
| Jump to frame 400 | 2.2 s | 8 ms |
| Step back one frame | 1.1 s | 12 ms |

OIIO's reader (present in Homebrew's OIIO) decodes forward from the start on every non-sequential read, so it is unusable for frame-by-frame review. AVFoundation/Media Foundation would be per-platform. libav is packaged everywhere: Homebrew `ffmpeg` (already a dependency of Homebrew OIIO), Ubuntu `libavformat-dev libavcodec-dev libavutil-dev libswresample-dev`, MSYS2 `mingw-w64-x86_64-ffmpeg`. Benchmark code is reproducible: a seek to the nearest keyframe before the target (`AVSEEK_FLAG_BACKWARD`), `avcodec_flush_buffers`, decode forward to the target PTS. (The benchmark's failure on the very last frame was a test bug — it did not drain the decoder at EOF with a null packet. The real reader must drain.)

**Licensing: JefeCheck becomes "GPL v2 or later"** (owner's decision; the owner is the original author). Stock packaged FFmpeg builds are GPLv3 (Homebrew uses `--enable-version3`, for OpenSSL), which cannot be linked into a v2-only program; v2-or-later can. Consequences to carry out as part of this work: update `LICENSE`/`README`/`CLAUDE.md` wording and the project's own source headers to "GPL v2 or later", add FFmpeg (linked) to `THIRD_PARTY_LICENSES` and correct its RakNet entry (line 16 says "v2.0"; the vendored RakNet headers say "version 2 … or any later version", `src/RakPeer.cpp:12-15`, so linking GPLv3 FFmpeg is fine), and note that distributed binaries are effectively under GPLv3. Vendored third-party code (RakNet, xmlParser) keeps its own terms.

**Audio output through Qt Multimedia (`QAudioSink`)** — my call: it is Qt, cross-platform, and avoids a new non-Qt dependency. Adds the Qt Multimedia package to all three platforms (Homebrew `qt` includes it; Ubuntu `qt6-multimedia-dev`; MSYS2 `mingw-w64-x86_64-qt6-multimedia`). Mixing the four tracks is done in JefeCheck (sum, clamp), not by opening four sinks. The builder may substitute a different backend if Qt Multimedia proves unfit for low-latency step slices; say why in the plan.

**YUV→RGB on the GPU** (my call). Upload the decoded planes (or a packed 16-bit YUV) and convert in a shader, rather than converting on the CPU with swscale. Measured: CPU conversion to RGB48 roughly doubles per-frame cost at 4K (7.6 → 14.7 ms) and uploads more bytes. The conversion must use the file's matrix and range; untagged files default to Rec.709, video range (my call, the HD convention). Chroma upsampling for 4:2:2/4:2:0 is a builder decision; it must not shift chroma by half a pixel. How this conversion composes with the super-shader (`gfcPlate::buildShader`), the FX stack's texture inputs, the filmstrip thumbnail capture (which reads a BGRA CPU buffer today, §15) and render read-back is the builder's to design, but **every downstream consumer must see RGB identical to what the screen shows.** A CPU conversion fallback is acceptable where the GPU path cannot reach (e.g. thumbnails).

## Decided: behaviour

**Opening.** `.mov` and `.mp4` are accepted by Quick Load (Cmd+O), drag-and-drop onto the viewport and onto timeline lanes, the Load Sequence Manager (Cmd+L), playlists (Add Files…), sessions and review packages. A movie always loads as one clip of many frames; it bypasses the numbered-filename sequence detection in `gfcSequence.cpp` (the `findSequence`/numbering code), so `shot_v003.mov` is never read as frame 3 of a sequence, and two movies `a_001.mov`, `a_002.mov` in one folder are never joined.

**Frame numbering** (owner's decision). The timeline keeps counting 1…n for movies exactly as for sequences; frame counter, notes, offsets and the remote protocol are unchanged. A movie's start timecode (the `tmcd` track or `timecode` metadata; 00:00:00:00 when absent) is shown beside the frame counter in the status bar as the current frame's source timecode, computed at the movie's exact rate. Drop-frame timecode is displayed as drop-frame. The existing `gfcPlaybackManager::updateTimeCode` rounds the rate to an integer (`(int)targetFPS`, `gfcPlaybackManager.cpp:675-680`, so 23.976 counts as 23); the source-timecode display must not reuse that rounding.

**Frame rate** (owner's decisions unless marked).
- The player keeps its single rate (the FPS box, `gfcPlaybackManager::targetFPS`), saved with the session as today. **Loading a movie sets the player's rate to the movie's rate.** When several movies are loaded, the most recently loaded one sets it; the user can still change the FPS box — my call.
- The rate must be held exactly: carry the rational (e.g. 24000/1001) alongside the `float`, and pace and convert time from the rational whenever the box holds a movie's rate, so 23.976 never drifts against its audio. How the rational is stored and saved is the builder's call.
- **Other tracks play at the player's rate**, one source frame per timeline frame, even when their native rate differs (a 25 fps track under a 23.976 player plays slightly slow). No retiming by repeating or dropping frames.
- Variable-frame-rate files (phone footage) are treated as constant at the stream's average rate, with a note in the status bar — my call.

**Sound.**
- All tracks with audio play at once, mixed. Each track's speaker toggle (lane header) mutes it. Mute state is per track, saved with the session — my call.
- **Native-rate playback: the audio clock leads.** When a track is playing at its own native rate (the player's rate equals its movie's rate), its sound plays continuously and the picture follows the audio position; if the picture cannot keep up, frames are dropped and the sound never stutters. With every audible track muted, or no audio at all, playback keeps the current FPS pacing (§13).
- **Off-rate playback: sound follows picture in slices, at normal pitch** (owner's decision). A track playing at any rate other than its native one — the FPS box changed, or a 25 fps track under a 23.976 player — plays each displayed frame's own slice of sound at normal pitch, the same mechanism as step/scrub slices. Faster than native: each slice is cut to the time that frame is on screen (the rest is skipped). Slower than native: each slice plays in full and is followed by silence until the next frame. Pitch and speed of the sound itself never change; no time-stretching or resampling for speed. With no native-rate audible track, the FPS pacing (§13) leads. When native and off-rate audible tracks are mixed (e.g. a 23.976 movie that set the rate, plus a 25 fps one), the native track's sound stays continuous and leads the clock, and the off-rate track plays slices of whichever frame is on screen — my call, so the shot that set the rate never sounds choppy.
- Track offsets (timeline drag) shift that track's audio with its picture. Hold mode, trims and loop/ping-pong apply to audio as they do to picture; reverse playback is silent (my call).
- **Step and scrub play the frame's slice** of audio: exactly one frame's duration, with a few-millisecond fade in and out so it does not click (my call). During a fast scrub, a slice that would start late is dropped rather than queued, so sound never lags the playhead (my call).
- Multi-channel audio is downmixed to the output device's layout; sample-rate conversion to the device rate via `libswresample` (my call).
- **Waveform:** a thin strip under the track's filmstrip in the timeline lane, painted from a per-movie peak summary computed in the background after load. It dims when the track is muted. It follows the lane's existing layout rules (§14–15: one painted widget, no forced extra height).

**The rest of the app.**
- **Notes** attach per frame as for sequences (keyed by timeline frame, as today).
- **Fingerprint:** `fp1:` today samples the first/middle/last *files* of a sequence through `OIIO::ImageInput` (`gfcMediaFingerprint.cpp:37, 67`). Movies need a movie-aware variant — one path, sampling decoded frames (or container bytes) — with the same `fp1:` shape so relinking and review-package merge keep working; `sequencesIn` must list a movie as one entry. The variant's exact sampling is the builder's call; it must be stable across platforms and FFmpeg versions (so prefer container bytes over decoded pixels) — my call.
- **Stamp Notes and anything that re-reads the displayed frame by path** (`framePathAt` → `sourcePath`, `SequenceLoadBridge_qt.cpp:3408`) must get the decoded movie frame, not the `.mov` bytes. Stamping from a movie reads the frame through the movie reader — my call.
- **Sessions, playlists, review packages** store the movie path like any media path. A review package that includes media copies the movie file itself.
- **Render/export** works unchanged on the decoded frames; output is silent in the first version.
- **Remote sessions:** unchanged; each participant needs the file locally, as with sequences.

## Performance

Measured on the M3, 5 s synthetic 4K DCI ProRes 4444 (~340 Mbit/s; real 4444 is nearer 1 Gbit/s and will decode slower — expect ~15–25 ms):

| Path | Per frame | CPU |
|---|---|---|
| libav software decode, all cores | 7.6 ms | most of the CPU |
| + CPU conversion to RGB48 | 14.7 ms | about double |
| VideoToolbox hardware decode | 8.7 ms | near zero |

Real time at 24 fps is 41.7 ms per frame, so one 4K 4444 track fits in software. **Target: one real 4K ProRes 4444 track decodes and uploads at least at 24 fps while loading on the M3; measure on real footage during the build.** Because loaded frames are resident textures, decode speed sets load time, not playback smoothness. Several 4K tracks at once will saturate the CPU in software; that is what the hardware-decode follow-up (libav's generic `hwaccel` API — one code path, per-platform device types) is for. The plan should leave the decode path shaped so hwaccel can slot in without restructuring.

The existing loader model (§13, §15, `gfcSequence`'s loader thread, read-ahead queue cap, contiguous loaded fill) decodes a whole range ahead into textures. Movies use the same model unchanged: the loader thread's single pass over the range decodes the movie sequentially through one open demuxer/decoder per track, seeking only to the pass's start (load range, alt-click load-from-frame, render `forceLoad` of an unloaded frame). Keep the reader seekable to any frame cheaply, because the later rolling cache will depend on it. Memory for long 4K clips is the same limitation sequences already have; the builder must not make it worse (no duplicate full-resolution copies per frame).

## Constraints

- **TU separation (developer_notes §1):** libav and Qt Multimedia includes stay out of glad translation units; the new reader sits behind the existing loader interface (`gfcFrame::loadFrame` / `gfcGLFrameInfo`) or a sibling of it, and Qt UI reaches it only through `jefe::qt::*` bridge accessors.
- The current loaders are per-file (one file → one image) and open the file on every load. A movie needs one file → many frames and an open decoder kept across frames. How that maps onto `gfcSequence`/`gfcFrame` is the builder's main design job.
- Build: CMake `find_package`/pkg-config for libav and `Qt6::Multimedia` on all three platforms; CI (`build.yml`, `release.yml`) installs the new packages; release packaging bundles the FFmpeg shared libraries on macOS (`.app`/tarball) and Windows (the existing `ldd … | grep mingw` copy step will pick up MSYS2's DLLs — verify), and declares them as Linux package dependencies.
- macOS GL is 2.1 via Metal translation; ARB shader functions only (CLAUDE.md, Platform notes). The YUV shader must follow the same rules as the super-shader.
- No automated suite exists; follow the project's headless self-test convention (`--render-test`, `--cc-test`, …).

## Verification (what the plan must include)

- **`--video-load-test`** (headless): generates or reads small ProRes 422 HQ, ProRes 4444 and H.264 clips; asserts frame count, rate as a rational, start-timecode numbering, a numbered movie filename not being sequence-detected, random-access correctness (frame N decoded after a jump equals frame N decoded sequentially, bit-exact) and backward step.
- **Colour test:** a movie and a DPX/EXR/PNG sequence of the same SMPTE bars (made with the FFmpeg CLI, both Rec.709 video range and full range, plus one untagged clip) render to the same RGB within a 10-bit tolerance through the same plate.
- **Sync test:** a clip with a flash and beep every second; after a full playback the beep and flash still land on the same frame, at 24000/1001 and 25 fps.
- **Step-slice test:** stepping emits exactly one frame's samples per step, with fades.
- **Off-rate test:** at 2× and 0.5× the FPS box, each displayed frame emits only its own samples at unchanged sample rate (cut short at 2×, followed by silence at 0.5×); loading a 24000/1001 movie sets the player's rate to exactly that rational, and the status-bar source timecode for frame n matches FFmpeg's timecode for that frame.
- **Performance:** real 4K ProRes 4444 footage plays at 24 fps on the M3 with no dropped frames reported.
- UI: the speaker toggle appears only on tracks with audio; mute persists in a saved session. The lanes are one painted widget (§14), so per-lane toggles are verified manually like the rest of the lanes, not through `locators.py` — my call; a bridge-level self-test covers mute state.

## Left to the builder

- The movie reader's shape and how it plugs into `gfcSequence`/`gfcFrame` and the loader thread.
- GPU YUV upload format and shader, chroma upsampling filter, and how it composes with the super-shader, FX inputs, thumbnails and render read-back (with the "everyone sees the same RGB" rule above).
- Audio engine internals: buffering, the master-clock handoff to `gfcPlaybackManager`, device-change handling.
- Waveform peak summary format and its painting.
- Exact CMake/CI/packaging steps per platform.
- Order of work. A sensible order is picture first (reader, colour, numbering, load paths), then sound (engine, sync, step slices), then the timeline mute/waveform UI, then licensing/packaging — but the builder may reorder.
