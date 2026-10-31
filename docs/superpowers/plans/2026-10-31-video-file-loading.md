# Video File Loading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A client's `.mov`/`.mp4` opens through every load path as one clip of many frames, decoded in memory, shown with VFX-player colour from its own tags, with its sound mixed in sync, a per-track mute, and a waveform in the timeline.

**Architecture:** A libav-backed `gfcMovieReader` (one open demuxer/decoder per track, seekable to any frame) sits behind a new `gfcImageLoaderMovie` that fills `gfcGLFrameInfo` with YUV planes; `gfcFrame::generateTexture` converts them on the GPU once, at upload, into the same resident RGBA rectangle texture a sequence frame has, so the super-shader, FX, thumbnails and render see one RGB. Audio is decoded per track into a 48 kHz stereo float `gfcAudioClip`; a pure `gfcAudioEngine` renders per-frame slices (contiguous when a track plays at its native rate, cut/padded otherwise, faded on step/scrub) into a Qt Multimedia `QAudioSink`, and leads `gfcPlaybackManager`'s clock during forward native-rate playback. The rate is carried as an exact rational.

**Tech Stack:** C++20, libavformat/libavcodec/libavutil/libswresample/libswscale (FFmpeg ≥ 6.1 API), Qt6 Multimedia (`QAudioSink` pull mode), OpenGL 2.1 ARB shader objects + EXT framebuffers (existing), CMake, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-31-video-file-loading-design.md`

## Global Constraints

- Formats, first version: `.mov` and `.mp4` containers only; ProRes 422/422 HQ/4444/4444 XQ, H.264, H.265. No MXF/DNxHR, no WAV beside sequences, no audio in exports, no hardware decode, no rolling cache (the decode path must still leave a seam for libav `hwaccel`).
- Decoded in memory, never written to disk: no extraction, no temp files, no duplicate full-resolution CPU copies per frame.
- A movie behaves like a sequence: same track/sequence model, frame range, trims, decode scale, hold mode, offsets, filmstrip, loaded fill; timeline counts 1…n; notes keyed by timeline frame; remote protocol unchanged.
- Colour like a VFX player: YUV→RGB with the file's own matrix (Rec.709/601/2020) and range (video/full); untagged defaults to **Rec.709, video range**; code values shown straight; chroma upsampling must not shift chroma by half a pixel. Every downstream consumer (super-shader, FX inputs, thumbnails, render read-back) sees RGB identical to the screen.
- Precision: 10/12-bit sources reach the screen through the existing 16-bit path (`GL_RGBA16F_ARB`) when the track's depth is 16-bit; movies follow the track's depth setting.
- Numbering: `shot_v003.mov` is never frame 3 of a sequence; `a_001.mov` and `a_002.mov` are never joined; a movie is one entry in `sequencesIn`.
- Frame rate: the player keeps one rate (FPS box); loading a movie sets it to the movie's rate (most recent wins); the rate is held as a rational (e.g. `24000/1001`) alongside the float; other tracks play one source frame per timeline frame at the player's rate; VFR files are treated as constant at the stream's average rate with a status note. Source timecode display must not reuse `updateTimeCode`'s `(int)targetFPS` rounding; drop-frame shows as drop-frame.
- Sound: all tracks with audio play mixed (sum, clamp) through one sink; mute per track, saved with the session; native-rate playback: the audio clock leads and picture drops frames rather than sound stuttering; off-rate playback: each displayed frame's own slice at normal pitch (cut when faster, silence-padded when slower), never time-stretched; step/scrub plays exactly one frame's slice with a few-ms fade; a late slice during a fast scrub is dropped, not queued; reverse playback is silent; multi-channel downmixed, sample rate converted via libswresample; offsets/hold/trims/loop apply to audio as to picture.
- Waveform: thin strip under the lane's filmstrip from a background-computed peak summary; dims when muted; lanes stay one painted widget with no forced extra height (developer_notes §14–15).
- TU separation (developer_notes §1): libav and Qt Multimedia includes stay out of glad translation units; Qt UI reaches movie/audio state only through `jefe::qt::*` bridge accessors in `SequenceLoadBridge_qt.h`. libav headers are confined to `src/gfcMovieReader.cpp` (pimpl); Qt Multimedia to `src/qt/AudioOutput_qt.cpp`.
- macOS GL is 2.1 via Metal translation: ARB shader functions only (`glCreateShaderObjectARB` …), `GL_TEXTURE_RECTANGLE_ARB`, EXT framebuffers; initialise every GL handle to 0.
- Build: CMake finds libav and `Qt6::Multimedia` on macOS (Homebrew), Ubuntu 24.04 (apt, FFmpeg 6.1 → libavcodec 60) and MSYS2 mingw64; use only APIs present in FFmpeg 6.1 (`avcodec_send_packet`/`receive_frame`, `AVChannelLayout`, `swr_alloc_set_opts2`) and Qt 6.4 (`QAudioSink::start(QIODevice*)`, not the 6.11 callback API). CI installs the new packages; release packaging bundles libav dylibs on macOS, DLLs + the Qt multimedia plugin on Windows, and declares Linux package dependencies.
- Licensing: JefeCheck becomes **"GPL v2 or later"** (LICENSE/README/CLAUDE.md/About/CLA wording, SPDX headers on project-own sources); `THIRD_PARTY_LICENSES` gains FFmpeg (linked) and corrects RakNet to "v2 or later"; distributed binaries are noted as effectively GPLv3. Export keeps the FFmpeg CLI.
- Verification: headless self-tests only (`NAME: pass=N fail=N` lines, exit 0/2); test media is generated with the ffmpeg CLI at test time into a temp dir, never committed.
- Performance target: one real 4K ProRes 4444 track decodes + uploads at ≥ 24 fps on the M3 and plays at full rate once loaded; measure on real footage.
- Build tree: `/Volumes/Archive/jefecheck-builds/video`. Every shell command in this plan is a single line.

## Review Focus

1. A `.mov`/`.mp4` that is empty, truncated, or has no video stream (audio-only podcast `.mp4`) — the track must report an error and stay empty, loading must stop cleanly, nothing crashes. Pinned in Task 5 (MOVIE-LOADER: `empty.mov`, `truncated.mov`).
2. A session or playlist whose movie was moved and is only findable through Preferences → Search Paths — movies must relink through `findFileInSearchPaths` like sequences. Pinned in Task 4 (MOVIE-NUMBERING search-path check).
3. Phone footage with odd dimensions (e.g. 321×181, 4:2:0) — chroma planes are `ceil(w/2)`; the frame must decode and render at the right size with no shear or crash. Pinned in Task 6 (`bars_709tv_odd.mp4`).
4. No usable audio output device (headless CI, a Mac with no output, a device that refuses 48 kHz float) — playback must keep FPS pacing, steps stay silent, nothing blocks. Pinned in Task 11 (AUDIO-PLAYBACK null-output check).
5. Audio that is not 48 kHz stereo (44.1 kHz mono AAC, 5.1 ProRes) — must downmix/resample so slice lengths stay exact at 48 kHz. Pinned in Task 9 (`mono441_24.mp4`).

---

## File Structure

| File | Responsibility |
|---|---|
| `cmake/FindFFmpeg.cmake` (new) | Imported targets `FFmpeg::avformat/avcodec/avutil/swresample/swscale` via `find_path`/`find_library` (no pkg-config dependency; works on brew, apt, MSYS2). |
| `src/gfcMediaKinds.h` (new, header-only) | `gfcIsMovieFile(path)` — the single extension test (`.mov`/`.mp4`, case-insensitive) shared by numbering, notes, fingerprint and UI. |
| `src/gfcMovieReader.{h,cpp}` (new) | libav demux/decode behind a pimpl: probe, open, random-access `readFrame`, audio FIFO, swscale BGRA fallback. The only TU that includes libav. |
| `src/gfcTimecode.{h,cpp}` (new) | SMPTE timecode parse/format/arithmetic with drop-frame; self-test. |
| `src/gfcimageloadermovie.{h,cpp}` (new) | `gfcImageLoaderMovie : gfcImageLoader` — one frame of a movie through the existing loader interface. |
| `src/gfcYuvConverter.{h,cpp}` (new) | GPU YUV→RGB pass (plane textures, ARB shader, EXT FBO) producing the resident RGBA rect texture; CPU RGBA8 path for thumbnails. |
| `src/gfcAudioClip.{h,cpp}` (new) | Per-track decoded audio (48 kHz stereo float), exact frame→sample mapping, peak summary. |
| `src/gfcAudioEngine.{h,cpp}` (new) | Mixer + slice transport + leading clock + `gfcAudioOutput` interface + `gfcFakeAudioOutput` for tests. Pure C++. |
| `src/gfcMovieSelfTests.{h,cpp}` (new) | Self-tests that need the rendering-chain managers (sequence/rate/session/package/mute), declared in a Qt-safe header. |
| `src/qt/MovieTestMedia_qt.{h,cpp}` (new) | Generates the test clips with the ffmpeg CLI (`QProcess`, `VideoEncoder_Qt::findFfmpeg`). |
| `src/qt/AudioOutput_qt.{h,cpp}` (new) | `QAudioSink` pull-mode `gfcAudioOutput`; header exposes no Qt Multimedia types. |
| `src/gfcSequence.{h,cpp}`, `src/gfcframe.{h,cpp}`, `src/gfcloadparams.{h,cpp}`, `src/gfcglframeinfo.h` | Movie bypass in `findSequence`, shared reader per sequence, loader dispatch, planar frame info, audio clip ownership, mute. |
| `src/gfcPlaybackManager.{h,cpp}` | Rational rate, pure frame-advance rule, audio-clock hook. |
| `src/gfcMediaFingerprint.cpp`, `src/gfcNoteStore.cpp`, `src/gfcNoteStamp.{h,cpp}`, `src/gfcSessionManager.cpp` | Movie-aware fingerprint, path normalisation, stamping from pixels, rational in sessions. |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}`, `src/qt/MainWindow_qt.{h,cpp}`, `src/qt/TrackStrip_qt.cpp`, `src/qt/PlaylistPanel_qt.cpp`, `src/qt/TimelinePanel_qt.{h,cpp}`, `src/qt/qticons.{h,cpp}` | File filters, status-bar timecode, mute/waveform accessors and painting, GL test harnesses. |
| `src/main_qt.cpp` | `--video-load-test`, `--video-colour-test <dir>`, `--video-perf-test <movie>` dispatch. |
| `CMakeLists.txt`, `.github/workflows/{build,release}.yml`, `scripts/*.sh`, `packaging/*` | Link, CI packages, bundling. |
| `LICENSE`, `README.md`, `CLA.md`, `CLAUDE.md`, `developer_notes.md`, `THIRD_PARTY_LICENSES`, `packaging/ffmpeg-NOTICE.txt` | Licensing and documentation. |

Test convention used below: each suite is `int <name>SelfTest(const std::string& mediaDir)` using the project's `check(cond, "msg")` lambda, printing `<SUITE>: pass=N fail=N`, returning the fail count. `--video-load-test` runs every suite and exits 0 only when all pass (mirrors `--notes-test`, `src/main_qt.cpp:632-655`). Helpers named in the test snippets but not in any interface (`readFile`, `waitUntil(pred, ms)`, `nonZero`, `countZeroCrossings(buf, from, to)`, `bytesOf`) are file-local lambdas in `src/gfcMovieSelfTests.cpp`. Setting `JEFE_KEEP_TEST_MEDIA=1` makes the aggregator keep the generated media directory and print its path (`VIDEO-LOAD-TEST media: <dir>`); the manual checks below refer to it as `<dir>`.

---

### Task 1: Link libav and Qt Multimedia; test-flag skeleton

**Files:**
- Create: `cmake/FindFFmpeg.cmake`, `src/gfcMediaKinds.h`, `src/gfcMovieReader.h`, `src/gfcMovieReader.cpp` (version function only), `src/gfcMovieSelfTests.h`, `src/gfcMovieSelfTests.cpp`
- Modify: `CMakeLists.txt:8-21,115-136`, `.github/workflows/build.yml:37-56,69-70,94-105`, `.github/workflows/release.yml:16-25,49-50,83-95`, `src/main_qt.cpp:195-202,632-655`

**Interfaces:**
- Produces: `bool gfcIsMovieFile(const std::string& path)` (inline, `src/gfcMediaKinds.h`; true for `.mov`/`.mp4` any case); `static std::string gfcMovieReader::libavVersionString()`; CMake imported targets `FFmpeg::avformat`, `FFmpeg::avcodec`, `FFmpeg::avutil`, `FFmpeg::swresample`, `FFmpeg::swscale` and `Qt6::Multimedia` linked into `jefecheck`; self-test suite functions `int movieKindsSelfTest(const std::string&)`, `int movieLibavSelfTest(const std::string&)` in `src/gfcMovieSelfTests.cpp`; the `--video-load-test` aggregator in `main_qt.cpp` (runs after `QApplication` and `jefe::qt::initializeRenderingChain()`, like `--playlist-test`; later tasks append suites to it).

- [ ] **Step 1: Write the failing tests** — in `src/gfcMovieSelfTests.cpp`:

```cpp
// MOVIE-KINDS
check(gfcIsMovieFile("/x/shot.mov") && gfcIsMovieFile("/x/SHOT.MP4") && gfcIsMovieFile("a.Mov"), "mov/mp4 any case");
check(!gfcIsMovieFile("/x/shot.0001.exr") && !gfcIsMovieFile("/x/clip.mxf") && !gfcIsMovieFile("movie"), "others rejected");
// MOVIE-LIBAV
check(gfcMovieReader::libavVersionString().rfind("libavcodec ", 0) == 0, "libav linked and reports a version");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake -S . -B /Volumes/Archive/jefecheck-builds/video -DCMAKE_BUILD_TYPE=Debug && cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: configure/build error (no FFmpeg targets, missing headers).

- [ ] **Step 3: Write `cmake/FindFFmpeg.cmake`** — for each of `avformat avcodec avutil swresample swscale`: `find_path(FFMPEG_<lib>_INCLUDE_DIR lib<lib>/<lib>.h HINTS /opt/homebrew/opt/ffmpeg/include /usr/local/include ENV FFMPEG_ROOT)` and `find_library(FFMPEG_<lib>_LIBRARY <lib> HINTS /opt/homebrew/opt/ffmpeg/lib …)`, an `IMPORTED` target `FFmpeg::<lib>` per library, `find_package_handle_standard_args(FFmpeg …)`. Decision (plan): `find_path`/`find_library` instead of pkg-config so no new tool is needed on macOS (no `pkgconf` is installed here).

- [ ] **Step 4: Wire CMake** — in `CMakeLists.txt`: `list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)`, `find_package(FFmpeg REQUIRED)`, add `Multimedia` to the `find_package(Qt6 … COMPONENTS …)` list, link the five `FFmpeg::*` targets and `Qt6::Multimedia` to `jefecheck`.

- [ ] **Step 5: Add `src/gfcMediaKinds.h`, `gfcMovieReader.{h,cpp}` (version only) and the self-test TU** — `gfcMovieReader.cpp` includes `<libavcodec/avcodec.h>` inside `extern "C"` and implements `libavVersionString()` as `"libavcodec " + std::to_string(LIBAVCODEC_VERSION_MAJOR) + "." + …`. `src/gfcMovieSelfTests.h` declares only `int …SelfTest(const std::string& mediaDir);` functions (no manager includes, so `main_qt.cpp` can include it).

- [ ] **Step 6: Add the `--video-load-test` aggregator** to `src/main_qt.cpp` next to `--notes-test`: `hasVideoLoadTest(argc, argv)`; after `applyDarkTheme`, call `jefe::qt::initializeRenderingChain()`, run each suite with `mediaDir` = `""` for now, `fflush(stdout)`, `std::_Exit(allZero ? 0 : 2)`.

- [ ] **Step 7: CI packages** — `build.yml` and `release.yml`: Ubuntu adds `libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev qt6-multimedia-dev`; macOS `brew install` adds `ffmpeg`; MSYS2 adds `mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-qt6-multimedia`.

- [ ] **Step 8: Build and run** — Run: `cmake -S . -B /Volumes/Archive/jefecheck-builds/video -DCMAKE_BUILD_TYPE=Debug && cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-KINDS: pass=2 fail=0`, `MOVIE-LIBAV: pass=1 fail=0`, exit 0.

- [ ] **Step 9: Commit** — `git add cmake/FindFFmpeg.cmake CMakeLists.txt .github/workflows src/gfcMediaKinds.h src/gfcMovieReader.h src/gfcMovieReader.cpp src/gfcMovieSelfTests.h src/gfcMovieSelfTests.cpp src/main_qt.cpp && git commit -m "build: link libav and Qt Multimedia; add --video-load-test skeleton"`

---

### Task 2: Test-media generator and the movie reader (picture)

**Files:**
- Create: `src/qt/MovieTestMedia_qt.h`, `src/qt/MovieTestMedia_qt.cpp`
- Modify: `src/gfcMovieReader.h`, `src/gfcMovieReader.cpp`, `src/gfcMovieSelfTests.cpp`, `src/main_qt.cpp` (aggregator generates media first)

**Interfaces:**
- Consumes: `VideoEncoder_Qt::findFfmpeg()` (`src/qt/VideoEncoder_qt.h:27`).
- Produces (`src/gfcMovieReader.h`):

```cpp
enum class gfcYuvMatrix  { BT709, BT601, BT2020, Unspecified };
enum class gfcYuvRange   { Video, Full, Unspecified };
enum class gfcChromaSiting { Left, Center, TopLeft };
struct gfcMovieInfo {
    int width = 0, height = 0; int64_t frameCount = 0;
    int rateNum = 0, rateDen = 1; bool variableRate = false;
    int bitDepth = 8; int subX = 1, subY = 1; bool hasAlpha = false; bool planarYuv = false;
    std::string pixFmt, codecName, startTimecode;        // startTimecode filled in Task 3
    gfcYuvMatrix matrix = gfcYuvMatrix::Unspecified; gfcYuvRange range = gfcYuvRange::Unspecified;
    gfcChromaSiting siting = gfcChromaSiting::Left;
    bool hasAudio = false; int audioSampleRate = 0, audioChannels = 0; double durationSeconds = 0;
};
struct gfcMoviePlane { const uint8_t* data = nullptr; int width = 0, height = 0, strideBytes = 0; };
struct gfcMovieFrame { int64_t index = -1; int width = 0, height = 0, bitDepth = 8; bool planarYuv = false;
                       int planeCount = 0; gfcMoviePlane plane[4]; std::shared_ptr<void> owner; };
class gfcMovieReader {
public:
    static bool probe(const std::string& path, gfcMovieInfo& out, std::string* err);   // cached by path+size+mtime, 16 entries
    bool open(const std::string& path, std::string* err); void close(); bool isOpen() const;
    const gfcMovieInfo& info() const;
    bool readFrame(int64_t index, gfcMovieFrame& out, std::string* err);               // thread-safe (internal mutex)
    bool convertToBGRA(const gfcMovieFrame& in, int bitsPerComponent, int outW, int outH, std::vector<uint8_t>& out, std::string* err);
    static std::string libavVersionString();
};
```
  and `namespace jefe::qt::testmedia { bool generate(const QString& dir, QString* err); }` producing these files in `dir`: `prores422hq_23976.mov`, `prores4444_24.mov`, `h264_25.mp4`, `shot_v003.mov`, `a_001.mov`, `a_002.mov`, `bars_truth.0001.png`…`0008.png`, `bars_709tv.mov`, `bars_601full.mp4`, `bars_untagged.mp4`, `bars_709tv_odd.mp4`, `flashbeep_23976.mov`, `flashbeep_25.mov`, `df_2997.mov`, `mono441_24.mp4`, `truncated.mov`, `empty.mov`.

- [ ] **Step 1: Write the failing test** — `int movieReaderSelfTest(const std::string& dir)` (MOVIE-READER):

```cpp
gfcMovieInfo i; std::string err;
check(gfcMovieReader::probe(dir+"/prores422hq_23976.mov", i, &err), "probe prores");
check(i.width==320 && i.height==180 && i.frameCount==48 && i.rateNum==24000 && i.rateDen==1001, "size/count/rate 24000/1001");
check(i.bitDepth==10 && i.subX==2 && i.subY==1 && i.planarYuv && i.pixFmt=="yuv422p10le", "422 10-bit planar");
check(i.matrix==gfcYuvMatrix::BT709 && i.range==gfcYuvRange::Video && !i.variableRate && !i.hasAudio, "tags + CFR");
gfcMovieInfo p4; check(gfcMovieReader::probe(dir+"/prores4444_24.mov", p4, &err) && p4.bitDepth==12 && p4.subX==1 && p4.subY==1 && p4.rateNum==24 && p4.rateDen==1, "4444 12-bit 444 24/1");
gfcMovieInfo h; check(gfcMovieReader::probe(dir+"/h264_25.mp4", h, &err) && h.bitDepth==8 && h.subX==2 && h.subY==2 && h.rateNum==25 && h.rateDen==1 && h.siting==gfcChromaSiting::Left, "h264 420 25/1 left-sited");
gfcMovieInfo u; check(gfcMovieReader::probe(dir+"/bars_untagged.mp4", u, &err) && u.matrix==gfcYuvMatrix::Unspecified, "untagged reports Unspecified");
gfcMovieInfo f; check(gfcMovieReader::probe(dir+"/bars_601full.mp4", f, &err) && f.matrix==gfcYuvMatrix::BT601 && f.range==gfcYuvRange::Full, "601 full-range tags");
gfcMovieReader r; check(r.open(dir+"/h264_25.mp4", &err), "open h264");
auto bytesOf = [](const gfcMovieFrame& fr){ /* copy plane[0..2] rows into one vector */ };
gfcMovieFrame a, b; std::vector<uint8_t> seq40, jump40;
bool seqOk=true; for (int k=0;k<=40;++k) seqOk = seqOk && r.readFrame(k, a, &err); check(seqOk, "sequential read 0..40"); seq40=bytesOf(a);
check(r.readFrame(5, b, &err) && r.readFrame(40, b, &err), "jump 5 then 40"); jump40=bytesOf(b);
check(seq40==jump40, "random access is bit-exact");
check(r.readFrame(39, b, &err) && b.index==39, "backward step");
check(r.readFrame(47, b, &err) && b.index==47 && !r.readFrame(48, b, &err), "last frame decodes (drained), 48 is out of range");
check(r.readFrame(0, b, &err) && b.plane[0].width==320 && b.plane[1].width==160 && b.plane[1].height==90, "plane geometry 420");
std::vector<uint8_t> bgra; check(r.convertToBGRA(b, 8, 320, 180, bgra, &err) && bgra.size()==320u*180*4, "bgra 8-bit");
check(r.convertToBGRA(b, 16, 160, 90, bgra, &err) && bgra.size()==160u*90*8, "bgra 16-bit scaled");
check(!gfcMovieReader::probe(dir+"/empty.mov", i, &err) && !err.empty(), "empty file errors");
gfcMovieReader t; check(t.open(dir+"/truncated.mov", &err), "truncated opens"); int got=0; gfcMovieFrame tf; for (int k=0;k<48;++k){ if(!t.readFrame(k,tf,&err)) break; ++got; } check(got>0 && got<48, "truncated decodes some frames then stops cleanly");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile errors (`probe`, `open`, `readFrame` undefined).

- [ ] **Step 3: Implement `jefe::qt::testmedia::generate`** in `src/qt/MovieTestMedia_qt.cpp` with `QProcess` runs of `findFfmpeg()` (return false with `err` when no ffmpeg). Commands (one `QStringList` each; `\,` escapes are filtergraph-level and are passed literally):
  - `prores422hq_23976.mov`: `-y -f lavfi -i testsrc2=size=320x180:rate=24000/1001,format=rgb24 -frames:v 48 -vf scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le -c:v prores_ks -profile:v 3 -timecode 01:00:00:00 OUT`
  - `prores4444_24.mov`: same source at `rate=24`, `-vf scale=out_color_matrix=bt709:out_range=tv,format=yuv444p12le -c:v prores_ks -profile:v 4`
  - `h264_25.mp4`: `rate=25`, `-vf scale=out_color_matrix=bt709:out_range=tv,format=yuv420p -c:v libx264 -g 12 -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv`
  - `shot_v003.mov`, `a_001.mov`, `a_002.mov`: byte copies of `prores422hq_23976.mov`
  - `bars_truth.%04d.png`: `-f lavfi -i smptehdbars=size=320x180:rate=24,format=rgb24 -frames:v 8 -pix_fmt rgb48be`
  - `bars_709tv.mov`: same bars source, `-vf scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le -c:v prores_ks -profile:v 3`
  - `bars_601full.mp4`: bars source, `-vf scale=out_color_matrix=bt601:out_range=pc,format=yuv420p -c:v libx264 -g 4 -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m -color_range pc`
  - `bars_untagged.mp4`: bars source, `-vf scale=out_color_matrix=bt709:out_range=tv,format=yuv420p -c:v libx264 -g 4 -bsf:v h264_metadata=colour_primaries=2:transfer_characteristics=2:matrix_coefficients=2:video_full_range_flag=0` (verified: ffprobe reports `color_space=unknown`; `-colorspace unknown` alone does not strip the tag)
  - `bars_709tv_odd.mp4`: bars at `size=321x181`, 709/tv libx264 yuv420p, 8 frames
  - `flashbeep_23976.mov`: `-f lavfi -i color=c=black:size=320x180:rate=24000/1001,drawbox=w=iw:h=ih:color=white:t=fill:enable='eq(mod(n\,24)\,0)',format=yuv422p10le -f lavfi -i aevalsrc='if(lt(mod(floor(t*24000/1001+0.0001)\,24)\,1)\,0.8*sin(2*PI*1000*t)\,0)':s=48000:c=stereo -t 10 -c:v prores_ks -profile:v 3 -c:a pcm_s16le -timecode 01:00:00:00`
  - `flashbeep_25.mov`: same with `rate=25`, `mod(n\,25)`, `floor(t*25+0.0001)\,25`
  - `df_2997.mov`: `testsrc2=size=320x180:rate=30000/1001,format=rgb24 -frames:v 10 … prores_ks -profile:v 3 -timecode 00:59:59;28`
  - `mono441_24.mp4`: `testsrc2 rate=24 -frames:v 48` + `-f lavfi -i sine=frequency=440:sample_rate=44100 -ac 1 -c:a aac -shortest`, libx264 yuv420p
  - `truncated.mov`: first half of `prores422hq_23976.mov`'s bytes; `empty.mov`: 0 bytes.

- [ ] **Step 4: Implement `gfcMovieReader`** (pimpl `struct gfcMovieReader::Impl` in the `.cpp` holding `AVFormatContext*`, `AVCodecContext*`, video stream index, `SwsContext*` cache, `std::mutex`, `lastIndex`). Decisions: `thread_count = 0`, `thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME`; decoder setup in one function `openDecoder()` (the future `hwaccel` seam: `hw_device_ctx` + `get_format` go there, `av_hwframe_transfer_data` in `decodeNext()`); `frameCount = st->nb_frames` if > 0 else `llround(duration_seconds * num / den)`; `rate` = `st->avg_frame_rate` reduced (`av_reduce`), `variableRate = (st->r_frame_rate != st->avg_frame_rate)`; tags read from the first decoded `AVFrame` (`colorspace`, `color_range`, `chroma_location`), falling back to `codecpar` when the frame says unspecified (verified: ProRes carries tags only in the frame header); `readFrame(i)`: if `i == lastIndex+1` decode next, else `av_seek_frame(ts(i), AVSEEK_FLAG_BACKWARD)` + `avcodec_flush_buffers` + decode forward until `pts >= ts(i)`; at EOF send a null packet and drain; `gfcMovieFrame::owner` keeps the `AVFrame` alive (zero copy, freed by the shared_ptr deleter); `convertToBGRA` uses `sws_getCachedContext` with `sws_setColorspaceDetails` set from matrix/range, output `AV_PIX_FMT_BGRA` or `AV_PIX_FMT_BGRA64LE`.

- [ ] **Step 5: Aggregator generates media** — `--video-load-test` creates `QDir::tempPath()/jefe_video_test_<pid>`, calls `testmedia::generate`, prints `VIDEO-LOAD-TEST SKIP: <err>` and exits 3 when ffmpeg is unavailable, passes the dir to every suite, removes the dir at the end unless `JEFE_KEEP_TEST_MEDIA=1` (then prints `VIDEO-LOAD-TEST media: <dir>`).

- [ ] **Step 6: Run** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-READER: pass=20 fail=0`, exit 0.

- [ ] **Step 7: Commit** — `git add src/qt/MovieTestMedia_qt.h src/qt/MovieTestMedia_qt.cpp src/gfcMovieReader.h src/gfcMovieReader.cpp src/gfcMovieSelfTests.cpp src/main_qt.cpp && git commit -m "feat: libav movie reader with random access, plus ffmpeg-generated test media"`

---

### Task 3: Timecode

**Files:**
- Create: `src/gfcTimecode.h`, `src/gfcTimecode.cpp`
- Modify: `src/gfcMovieReader.cpp` (fill `startTimecode`), `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Produces (`src/gfcTimecode.h`): `struct gfcTimecode { int hh=0, mm=0, ss=0, ff=0; bool drop=false; };` `bool gfcParseTimecode(const std::string& s, gfcTimecode& out);` (accepts `HH:MM:SS:FF` and `HH:MM:SS;FF`, `;` ⇒ drop) `int gfcNominalFps(int num, int den);` (`(num + den - 1) / den`: 24000/1001→24, 30000/1001→30) `int64_t gfcTimecodeToFrames(const gfcTimecode& tc, int num, int den);` `gfcTimecode gfcFramesToTimecode(int64_t frames, int num, int den, bool drop);` `std::string gfcFormatTimecode(const gfcTimecode& tc);` `int timecodeSelfTest(const std::string&);`
- `gfcMovieInfo::startTimecode`: the first `"timecode"` tag found over the format metadata then every stream's metadata (mov `tmcd` track), else `""`.

- [ ] **Step 1: Write the failing test** — `timecodeSelfTest` (MOVIE-TIMECODE):

```cpp
gfcTimecode tc; check(gfcParseTimecode("01:00:00;00", tc) && tc.drop && tc.hh==1, "parse drop");
check(gfcParseTimecode("00:59:59:28", tc) && !tc.drop && tc.ss==59 && tc.ff==28, "parse non-drop");
check(!gfcParseTimecode("1:2", tc), "garbage rejected");
check(gfcNominalFps(24000,1001)==24 && gfcNominalFps(30000,1001)==30 && gfcNominalFps(25,1)==25 && gfcNominalFps(60000,1001)==60, "nominal fps");
gfcTimecode s; gfcParseTimecode("01:00:00:00", s);
check(gfcFormatTimecode(gfcFramesToTimecode(gfcTimecodeToFrames(s,24000,1001)+24, 24000,1001,false))=="01:00:01:00", "ndf 23.976 +24 frames");
check(gfcFormatTimecode(gfcFramesToTimecode(gfcTimecodeToFrames(s,24000,1001)+86400, 24000,1001,false))=="02:00:00:00", "ndf +86400");
gfcTimecode d; gfcParseTimecode("01:00:00;00", d); const int64_t d0=gfcTimecodeToFrames(d,30000,1001);
check(gfcFormatTimecode(gfcFramesToTimecode(d0+1799,30000,1001,true))=="01:00:59;29", "df +1799");
check(gfcFormatTimecode(gfcFramesToTimecode(d0+1800,30000,1001,true))=="01:01:00;02", "df +1800 skips 2");
check(gfcFormatTimecode(gfcFramesToTimecode(d0+17982,30000,1001,true))=="01:10:00;00", "df tenth minute not dropped");
check(gfcTimecodeToFrames(gfcFramesToTimecode(123456,60000,1001,true),60000,1001)==123456, "df 59.94 round trip");
gfcMovieInfo i; std::string err;
check(gfcMovieReader::probe(dir+"/prores422hq_23976.mov", i, &err) && i.startTimecode=="01:00:00:00", "tmcd start timecode read");
check(gfcMovieReader::probe(dir+"/df_2997.mov", i, &err) && i.startTimecode=="00:59:59;28", "drop-frame tmcd read");
check(gfcMovieReader::probe(dir+"/h264_25.mp4", i, &err) && i.startTimecode.empty(), "no tmcd => empty");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile error (gfcTimecode undefined).

- [ ] **Step 3: Implement `gfcTimecode.cpp`** — drop-frame per SMPTE: drop `2 × (nominal/30)` frame numbers at the start of every minute except minutes divisible by 10 (so 2 at 29.97, 4 at 59.94); non-drop counts at the nominal rate. Fill `startTimecode` in `gfcMovieReader::Impl::readInfo`.

- [ ] **Step 4: Run** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-TIMECODE: pass=13 fail=0`.

- [ ] **Step 5: Commit** — `git add src/gfcTimecode.h src/gfcTimecode.cpp src/gfcMovieReader.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: SMPTE timecode with drop-frame; read a movie's start timecode"`

---

### Task 4: Numbering bypass — a movie is one clip

**Files:**
- Modify: `src/gfcSequence.cpp:100-135` (`findSequence`), `src/gfcNoteStore.cpp:509-520` (`normalisePath`), `src/gfcMediaFingerprint.cpp:112-160` (`sequencesIn`), `src/qt/SequenceLoadBridge_qt.cpp:3257-3265` (`listSequenceFrames`), `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Consumes: `gfcIsMovieFile`, `gfcMovieReader::probe`.
- Produces: `findSequence(refFiles, inputFilename, label, startNum, endNum)` for a movie path (after the existing search-path resolution at `gfcSequence.cpp:114-121`) returns `refFiles` = `frameCount` copies of the resolved path, `label` = the resolved path, `startNum = 1`, `endNum = frameCount` (a probe failure returns one entry, `startNum=endNum=1`); `gfcNoteStore::normalisePath` returns movie paths unchanged; `sequencesIn` keys a movie by its own path (one entry); `jefe::qt::listSequenceFrames(moviePath)` returns exactly one path. Decision (plan): N copies in `files` so `frames.size()`, `getNumFrames`, `framePathAt` and the thumbnail stride keep working untouched.

- [ ] **Step 1: Write the failing test** — `movieNumberingSelfTest` (MOVIE-NUMBERING):

```cpp
std::vector<std::string> files; std::string label; int s=0,e=0;
findSequence(files, dir+"/shot_v003.mov", label, s, e);
check(files.size()==48 && s==1 && e==48 && label==dir+"/shot_v003.mov" && files[0]==files[47], "shot_v003.mov is 48 frames of one file, not frame 3");
findSequence(files, dir+"/a_001.mov", label, s, e);
check(files.size()==48 && files[0]==dir+"/a_001.mov", "a_001.mov and a_002.mov are never joined");
check(gfcNoteStore::normalisePath(dir+"/a.001.mov")==dir+"/a.001.mov", "normalisePath leaves a movie alone");
auto seqs = gfcMediaFingerprint::sequencesIn(dir, false);
check(seqs.count(dir+"/a_001.mov")==1 && seqs.at(dir+"/a_001.mov").size()==1 && seqs.count(dir+"/a_002.mov")==1, "sequencesIn lists each movie once");
check(jefe::qt::listSequenceFrames(dir+"/shot_v003.mov").size()==1, "listSequenceFrames returns the single file");
// search-path relink (Review Focus 2): move a copy, point sett.searchPaths at the new dir
std::filesystem::create_directories(dir+"/moved"); std::filesystem::copy_file(dir+"/a_002.mov", dir+"/moved/a_002.mov");
sett.useSearchPaths=true; sett.searchPaths={dir+"/moved"}; findSequence(files, dir+"/gone/a_002.mov", label, s, e); sett.useSearchPaths=false; sett.searchPaths.clear();
check(files.size()==48 && files[0]==dir+"/moved/a_002.mov", "a moved movie is found through search paths");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-NUMBERING: pass=1 fail=5` (today `a_001.mov` happens to key itself in `sequencesIn`; every other check fails).

- [ ] **Step 3: Implement** — the branch at the top of `findSequence` right after `theFilename` is resolved; `normalisePath`: `if (gfcIsMovieFile(anyFramePath)) return anyFramePath;`; `sequencesIn::visit`: key a movie by `path`; `listSequenceFrames`: return `{files.front()}` when `gfcIsMovieFile(anyFramePath)`.

- [ ] **Step 4: Run** — same command — Expected: `MOVIE-NUMBERING: pass=6 fail=0`.

- [ ] **Step 5: Commit** — `git add src/gfcSequence.cpp src/gfcNoteStore.cpp src/gfcMediaFingerprint.cpp src/qt/SequenceLoadBridge_qt.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: movies bypass numbered-sequence detection everywhere"`

---

### Task 5: Movie loader and load paths (CPU colour path)

After this task a movie plays in the app (through swscale BGRA); Task 6 moves the conversion to the GPU.

**Files:**
- Create: `src/gfcimageloadermovie.h`, `src/gfcimageloadermovie.cpp`
- Modify: `src/gfcloadparams.h`, `src/gfcloadparams.cpp`, `src/gfcframe.h:30`, `src/gfcframe.cpp:79-139`, `src/gfcSequence.h`, `src/gfcSequence.cpp:833-862,936-960,1240-1283,1658-1760`, `src/qt/MainWindow_qt.cpp:538-541,3243-3251`, `src/qt/TrackStrip_qt.cpp:305`, `src/qt/PlaylistPanel_qt.cpp:342-343`, `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Consumes: `gfcMovieReader`, `gfcIsMovieFile`.
- Produces: `gfcLoadParams` gains `int movieFrameIndex = -1;` and `std::shared_ptr<gfcMovieReader> movieReader;` (forward-declared); `enum loaders` gains `GFCLOADER_MOVIE`; `gfcFrame::whatLoaderToUse` returns it for movie paths; `class gfcImageLoaderMovie : public gfcImageLoader` (`load`, `peek`, `getPixelPointer`, `releaseMemory`, `getChannelNames` → `{"RGB"}`; `const gfcMovieFrame& movieFrame() const`). `load()` uses `params.movieReader` (opening its own when null), frame index = `params.movieFrameIndex` if ≥ 0 else `max(params.fromFrame - 1, 0)`, honours `params.scale` (100/50/25) and `params.crop`/`params.aoi`, and in this task fills `frameInfo` as BGRA (`GL_UNSIGNED_BYTE` + `GL_RGBA`, or `GL_UNSIGNED_SHORT` + `GL_RGBA16F_ARB` when `params.compressed` is `GFC_16BPC`/`GFC_16HALF`), `originalBitDepth` = movie bit depth, `texCoords`/`quadSizeX/Y` as the OIIO loader does (`gfcimageloaderoiio.cpp:396-412`). `gfcSequence` gains `std::shared_ptr<gfcMovieReader> movieReader_` (opened in `prepareSequenceFiles`/`loadPreview` when the GUI filename is a movie, dropped in `clearSequence`), `bool isMovie() const`, `const gfcMovieInfo* movieInfo() const` (null for sequences); `loadSequence()`'s per-frame `tmpParams` sets `movieFrameIndex = i` and `movieReader = movieReader_`; `loadPreview` sets `movieFrameIndex = 0`. File filters: Quick Load, folder-drop list, Track browse and Playlist Add Files gain `*.mov *.mp4` in the media filter plus a separate `Movies (*.mov *.mp4)` entry.

- [ ] **Step 1: Write the failing test** — `movieLoaderSelfTest` (MOVIE-LOADER):

```cpp
gfcLoadParams p; p.fileName=dir+"/prores422hq_23976.mov"; p.fromFrame=1; p.toFrame=48; p.scale=100; p.compressed=GFC_16HALF; p.movieFrameIndex=7;
gfcFrame f; check(f.loadFrame(p)==0 && f.loaded && f.sizeX==320 && f.sizeY==180 && f.bpc==16 && f.originalBitDepth==10, "16-bit movie frame loads");
check(f.whatLoaderToUse(p.fileName)==GFCLOADER_MOVIE, "dispatch");
gfcGLFrameInfo gi=f.theImageLoader->getFrameInfo(); check(gi.dataType==GL_UNSIGNED_SHORT && gi.internalFormat==GL_RGBA16F_ARB && gi.target==GL_TEXTURE_RECTANGLE_ARB, "16F upload info");
p.compressed=GFC_8BPC; p.scale=50; gfcFrame g; check(g.loadFrame(p)==0 && g.sizeX==160 && g.sizeY==90 && g.bpc==8, "8-bit at 50% scale");
p.fileName=dir+"/empty.mov"; gfcFrame e; check(e.loadFrame(p)!=0 && !e.loaded && !e.loadErrorString.empty(), "empty movie reports an error");
p.fileName=dir+"/truncated.mov"; p.movieFrameIndex=47; gfcFrame t; check(t.loadFrame(p)!=0 && !t.loaded, "frame past the truncation fails without crashing");
p.fileName=dir+"/h264_25.mp4"; p.movieFrameIndex=-1; p.fromFrame=3; gfcFrame h; check(h.loadFrame(p)==0 && h.theImageLoader && static_cast<gfcImageLoaderMovie*>(h.theImageLoader)->movieFrame().index==2, "index defaults to fromFrame-1");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile errors (`movieFrameIndex`, `GFCLOADER_MOVIE`).

- [ ] **Step 3: Implement `gfcImageLoaderMovie`, the `gfcLoadParams` fields, dispatch, and the `gfcSequence` reader lifecycle.** Scale/crop in this task: `convertToBGRA(frame, bits, outW, outH)` with `outW = round(w * scale/100)`, crop by copying the AOI rows afterwards (as `gflCrop` does).

- [ ] **Step 4: Update the four file filters** to `"Media files (*.exr *.dpx *.png *.jpg *.jpeg *.tif *.tiff *.tga *.bmp *.mov *.mp4);;Movies (*.mov *.mp4);;Image files (…);;All files (*)"` and add `"*.mov", "*.MOV", "*.mp4", "*.MP4"` to `kImageFilters` (`MainWindow_qt.cpp:3243`).

- [ ] **Step 5: Run the suite** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-LOADER: pass=7 fail=0`.

- [ ] **Step 6: Manual check in the app** — Run: `JEFE_KEEP_TEST_MEDIA=1 /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test | grep media:` then `/Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --open-file <dir>/h264_25.mp4` (or any client `.mov`) — Expected: the clip loads as 48 frames, the timeline fill grows, Space plays, Left/Right step, drag-drop on the viewport and on a timeline lane load it, Cmd+L shows from/to 1–48.

- [ ] **Step 7: Commit** — `git add src/gfcimageloadermovie.h src/gfcimageloadermovie.cpp src/gfcloadparams.h src/gfcloadparams.cpp src/gfcframe.h src/gfcframe.cpp src/gfcSequence.h src/gfcSequence.cpp src/qt/MainWindow_qt.cpp src/qt/TrackStrip_qt.cpp src/qt/PlaylistPanel_qt.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: load movies as sequences through every load path (CPU colour path)"`

---

### Task 6: GPU YUV→RGB with the file's tags; colour, thumbnail and performance tests

**Files:**
- Create: `src/gfcYuvConverter.h`, `src/gfcYuvConverter.cpp`
- Modify: `src/gfcglframeinfo.h`, `src/gfcframe.cpp:187-282`, `src/gfcimageloadermovie.cpp`, `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp` (new harnesses next to `runHeadlessCCTest`, line 1611), `src/main_qt.cpp` (dispatch next to `--cc-test`, lines 165-172 and 1754-1760)

**Interfaces:**
- Consumes: `gfcMovieFrame` planes; `jefe::qt::loadFileIntoPlate`, `triggerSyncRender`, `previewRenderFilename`, `getTrackTimelineState`, `getTrackThumbnail` (bridge).
- Produces: `gfcGLFrameInfo` gains `bool planarYuv=false; int planeCount=0; struct Plane { const void* data=nullptr; int width=0, height=0, strideBytes=0; } planes[4]; int bitDepth=8; int subX=1, subY=1; int matrix=0; int range=0; int siting=0; bool hasAlpha=false;` (`matrix/range/siting` are the `gfcYuv*` enums cast to int; `dataPointer` stays null for planar frames). `src/gfcYuvConverter.h`: `GLuint gfcYuvConvertToTexture(const gfcGLFrameInfo& info, int srcX, int srcY, int srcW, int srcH, int dstW, int dstH, GLuint internalFormat);` (returns a `GL_TEXTURE_RECTANGLE_ARB` texture of `dstW×dstH`; GL context must be current) `bool gfcYuvConvertToRGBA8(const gfcGLFrameInfo& info, int dstW, int dstH, std::vector<unsigned char>& rgba);` (CPU nearest, same maths) `void gfcYuvConverterRelease();`. `gfcImageLoaderMovie::load` now fills the planar fields for planar YUV sources (`sizeX/sizeY` = scaled/cropped output size) and keeps the BGRA path for anything else. `gfcFrame::generateTexture`: `if (info.planarYuv)` → `textureID = gfcYuvConvertToTexture(…)`, thumbnail via `gfcYuvConvertToRGBA8`. Harness flags: `--video-colour-test <dir>` and `--video-perf-test <movie>` (`MainWindow_Qt::runHeadlessVideoColourTest(const QString& dir)`, `runHeadlessVideoPerfTest(const QString& movie)`).

Decisions (plan): conversion happens **once at upload** into the ordinary resident RGBA texture (8-bit `GL_RGBA` or `GL_RGBA16F_ARB` per the track depth), so the super-shader, FX stack, render read-back and remote paths are untouched. Planes upload as `GL_LUMINANCE`/`GL_LUMINANCE16` rectangle textures (`GL_UNSIGNED_BYTE`/`GL_UNSIGNED_SHORT`, `GL_UNPACK_ROW_LENGTH` from the stride), luma sampled `GL_NEAREST`, chroma `GL_LINEAR` (bilinear upsampling). Scale and crop are the pass's quad/viewport (bilinear), so the Preferences decode filter does not apply to movies (document in §38). The pass draws a quad into an EXT FBO whose colour attachment is the output texture, with one ARB program compiled lazily per (bitDepth, planeCount, hasAlpha) and the tag values as uniforms.

Shader maths (codes `c = texel × codeScale`, `codeScale = 65535` for 16-bit uploads, `255` for 8-bit; `B` = bit depth, `M = 2^B − 1`): video range `y = (cY − 16·2^(B−8)) / (219·2^(B−8))`, `cb = (cCb − 128·2^(B−8)) / (224·2^(B−8))` (cr likewise); full range `y = cY / M`, `cb = (cCb − 2^(B−1)) / M`. Then `R = y + 2(1−Kr)·cr`, `G = y − 2(1−Kb)·Kb/Kg·cb − 2(1−Kr)·Kr/Kg·cr`, `B = y + 2(1−Kb)·cb` with (Kr, Kb) = 709 (0.2126, 0.0722), 601 (0.299, 0.114), 2020 (0.2627, 0.0593), `Kg = 1 − Kr − Kb`; `Unspecified` ⇒ 709 and video. No clamp in the shader (8-bit storage clamps; 16F keeps headroom like EXR). Chroma coordinate for luma texel centre `s` (= x + 0.5) with nominal ratio `r = 1/subX`: horizontally **co-sited** `(s − 0.5)·r + 0.5` unless `siting == Center` (then `s·r`); vertically `t·rv` (centred) unless `siting == TopLeft` (then `(t − 0.5)·rv + 0.5`). For 4:4:4 both reduce to `s`,`t`.

- [ ] **Step 1: Write the failing GL test** — `MainWindow_Qt::runHeadlessVideoColourTest(dir)` (prints `VIDEO-COLOUR-TEST PASS|FAIL …`, returns 0/2). It generates media into `dir` via `testmedia::generate`, then for each clip `bars_709tv.mov` (16-bit render, track depth `GFC_16HALF`), `bars_601full.mp4`, `bars_untagged.mp4`, `bars_709tv_odd.mp4` (8-bit renders, `GFC_8BPC`): `loadFileIntoPlate(0, clip)`, wait (QTimer loop ≤ 10 s) until `getTrackTimelineState(0).loadedCount == 8`, `seekToFrame(3)`, render frame 3 as PNG (`RenderParams` as in `runHeadlessCCTest`, `bitsPerChannel` 16 for the ProRes clip) and compare with `bars_truth.0003.png`:

```cpp
// interior mask: truth pixel equals its 8 neighbours within 1 code (8-bit) — excludes bar edges and the chroma blur around them
// ProRes 10-bit clip, 16-bit compare: every interior pixel |Δ| <= 256 (4 ten-bit codes), mean |Δ| <= 64
// 8-bit clips, 8-bit compare:          every interior pixel |Δ| <= 2,  mean |Δ| <= 0.5
// odd clip: rendered size == 321x181 and the same 8-bit rule
// thumbnail: getTrackThumbnail(0, 3) vs the 8-bit render sampled at the thumbnail's grid: every interior sample |Δ| <= 2
// 16-bit precision: the ProRes render has > 16 distinct values inside the grey ramp band (proves the 16F path is not 8-bit)
```
  The 601 and full-range clips pass only if the tags are honoured (they were encoded with those matrices/ranges); the untagged clip passes only through the 709/video default.

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --config-dir /tmp/jefe_vct_cfg --video-colour-test /tmp/jefe_vct` — Expected: `VIDEO-COLOUR-TEST FAIL` (swscale 8-bit path: the 16-bit precision check fails; the odd clip renders) — or a compile error before the harness exists.

- [ ] **Step 3: Implement `gfcYuvConverter`, the `gfcGLFrameInfo` fields, the planar fill in `gfcImageLoaderMovie::load`, and the `generateTexture` branch** (release the loader after conversion exactly as today; plane textures and the FBO are per-context statics released by `gfcYuvConverterRelease()`).

- [ ] **Step 4: Run** — same command — Expected: `VIDEO-COLOUR-TEST PASS` with the per-clip max/mean printed, exit 0. If `GL_LUMINANCE16` turns out 8-bit on the Metal GL layer (the precision check fails), switch the plane format to `GL_R16` (`GL_ARB_texture_rg`, present in glad) and re-run.

- [ ] **Step 5: Add `--video-perf-test <movie>`** — loads the movie into plate 0 at the track default depth, measures wall time from `startLoadingSequence` until `loadedCount == frameCount` (or until the memory limit stops loading), prints `VIDEO-PERF decode+upload: N frames in S s = F fps`, then plays for 5 s and prints the EMA FPS; PASS when F ≥ 24 and the played FPS is within 2 % of the target. Run: `… --video-perf-test "/path/to/real 4K ProRes 4444.mov"` on real client footage — Expected: `VIDEO-PERF PASS` (record the numbers in the commit message).

- [ ] **Step 6: Commit** — `git add src/gfcYuvConverter.h src/gfcYuvConverter.cpp src/gfcglframeinfo.h src/gfcframe.cpp src/gfcimageloadermovie.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/main_qt.cpp && git commit -m "feat: GPU YUV->RGB from the file's matrix/range at upload; colour and perf tests"`

---

### Task 7: Exact rational rate, load sets the rate, source timecode in the status bar

**Files:**
- Modify: `src/gfcPlaybackManager.h:62,86`, `src/gfcPlaybackManager.cpp:27-36,440-455,666-690`, `src/gfcSequence.cpp:1559-1590` (`startLoading`), `src/gfcSessionManager.cpp:69,168`, `src/qt/SequenceLoadBridge_qt.h` (near line 132), `src/qt/SequenceLoadBridge_qt.cpp:510-516`, `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp:138-160,455-470`, `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Produces (`gfcPlaybackManager`): `void setTargetRate(int num, int den);` (reduces, sets `targetFPS = float(num)/den`, `timePerFrame = double(den)/num`, pushes to the GUI box) `void getTargetRate(int& num, int& den) const;` `static void rationalFromFPS(float fps, int& num, int& den);` (within 0.005 of 23.976→24000/1001, 29.97→30000/1001, 47.952→48000/1001, 59.94→60000/1001 — so a typed `23.98`/`29.97`/`59.94` maps to the NTSC rational; otherwise `round(fps*1000)/1000` reduced). FPS box (`TimelinePanel_qt.cpp:536-540`, `transport.fps.spin`): `setDecimals(3)`, width wide enough for `119.880`; `setTargetRate` pushes `num/den` rounded to 3 decimals (`23.976`, `29.970`), with signals blocked so the push does not round-trip through `setTargetFPS`; a user-typed value goes through `rationalFromFPS`; `setTargetFPS(float)` now also sets the rational through `rationalFromFPS`. Session: `settings` node gains `targetFPSNum`/`targetFPSDen`; load prefers them when present (fallback `targetFPS`). `gfcSequence::startLoading(gfcLoadParams)` with `isMovie()` and `params.loadFromTimeline == 0` calls `playbackManager.setTargetRate(info.rateNum, info.rateDen)` (decision: a fresh load only; alt-click/in-point reloads keep the user's rate). Bridge: `void getTargetRate(int& num, int& den);` `std::string activeTrackSourceTimecode();` (empty unless the active plate's track is a movie: `gfcFormatTimecode(gfcFramesToTimecode(startFrames + frame - 1 - offset, num, den, drop))` where `startFrames` parses `movieInfo()->startTimecode`, `00:00:00:00` when empty; empty when the mapped index is outside `[0, frameCount)`) `bool activeTrackIsVariableRate();`. MainWindow: `timecodeStatusLabel_` (`objectName "status.timecode"`) between the track and loaded labels, text `TC 01:00:02:13` plus ` · VFR (avg 29.97)` when variable, refreshed in the 60 Hz branch, cache-gated.

- [ ] **Step 1: Write the failing test** — `movieRateSelfTest` (MOVIE-RATE) in `gfcMovieSelfTests.cpp` (may include the managers):

```cpp
int n,d; gfcPlaybackManager::rationalFromFPS(23.976f,n,d); check(n==24000&&d==1001,"23.976 -> 24000/1001");
gfcPlaybackManager::rationalFromFPS(29.97f,n,d); check(n==30000&&d==1001,"29.97"); gfcPlaybackManager::rationalFromFPS(23.98f,n,d); check(n==24000&&d==1001,"a typed 23.98 is NTSC 24000/1001"); gfcPlaybackManager::rationalFromFPS(25.0f,n,d); check(n==25&&d==1,"25");
playbackManager.setTargetRate(24000,1001); playbackManager.getTargetRate(n,d); check(n==24000&&d==1001 && std::fabs(playbackManager.getTargetFPS()-23.976f)<0.001f, "rate held exactly");
gfcSequence* seq=trackManager.getSequence(0); seq->myGUI->setFilename(dir+"/prores422hq_23976.mov"); seq->myGUI->setCompression(GFC_8BPC);
playbackManager.setTargetFPS(30.0f); trackManager.startLoadingSequence(0); trackManager.stopLoadingSequence(0);
playbackManager.getTargetRate(n,d); check(n==24000&&d==1001, "loading a movie sets the player's rate to its rational");
playbackManager.setTargetFPS(30.0f); trackManager.startLoadingSequenceAt(0, 10); trackManager.stopLoadingSequence(0); playbackManager.getTargetRate(n,d); check(n==30&&d==1, "an alt-click reload keeps the user's rate");
playbackManager.setTargetRate(24000,1001); playbackManager.setCurrentFrame(25);
check(jefe::qt::activeTrackSourceTimecode()=="01:00:01:00", "source TC for frame 25 = start + 24 frames at the exact rate");
seq->setOffset(10); check(jefe::qt::activeTrackSourceTimecode()=="01:00:00:14", "offset shifts the mapping"); seq->setOffset(0);
check(!jefe::qt::activeTrackIsVariableRate(), "CFR clip");
const std::string sess=dir+"/rate.jcs"; jefe::qt::saveSession(sess); playbackManager.setTargetFPS(24.0f); jefe::qt::loadSession(sess);
playbackManager.getTargetRate(n,d); check(n==24000&&d==1001, "session round-trips the rational");
check(readFile(sess).find("targetFPSNum=\"24000\"")!=std::string::npos, "session stores num/den");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile errors (`setTargetRate`, `activeTrackSourceTimecode`).

- [ ] **Step 3: Implement** the rational members, `startLoading` hook, session keys, bridge accessors and the status-bar label.

- [ ] **Step 4: Run** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-RATE: pass=12 fail=0`.

- [ ] **Step 5: Commit** — `git add src/gfcPlaybackManager.h src/gfcPlaybackManager.cpp src/gfcSequence.cpp src/gfcSessionManager.cpp src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: exact rational playback rate set by movie loads; source timecode in the status bar"`

---

### Task 8: Fingerprint, review package and Stamp Notes for movies

**Files:**
- Modify: `src/gfcMediaFingerprint.cpp:65-110`, `src/gfcNoteStamp.h`, `src/gfcNoteStamp.cpp`, `src/qt/SequenceLoadBridge_qt.cpp:3363-3460` (`stampNotesIntoExr`), `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Consumes: `gfcMovieReader::probe`/`readFrame`/`convertToBGRA`, `jefe::qt::listSequenceFrames` (Task 4), the package exporter/opener (`jefe::qt::package::exportPackage`/`openPackage` as used by `--package-test`, `MainWindow_qt.cpp:2580-2640`).
- Produces: `gfcMediaFingerprint::probe(moviePath)` → width/height from the movie, `channels = {"R","G","B"}` (+`"A"` when `hasAlpha`); `compute({moviePath})` → `"fp1:" + sha1("jefecheck-fp1", w, h, nch, names, n=1, fileSize, bytes[0,64 KiB), bytes[mid−32 KiB, mid+32 KiB), bytes[size−64 KiB, size))` — container bytes, so it is stable across platforms and FFmpeg versions (decision). `gfcNoteStamp::stampPixels(int width, int height, const float* rgb, const std::string& outExr, const gfcReview&, const Options&, std::string* err)` (writes a half-float RGB EXR plus the header/layer `stamp` writes); `stamp(inExr, …)` becomes "read `inExr` then `stampPixels`". `stampNotesIntoExr`: when the active track `isMovie()`, decode the displayed frame (`convertToBGRA(…, 16, w, h)` → float RGB) and call `stampPixels` instead of reading `sourcePath`.

- [ ] **Step 1: Write the failing test** — `movieFingerprintSelfTest` (MOVIE-FINGERPRINT) and `movieStampSelfTest` (MOVIE-STAMP):

```cpp
std::string err; gfcMediaFingerprint::Probe p;
check(gfcMediaFingerprint::probe(dir+"/h264_25.mp4", p, &err) && p.width==320 && p.height==180 && p.channels==std::vector<std::string>{"R","G","B"}, "movie probe");
const std::string fa=gfcMediaFingerprint::compute({dir+"/a_001.mov"}, &err), fb=gfcMediaFingerprint::compute({dir+"/a_002.mov"}, &err), fh=gfcMediaFingerprint::compute({dir+"/h264_25.mp4"}, &err);
check(fa.rfind("fp1:",0)==0 && fa.size()==44 && fa==fb && fa!=fh, "same bytes same fp1; different movie differs");
check(gfcMediaFingerprint::compute({dir+"/empty.mov"}, &err).empty() && !err.empty(), "empty movie errors");
// package round trip: a session with the movie on track A, export with media, open, assert one copy and a rewritten path
// (reuse the --package-test flow: jefe::qt::package::exportPackage / openPackage)
check(manifest.media.size()==1 && manifest.media[0].frames.size()==1 && manifest.media[0].frames[0]=="shot_v003.mov", "package lists the movie once");
check(std::filesystem::exists(cache+"/media/000/shot_v003.mov") && openedSessionXml.find("media/000/shot_v003.mov")!=std::string::npos, "the movie file is copied and relinked");
// MOVIE-STAMP
std::vector<float> rgb(320*180*3, 0.25f); gfcReview review; gfcNoteStamp::Options opt;
check(gfcNoteStamp::stampPixels(320,180,rgb.data(), dir+"/stamp.exr", review, opt, &err), "stampPixels writes");
int w=0,h=0; check(gfcNoteStamp::imageSize(dir+"/stamp.exr",&w,&h) && w==320 && h==180, "stamped EXR has the frame size");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MOVIE-FINGERPRINT` fails (probe cannot open a movie through OIIO) and `MOVIE-STAMP` does not compile until `stampPixels` exists.

- [ ] **Step 3: Implement** the movie branches in `probe`/`compute`, `stampPixels`, and the `stampNotesIntoExr` movie path.

- [ ] **Step 4: Run** — same command — Expected: `MOVIE-FINGERPRINT: pass=5 fail=0`, `MOVIE-STAMP: pass=2 fail=0`; also `… --notes-test` still all pass.

- [ ] **Step 5: Commit** — `git add src/gfcMediaFingerprint.cpp src/gfcNoteStamp.h src/gfcNoteStamp.cpp src/qt/SequenceLoadBridge_qt.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: movie-aware fingerprint, review packages and Stamp Notes"`

---

### Task 9: Audio decode into per-track clips, with peaks

**Files:**
- Create: `src/gfcAudioClip.h`, `src/gfcAudioClip.cpp`
- Modify: `src/gfcMovieReader.h`, `src/gfcMovieReader.cpp`, `src/gfcSequence.h`, `src/gfcSequence.cpp:845-1030,1240-1283`, `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Produces (`src/gfcAudioClip.h`):

```cpp
class gfcAudioClip {           // 48 kHz, 2 ch, float32 interleaved; thread-safe (loader appends, mixer reads)
public:
    static constexpr int kRate = 48000; static constexpr int kChannels = 2;
    void reset(int64_t startSample);                       // sample index in the movie's own 48 kHz timeline
    void append(const float* interleaved, int64_t frames);
    int64_t startSample() const; int64_t endSample() const; // [start, end)
    int64_t read(int64_t fromSample, int64_t frames, float* out) const;   // zero-fills outside [start,end); returns frames copied
    static int64_t frameStartSample(int64_t frameIndex, int rateNum, int rateDen); // floor(frameIndex * 48000 * rateDen / rateNum), int64 exact
};
struct gfcAudioPeaks {         // abs-peak of the stereo mix per 10 ms bin, built as audio arrives
    int binsPerSecond = 100; int64_t firstSample = 0; std::vector<float> peaks; uint64_t version = 0;
    void reset(int64_t firstSample); void accumulate(const float* interleaved, int64_t frames);
};
```
  `gfcMovieReader`: `void setAudioEnabled(bool on);` (default off; when on, `open()` also opens the audio decoder and a `SwrContext` to 48 kHz stereo float, and `readFrame` decodes every audio packet it demuxes into an internal FIFO; a seek clears the FIFO and resets `firstSample` from the audio PTS) `bool takeAudio(std::vector<float>& interleaved, int64_t& firstSample);` (drains the FIFO; `firstSample` is the first sample's movie-time index; false when empty). `gfcSequence`: `std::shared_ptr<gfcAudioClip> audioClip_`, `gfcAudioPeaks audioPeaks_` (+ mutex), `bool hasAudio() const`, `std::shared_ptr<gfcAudioClip> audioClip() const`, `gfcAudioPeaks audioPeaksSnapshot() const`, `uint64_t audioPeaksVersion() const`; `initializeSequence` resets clip and peaks (`reset(frameStartSample(startFrame…))`), the loader loop after each `loadFrame` calls `takeAudio` and appends to both (append order == decode order, so a pass from frame F gives one contiguous buffer starting at F's sample).

- [ ] **Step 1: Write the failing test** — `audioClipSelfTest` (AUDIO-CLIP):

```cpp
check(gfcAudioClip::frameStartSample(1,24000,1001)==2002 && gfcAudioClip::frameStartSample(1000,24000,1001)==2002000, "23.976: 2002 samples per frame, exact");
check(gfcAudioClip::frameStartSample(1,30000,1001)==1601 && gfcAudioClip::frameStartSample(2,30000,1001)==3203 && gfcAudioClip::frameStartSample(3,30000,1001)==4804, "29.97 floor math 1601/1602");
check(gfcAudioClip::frameStartSample(1,25,1)==1920 && gfcAudioClip::frameStartSample(1,24,1)==2000, "25 and 24");
gfcMovieReader r; std::string err; r.setAudioEnabled(true); check(r.open(dir+"/flashbeep_23976.mov", &err) && r.info().hasAudio && r.info().audioSampleRate==48000 && r.info().audioChannels==2, "audio stream opens");
gfcAudioClip clip; clip.reset(0); gfcMovieFrame f; std::vector<float> a; int64_t first=-1;
for (int k=0;k<48;++k){ r.readFrame(k,f,&err); while (r.takeAudio(a, first)) { if (clip.endSample()==clip.startSample()) clip.reset(first); clip.append(a.data(), (int64_t)a.size()/2); } }
check(clip.startSample()==0 && clip.endSample()>=gfcAudioClip::frameStartSample(48,24000,1001), "audio covers the decoded frames");
auto rms=[&](int64_t frame){ std::vector<float> s(2002*2); clip.read(gfcAudioClip::frameStartSample(frame,24000,1001), 2002, s.data()); double e=0; for(float v:s) e+=v*v; return std::sqrt(e/s.size()); };
check(rms(0)>0.3 && rms(24)>0.3 && rms(1)<0.01 && rms(23)<0.01 && rms(25)<0.01, "beep lands in frames 0 and 24 only");
gfcMovieReader m; m.setAudioEnabled(true); check(m.open(dir+"/mono441_24.mp4",&err) && m.info().audioChannels==1 && m.info().audioSampleRate==44100, "mono 44.1k source");
gfcAudioClip mc; mc.reset(0); for (int k=0;k<48;++k){ m.readFrame(k,f,&err); while (m.takeAudio(a,first)) mc.append(a.data(), (int64_t)a.size()/2); }
check(mc.endSample()>=48*2000-2000 && mc.endSample()<=48*2000+4096, "resampled to 48k stereo: ~2000 samples per frame");
std::vector<float> z(100*2); check(clip.read(-50, 100, z.data())==50 && z[0]==0.f, "reads before the start are zero-filled");
gfcAudioPeaks pk; pk.reset(0); std::vector<float> sig(4800*2, 0.f); sig[2*480]=0.9f; pk.accumulate(sig.data(), 4800);
check(pk.peaks.size()==10 && pk.peaks[1]>0.89f && pk.peaks[0]==0.f && pk.version==1, "10 ms bins, abs peak");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile errors (`gfcAudioClip`, `setAudioEnabled`).

- [ ] **Step 3: Implement** the clip, peaks, reader audio path (`swr_alloc_set_opts2` to `AV_CHANNEL_LAYOUT_STEREO`, `AV_SAMPLE_FMT_FLT`, 48000) and the sequence integration (`movieReader_->setAudioEnabled(info.hasAudio)` at open).

- [ ] **Step 4: Run** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `AUDIO-CLIP: pass=10 fail=0`.

- [ ] **Step 5: Commit** — `git add src/gfcAudioClip.h src/gfcAudioClip.cpp src/gfcMovieReader.h src/gfcMovieReader.cpp src/gfcSequence.h src/gfcSequence.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: decode movie audio into per-track 48 kHz clips with peak summaries"`

---

### Task 10: Audio engine core (pure): slices, fades, leading clock, off-rate

**Files:**
- Create: `src/gfcAudioEngine.h`, `src/gfcAudioEngine.cpp`
- Modify: `src/gfcPlaybackManager.h`, `src/gfcPlaybackManager.cpp:150-200` (extract the advance rule), `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Consumes: `gfcAudioClip`, `gfcAudioClip::frameStartSample`.
- Produces (`src/gfcAudioEngine.h`):

```cpp
class gfcAudioEngine;
class gfcAudioOutput { public: virtual ~gfcAudioOutput(){}; virtual bool open(gfcAudioEngine* source)=0; virtual void close()=0;
                       virtual bool isOpen() const=0; virtual int64_t playedSamples() const=0; };
struct gfcAudioTransport { int startFrame=1; int startLimit=1; int endLimit=1; int loopMode=0; int direction=1; /* LOOPMODEONCE_ID/LOOPMODELOOP_ID/LOOPMODEBOUNCE_ID; direction from advanceFrame */ };
class gfcAudioEngine {
public:
    static constexpr int kRate=48000, kChannels=2, kFadeSamples=144;        // 3 ms fades (decision)
    void setOutput(gfcAudioOutput* out);                                      // may be null (no device): everything below still works, silently
    void setTrackClip(int track, std::shared_ptr<gfcAudioClip> clip, int rateNum, int rateDen, int64_t frameCount);
    void clearTrack(int track); void setTrackOffset(int track, int offset);
    void setTrackMuted(int track, bool muted); bool trackMuted(int track) const; bool trackHasAudio(int track) const;
    void setPlayerRate(int num, int den);
    bool canLead() const;        // forward ONCE/LOOP, or BOUNCE on a forward pass, and some unmuted track with audio whose rate == player rate (reduced equality)
    void startLeading(const gfcAudioTransport& t); void updateTransport(const gfcAudioTransport& t); void stopLeading();
    bool isLeading() const; int leaderFrame() const;                        // frame whose slice is at the output's playedSamples()
    void playSlice(int frame);   // follow mode / step / scrub: start this frame's slices now (cuts a playing slice with a fade-out); a slice requested while a newer one is pending replaces it
    void stopSlices();
    void render(float* interleaved, int frames);                             // called by the output; mixes (sum, clamp [-1,1]); never allocates
};
class gfcFakeAudioOutput : public gfcAudioOutput { public: void pump(int frames, std::vector<float>* captured=nullptr); /* calls render, advances playedSamples */ };
```
  `gfcPlaybackManager` gains `static int advanceFrame(int cur, int& direction, int mode, int startLimit, int endLimit);` (the exact switch at `gfcPlaybackManager.cpp:154-199`, now also used by `update()`), and the engine uses it to walk frames while leading. Slice rules (decisions): a track's slice for timeline frame `F` is movie frame `i = F − offset − 1` when `0 ≤ i < frameCount` (otherwise silence — hold modes never repeat audio), samples `[frameStartSample(i), frameStartSample(i+1))` at the track's rational; on-screen duration of a frame = `frameStartSample(1, playerNum, playerDen)` samples; a slice longer than the on-screen duration is cut there with a fade-out, a shorter one is followed by silence; fade-in on any slice not contiguous (same track, next sample) with the previously rendered one, fade-out on any slice whose successor is not contiguous; leading = the engine advances its own frame after each on-screen duration, logs `(startSample, frame)` into a ring so `leaderFrame()` maps `playedSamples()` back; reverse never leads and renders nothing. BOUNCE (ping-pong): the engine leads on each forward pass; when `advanceFrame` turns the direction to -1 it renders nothing (fade-out on the last forward slice) and keeps walking frames silently, and resumes sound with a fade-in when the next forward pass begins — decision, matching "reverse playback is silent".

- [ ] **Step 1: Write the failing tests** — `audioEngineSelfTest` (AUDIO-ENGINE) and `audioSyncSelfTest` (AUDIO-SYNC):

```cpp
// fixtures: clipA = flashbeep_23976 fully decoded (Task 9 loop), rate 24000/1001, 240 frames; engine e; gfcFakeAudioOutput out; e.setOutput(&out); out.open(&e);
e.setTrackClip(0, clipA, 24000, 1001, 240); e.setPlayerRate(24000,1001);
// step slice: exactly one frame, faded
std::vector<float> cap; e.playSlice(25); out.pump(4000, &cap);                      // frame 25 = movie index 24 (beep)
auto nonZero=[&](const std::vector<float>& v){ int64_t n=0; for(size_t i=0;i<v.size();i+=2) if (std::fabs(v[i])>1e-6f) n=i/2+1; return n; };
check(nonZero(cap)==2002, "one frame's samples (2002) then silence");
bool ramp=true; for (int i=0;i<144;++i) ramp &= std::fabs(cap[2*i]) <= 0.8f*(i+1)/144.0f + 1e-3f; check(ramp, "3 ms fade-in envelope");
check(std::fabs(cap[2*2001])<0.02f, "fade-out reaches ~0 at the slice end");
// fast scrub drops the late one: two requests before any pump -> only the last plays
cap.clear(); e.playSlice(49); e.playSlice(73); out.pump(4000,&cap); check(nonZero(cap)==2002, "only the newest pending slice plays");
// muted -> zeros
e.setTrackMuted(0,true); cap.clear(); e.playSlice(25); out.pump(4000,&cap); check(nonZero(cap)==0, "muted track renders silence"); e.setTrackMuted(0,false);
// off-rate: 2x -> cut to 1001 samples; 0.5x -> 2002 then 2002 of silence; pitch unchanged (1 kHz = 48 samples/period)
e.setPlayerRate(48000,1001); cap.clear(); e.playSlice(25); out.pump(1001,&cap); e.playSlice(26); out.pump(3000,&cap);
check(nonZero(cap)<=1001+144 && countZeroCrossings(cap,0,900)>=35 && countZeroCrossings(cap,0,900)<=40, "2x: cut at 1001 samples, unchanged pitch");
e.setPlayerRate(12000,1001); cap.clear(); e.playSlice(25); out.pump(4004,&cap); check(nonZero(cap)==2002, "0.5x: full slice then silence until the next frame");
// leading: contiguous, no fades between frames, leaderFrame follows playedSamples, loop wraps
e.setPlayerRate(24000,1001); check(e.canLead(), "native-rate unmuted track can lead");
gfcAudioTransport t{1,1,48,LOOPMODELOOP_ID}; e.startLeading(t); cap.clear(); out.pump(2002*10+1000,&cap);
check(e.isLeading() && e.leaderFrame()==11, "after 10.5 frames of samples the leader is on frame 11");
check(std::fabs(cap[2*2002]-cap[2*2001]) < 0.05f, "no fade between contiguous frames");
out.pump(2002*40, nullptr); check(e.leaderFrame()>=1 && e.leaderFrame()<=48, "loop wraps inside [1,48]");
e.stopLeading(); check(!e.isLeading(), "stopped");
// reverse never leads; off-rate track never leads
e.setPlayerRate(25,1); check(!e.canLead(), "off-rate track cannot lead");
// mixing: two tracks, sum and clamp
e.setTrackClip(1, clipA, 24000, 1001, 240); e.setPlayerRate(24000,1001); cap.clear(); e.playSlice(25); out.pump(2002,&cap);
float peak=0; for (float v: cap) peak=std::max(peak,std::fabs(v)); check(peak<=1.0f && peak>0.95f, "two 0.8 beeps sum then clamp to 1.0");
// null output: every call is safe
gfcAudioEngine n; n.setOutput(nullptr); n.setTrackClip(0, clipA, 24000,1001,240); n.setPlayerRate(24000,1001); n.playSlice(25); n.startLeading(t);
check(!n.canLead() && !n.isLeading(), "no device: cannot lead, every call is a safe no-op");
// AUDIO-SYNC: play the whole 10 s clip at 24000/1001 and at 25 (flashbeep_25) while leading; for every rendered frame compare beep energy vs the frame's mean luma (reader CPU path)
// expectation: mismatches == 0 at both rates, and the frame count rendered == 240 (23.976) / 250 (25) with no frame rendered twice
```
  `audioSyncSelfTest` walks the engine's render log: for each `(startSample, frame)` entry, the slice RMS > 0.3 iff the decoded movie frame's mean luma > 0.5; asserts `mismatches == 0` for both clips.

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile errors (`gfcAudioEngine`).

- [ ] **Step 3: Implement `gfcAudioEngine`** (one mutex around state; `render` holds it briefly and never allocates — per-track scratch buffers preallocated to one on-screen duration + fade), `gfcFakeAudioOutput`, and extract `advanceFrame`.

- [ ] **Step 4: Run** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `AUDIO-ENGINE: pass=15 fail=0`, `AUDIO-SYNC: pass=4 fail=0`.

- [ ] **Step 5: Commit** — `git add src/gfcAudioEngine.h src/gfcAudioEngine.cpp src/gfcPlaybackManager.h src/gfcPlaybackManager.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: audio engine with frame slices, fades, off-rate cut/pad and a leading clock"`

---

### Task 11: Qt audio output and playback integration

**Files:**
- Create: `src/qt/AudioOutput_qt.h`, `src/qt/AudioOutput_qt.cpp`
- Modify: `src/gfcPlaybackManager.cpp:55-100,300-420` (play/pause/step/seek hooks), `src/gfcSequence.cpp` (`startLoading`, `setOffset`, `clearSequence` → engine), `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp` (`initializeRenderingChain`, `beginBridgeShutdown`), `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Consumes: `gfcAudioEngine`, `gfcAudioOutput`.
- Produces: global `gfcAudioEngine audioEngine;` (defined in `gfcAudioEngine.cpp`, `extern` where used). `namespace jefe::qt::audio { gfcAudioOutput* createOutput(); void destroyOutput(gfcAudioOutput*); }` in `AudioOutput_qt.h` (no Qt Multimedia types in the header). `AudioOutput_qt.cpp`: a `QIODevice` subclass whose `readData` calls `audioEngine.render`; `QAudioSink` in pull mode with format 48000 Hz / 2 ch / `Float` (falls back to `Int16` with conversion, then to the device's `preferredFormat()` rate with a linear resampler — decision, one line in §38), `setBufferSize` = 40 ms (`48000*0.04*2*4` bytes for float), `playedSamples()` from `processedUSecs()`, reopened on `QMediaDevices::audioOutputsChanged`. `initializeRenderingChain` sets the output; `beginBridgeShutdown` closes it. `gfcPlaybackManager`: `startPlayFwd()` → `audioEngine.setPlayerRate(num,den); if (audioEngine.canLead()) audioEngine.startLeading({currentFrame, getStartLimit(), getEndLimit(), playbackMode})`; `update()` while `audioEngine.isLeading()`: `currentFrame = audioEngine.leaderFrame()` (then the usual `updateTimeCode`/`setChanged`), else on every frame advance in forward direction `audioEngine.playSlice(currentFrame)`; `pause()`/`startPlayRev()` → `stopLeading(); stopSlices()`; `setCurrentFrame`, `oneFrameFwd`, `oneFrameRev` → `playSlice(currentFrame)` when not playing; `setInPoint`/`setOutPoint`/`setPlaybackMode` → `updateTransport`. `gfcSequence::startLoading` → `audioEngine.setTrackClip(trackIdx, audioClip_, num, den, frameCount)` (or `clearTrack` for non-movies), `setOffset` → `audioEngine.setTrackOffset`, `clearSequence` → `clearTrack`. Decision (plan): Qt Multimedia stays; if the measured step-slice latency on macOS exceeds 60 ms after the 40 ms buffer, replace only `AudioOutput_qt.cpp` with a miniaudio-backed `gfcAudioOutput` (same header) and record why in §38.

- [ ] **Step 1: Write the failing test** — `audioPlaybackSelfTest` (AUDIO-PLAYBACK) in `gfcMovieSelfTests.cpp`, driving the real `playbackManager` with a `gfcFakeAudioOutput out` installed via `audioEngine.setOutput(&out); out.open(&audioEngine);` (track index for `setTrackClip` is `trackID - 'A'`):

```cpp
gfcSequence* seq=trackManager.getSequence(0); seq->myGUI->setFilename(dir+"/flashbeep_23976.mov"); seq->myGUI->setCompression(GFC_8BPC);
trackManager.startLoadingSequence(0); waitUntil([&]{ return seq->audioClip() && seq->audioClip()->endSample()>=2002*48; }, 10000); trackManager.stopLoadingSequence(0);
int n,d; playbackManager.getTargetRate(n,d); check(n==24000&&d==1001, "movie rate");
playbackManager.setCurrentFrame(1); playbackManager.startPlayFwd(); check(audioEngine.isLeading(), "forward play leads with audio");
out.pump(2002*10); playbackManager.update(); check(playbackManager.getCurrentFrame()==11, "picture follows the audio clock");
playbackManager.pause(); check(!audioEngine.isLeading(), "pause stops leading");
std::vector<float> cap; playbackManager.setCurrentFrame(25); out.pump(3000,&cap); check(nonZero(cap)==2002, "seek plays one slice");
playbackManager.setTargetFPS(25.0f); playbackManager.startPlayFwd(); check(!audioEngine.isLeading(), "off-rate: FPS pacing leads");
std::this_thread::sleep_for(std::chrono::milliseconds(50)); playbackManager.update(); check(playbackManager.getCurrentFrame()>=26, "timer pacing still advances");
playbackManager.pause(); playbackManager.startPlayRev(); check(!audioEngine.isLeading(), "reverse never leads"); playbackManager.pause();
// BOUNCE: forward passes have sound, backward passes are silent (fake output, range 1..4 at 24000/1001)
{ gfcAudioEngine e; gfcFakeAudioOutput o; e.setOutput(&o); /* clip = tone on every frame */ gfcAudioTransport t{1,1,4,LOOPMODEBOUNCE_ID,1}; e.startLeading(t); std::vector<float> cap; o.pump(2002*4,&cap); check(rms(cap)>0.1f, "bounce forward pass is audible"); cap.clear(); o.pump(2002*3,&cap); check(rms(cap)<1e-4f, "bounce backward pass is silent"); cap.clear(); o.pump(2002*2,&cap); check(rms(cap)>0.1f, "bounce sound resumes on the next forward pass"); }
audioEngine.setOutput(nullptr); playbackManager.setTargetRate(24000,1001); playbackManager.startPlayFwd(); check(!audioEngine.isLeading(), "no device: pacing leads");
std::this_thread::sleep_for(std::chrono::milliseconds(50)); playbackManager.update(); check(playbackManager.getCurrentFrame()>=2, "no device: playback still advances"); playbackManager.pause();
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `AUDIO-PLAYBACK` fails on "forward play leads with audio" (or does not compile until the hooks exist).

- [ ] **Step 3: Implement** `AudioOutput_qt`, the playback-manager hooks, the sequence→engine wiring and bridge init/shutdown.

- [ ] **Step 4: Run** — same command — Expected: `AUDIO-PLAYBACK: pass=13 fail=0`.

- [ ] **Step 5: Manual check** — Run: `/Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --open-file <dir>/flashbeep_23976.mov` — Expected: Space plays with the beep on the white frames; Left/Right click one frame of sound without clicks; scrubbing the timeline chatters without lagging; changing the FPS box to 48 gives half-length clicks; `--open-file <dir>/flashbeep_25.mov` as a second track under the 23.976 player plays slices while track A stays continuous.

- [ ] **Step 6: Commit** — `git add src/qt/AudioOutput_qt.h src/qt/AudioOutput_qt.cpp src/gfcPlaybackManager.cpp src/gfcSequence.cpp src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: Qt Multimedia output; audio clock leads native-rate playback, slices on step/scrub/off-rate"`

---

### Task 12: Timeline mute toggle and waveform

**Files:**
- Modify: `src/gfcSequence.h`, `src/gfcSequence.cpp:2207-2258` (session `muted`), `src/qt/SequenceLoadBridge_qt.h:1000-1050`, `src/qt/SequenceLoadBridge_qt.cpp:1982-2040`, `src/qt/TimelinePanel_qt.h:64-107`, `src/qt/TimelinePanel_qt.cpp:149-470`, `src/qt/qticons.h`, `src/qt/qticons.cpp`, `src/gfcMovieSelfTests.cpp`

**Interfaces:**
- Consumes: `audioEngine.setTrackMuted/trackMuted`, `gfcSequence::hasAudio/audioPeaksSnapshot/audioPeaksVersion`, `movieInfo()`.
- Produces: `gfcSequence::setAudioMuted(bool)` / `bool audioMuted() const` (mirrors to `audioEngine.setTrackMuted(trackIdx, …)`), saved as `muted` in `saveTrackSessionParameters`/read in `loadTrackSessionParameters` (default 0). Bridge: `struct TrackAudioState { bool hasAudio=false; bool muted=false; };` `TrackAudioState getTrackAudioState(int trackIdx);` `void setTrackMuted(int trackIdx, bool muted);` `struct WaveformPeaks { bool present=false; uint64_t version=0; int binsPerSecond=100; int64_t firstSample=0; int rateNum=24, rateDen=1; int offset=0; std::vector<float> peaks; };` `WaveformPeaks getTrackWaveform(int trackIdx);` `TrackTimelineState` gains `bool hasAudio=false; bool muted=false; uint64_t audioPeaksVersion=0;` (included in `refresh()`'s cache compare). `jefe::qticons::speaker()` and `speakerMuted()` (dual-state like the others, `qticons.h:41-61`). Painting (decision): a lane with audio reserves an 8 px strip above the 4 px loaded bar; filmstrip height shrinks by 8 only on such lanes; the strip draws, per pixel column, the max peak of the bins covering that column's frame span (frame → sample via `frameStartSample(frame − 1 − offset, rateNum, rateDen)`), colour `QColor(150,185,150)` at full alpha, 35 % alpha when muted; a 14 px speaker glyph is drawn right of the track letter only when `hasAudio`, and a left click inside its rect toggles the mute (no drag starts); the right-click menu gains "Mute audio" (checkable) for tracks with audio.

- [ ] **Step 1: Write the failing test** — `muteBridgeSelfTest` (MUTE-BRIDGE):

```cpp
gfcSequence* seq=trackManager.getSequence(1); seq->myGUI->setFilename(dir+"/flashbeep_25.mov"); seq->myGUI->setCompression(GFC_8BPC);
trackManager.startLoadingSequence(1); waitUntil([&]{ return seq->audioPeaksVersion()>0; }, 10000); trackManager.stopLoadingSequence(1);
check(jefe::qt::getTrackAudioState(1).hasAudio && !jefe::qt::getTrackAudioState(1).muted, "track with audio, unmuted");
check(!jefe::qt::getTrackAudioState(2).hasAudio, "empty track has no audio");
jefe::qt::setTrackMuted(1,true); check(jefe::qt::getTrackAudioState(1).muted && audioEngine.trackMuted(1) && jefe::qt::getTrackTimelineState(1).muted, "mute reaches the engine and the timeline state");
auto wf=jefe::qt::getTrackWaveform(1); check(wf.present && wf.binsPerSecond==100 && !wf.peaks.empty() && wf.rateNum==25 && wf.rateDen==1, "waveform peaks available");
check(wf.peaks[0]>0.5f && wf.peaks[50]<0.05f, "beep in the first bins, silence at 0.5 s");
const std::string sess=dir+"/mute.jcs"; jefe::qt::saveSession(sess); jefe::qt::setTrackMuted(1,false); jefe::qt::loadSession(sess);
check(jefe::qt::getTrackAudioState(1).muted, "mute persists in a saved session");
check(readFile(sess).find("muted=\"1\"")!=std::string::npos, "session stores muted");
```

- [ ] **Step 2: Run to verify it fails** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8` — Expected: compile errors (`getTrackAudioState`).

- [ ] **Step 3: Implement** the sequence/bridge/session pieces, the icons, and the lane painting + hit test.

- [ ] **Step 4: Run** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && /Volumes/Archive/jefecheck-builds/video/jefecheck.app/Contents/MacOS/jefecheck --video-load-test` — Expected: `MUTE-BRIDGE: pass=7 fail=0`, and the whole run exits 0.

- [ ] **Step 5: Manual check** — Run the app with `flashbeep_25.mov` on A and `h264_25.mp4` (no audio) on B — Expected: the speaker shows on lane A only; clicking it dims the waveform and silences A; the waveform follows the loaded fill and the offset drag; lanes keep their height; the right-click menu toggles the same state.

- [ ] **Step 6: Commit** — `git add src/gfcSequence.h src/gfcSequence.cpp src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/qt/TimelinePanel_qt.h src/qt/TimelinePanel_qt.cpp src/qt/qticons.h src/qt/qticons.cpp src/gfcMovieSelfTests.cpp && git commit -m "feat: per-track mute toggle and waveform strip on the timeline lanes"`

---

### Task 13: Licensing and documentation

**Files:**
- Modify: `README.md:86-88`, `CLA.md:15`, `CLAUDE.md:7,101-125,179`, `THIRD_PARTY_LICENSES:12-17`, `packaging/ffmpeg-NOTICE.txt`, `src/qt/MainWindow_qt.cpp:870-875` (About text), `developer_notes.md` (new §38, amend §18–20), project-own sources (SPDX line)
- Create: `scripts/add_spdx_headers.sh`

**Interfaces:**
- Produces: the wording "GNU General Public License, version 2 or (at your option) any later version" in README's License section, CLA.md, CLAUDE.md's overview line, and the About dialog ("GPL v2 or later"); `THIRD_PARTY_LICENSES` entries: RakNet → "GNU General Public License v2.0 or later (see `src/RakPeer.cpp:12-15`)", new "FFmpeg (libavformat, libavcodec, libavutil, libswresample, libswscale) — linked; GPL v2 or later, packaged builds are GPLv3 (`--enable-version3`), so distributed JefeCheck binaries are effectively under GPLv3; the export CLI binary is covered by `packaging/ffmpeg-NOTICE.txt`"; a first-line `// SPDX-License-Identifier: GPL-2.0-or-later` on every file matched by `git ls-files 'src/gfc*' 'src/qt/*' 'src/auth/*' 'src/ui/*' 'src/main_qt.cpp' 'src/macos_bundle_path.mm' 'src/trilerp.*'` that lacks one (decision: these globs are the project-own set; vendored RakNet/xmlParser/Rijndael/DS_* keep their own terms); `LICENSE` keeps the verbatim GPLv2 text with a two-line preface stating the "or later" grant.

- [ ] **Step 1: Write the check** — `scripts/add_spdx_headers.sh --check` exits 1 listing project-own files without the SPDX line; Run: `bash scripts/add_spdx_headers.sh --check` — Expected: exit 1 with the list.

- [ ] **Step 2: Apply** — Run: `bash scripts/add_spdx_headers.sh && bash scripts/add_spdx_headers.sh --check` — Expected: exit 0.

- [ ] **Step 3: Update the wording** in README, CLA, CLAUDE.md (overview, dependency table: FFmpeg row → "Movie reading (libav*, linked) + export CLI; GPL v2 or later / bundled GPLv3", RakNet row → "GPL v2 or later", remove "**not linked**" from §18–20 pointers and the Image I/O list gains `gfcMovieReader`/`gfcImageLoaderMovie`), THIRD_PARTY_LICENSES, ffmpeg-NOTICE.txt (add the linked libraries), the About text, and `developer_notes.md` **§38 Movie loading and audio** covering: reader/pimpl and the hwaccel seam, N-copies numbering bypass, GPU conversion at upload (+ why the decode filter does not apply to movies), tag precedence frame → codecpar → 709/tv, rational rate and where it is set, audio engine slice model and leading rules, Qt Multimedia buffer/format fallbacks and the miniaudio escape hatch, the self-test flags, and the licensing change.

- [ ] **Step 4: Verify the build still passes and grep** — Run: `cmake --build /Volumes/Archive/jefecheck-builds/video -j8 && grep -rn "not linked\|under GPL v2\b" README.md CLAUDE.md CLA.md src/qt/MainWindow_qt.cpp packaging/ffmpeg-NOTICE.txt; echo grep-exit=$?` — Expected: build OK and `grep-exit=1` (no stale wording).

- [ ] **Step 5: Commit** — `git add -A LICENSE README.md CLA.md CLAUDE.md THIRD_PARTY_LICENSES packaging/ffmpeg-NOTICE.txt developer_notes.md scripts/add_spdx_headers.sh src && git commit -m "docs: relicense to GPL v2 or later; document movie loading, audio and FFmpeg linking"`

---

### Task 14: Release packaging of the FFmpeg libraries

**Files:**
- Create: `scripts/bundle_macos_libav.sh`
- Modify: `scripts/create_macos_bundle.sh:24-40`, `scripts/create_linux_package.sh`, `scripts/install_linux.sh`, `.github/workflows/release.yml:102-118`

**Interfaces:**
- Produces: `scripts/bundle_macos_libav.sh <App.app>` copies every `libav*`/`libsw*` dylib that `otool -L` reports (transitively) into `Contents/Frameworks/`, rewrites each with `install_name_tool -id @rpath/<name>` and `-change <old> @rpath/<name>`, adds `-add_rpath @executable_path/../Frameworks` to the binary, and re-signs ad hoc; called from `create_macos_bundle.sh` before `codesign`. Linux: `create_linux_package.sh` writes `DEPENDENCIES` (`libavformat60 libavcodec60 libavutil58 libswresample4 libswscale7 libqt6multimedia6` for Ubuntu 24.04, plus the apt line) into the tarball and `install_linux.sh` checks `ldconfig -p` for `libavcodec.so` and `libQt6Multimedia.so`, printing the apt line when missing. Windows (`release.yml`): after the `ldd … | grep mingw` copy, `cp -r /mingw64/share/qt6/plugins/multimedia "$PACKAGE/"` and a verification line `ls "$PACKAGE"/avcodec-*.dll "$PACKAGE"/Qt6Multimedia.dll "$PACKAGE"/multimedia/*.dll` (fails the job when missing).

- [ ] **Step 1: Write the failing check** — Run: `cmake -S . -B /Volumes/Archive/jefecheck-builds/video-rel -DCMAKE_BUILD_TYPE=Release && cmake --build /Volumes/Archive/jefecheck-builds/video-rel -j8 && rm -rf JefeCheck.app && ln -sfn /Volumes/Archive/jefecheck-builds/video-rel build && bash scripts/create_macos_bundle.sh 1.7.0 && otool -L JefeCheck.app/Contents/MacOS/jefecheck | grep -c "/opt/homebrew/opt/ffmpeg"` — Expected: a count > 0 (libav still resolved from Homebrew). (Remove the `build` symlink afterwards.)

- [ ] **Step 2: Implement** the three packaging changes and the release.yml lines.

- [ ] **Step 3: Verify** — Run: `bash scripts/create_macos_bundle.sh 1.7.0 && otool -L JefeCheck.app/Contents/MacOS/jefecheck | grep -c "/opt/homebrew/opt/ffmpeg"; ls JefeCheck.app/Contents/Frameworks | grep -c "libavcodec"` — Expected: `0` then `1`; and `DYLD_PRINT_LIBRARIES=1 JefeCheck.app/Contents/MacOS/jefecheck --video-load-test 2>&1 | grep -c "Frameworks/libavcodec"` prints `1`. Run `bash scripts/create_linux_package.sh 1.7.0` on Linux (or via CI) — Expected: the tarball contains `DEPENDENCIES`. Push the branch and open a draft PR so `build.yml` runs on all three platforms — Expected: green.

- [ ] **Step 4: Commit** — `git add scripts/bundle_macos_libav.sh scripts/create_macos_bundle.sh scripts/create_linux_package.sh scripts/install_linux.sh .github/workflows/release.yml && git commit -m "packaging: bundle libav dylibs on macOS, Qt multimedia plugin and DLLs on Windows, declare Linux deps"`

---

## Self-review notes

- Spec coverage: formats/containers (T2, T5), in-memory decode (T2, T5), sequence behaviour (T4, T5), colour from tags with 709/video default and no chroma shift (T6), 16-bit precision (T5, T6), sound mixed and in sync (T9–T11), mute on the lane header + waveform (T12), step/scrub slices (T10–T11), same code on every platform (libav + Qt Multimedia, T1/T11/T14), numbering bypass (T4), frame numbering + source timecode + drop-frame + no `(int)targetFPS` (T3, T7), rate set by load / rational / other tracks at player rate / VFR note (T7), native-rate audio leads, off-rate slices, mixed case, offsets/hold/trims/loop, reverse silent, fast-scrub drop, downmix/resample (T9–T11), notes per frame (unchanged, T4 normalisation), fingerprint/stamp/sessions/playlists/review packages (T7, T8), render unchanged (T6 renders through the plate), remote unchanged, performance target (T6 perf test), loader model unchanged + seekable reader + hwaccel seam (T2, T5), TU separation (T1, T11), CMake/CI/packaging (T1, T14), licensing (T13), every listed verification (`--video-load-test` suites, colour, sync, step-slice, off-rate, rate/timecode, performance, mute persistence).
- Not placed: a UI-level test of the lane speaker toggle (the spec says lanes are verified manually; T12 has the manual step and the bridge-level test).
- Type names used across tasks: `gfcMovieInfo`, `gfcMovieFrame`, `gfcMovieReader`, `gfcAudioClip`, `gfcAudioPeaks`, `gfcAudioEngine`, `gfcAudioOutput`, `gfcFakeAudioOutput`, `gfcAudioTransport`, `TrackAudioState`, `WaveformPeaks`, `TrackTimelineState` fields — checked consistent between the task that defines them and the tasks that use them.
