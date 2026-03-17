# JefeCheck Modernization Journal — March 16, 2026

## Starting Point

JefeCheck is a professional video frame player and color correction tool I wrote between 2006-2014. It was built for the VFX/cinema pipeline — real-time playback of DPX and EXR sequences with GPU-accelerated color grading, LUTs, and shader-based effects. It supported Windows, Linux, and macOS, and was sold commercially through JefeCorp.

The codebase had been untouched for over 10 years. It was hosted on Beanstalk (remember Beanstalk?), built with Visual Studio 2010 on Windows, GNU Autotools on Linux, and custom shell scripts on macOS. It depended on several proprietary libraries that made open-sourcing impossible.

Today, with the help of Claude Code, I brought it back to life as an open-source project in a single session.

## Getting the Code on GitHub

The git repository had been sitting on my local machine since the Beanstalk days. First problem: a corrupt blob — a file called `hacked_page` (from a website defacement incident years ago) had a missing git object. We used `git-filter-repo` to strip it from history entirely.

Second problem: GitHub's 100MB file limit. The repo had Visual Studio IntelliSense databases (`.sdf` files, up to 131MB) and debug builds of the Botan crypto library checked into history. Another `git-filter-repo` pass cleaned those out. The `.sdf` files were auto-generated VS artifacts that should never have been committed — a reminder of how much we've learned about `.gitignore` best practices since 2006.

With history cleaned up, all five branches pushed to GitHub successfully.

## The Dependency Problem

The biggest blocker for open-sourcing was the dependency chain:

- **GFL SDK** (Graphics File Library) — proprietary image format library from Pierre-e Gougelet. Handled 100+ image formats but required a commercial license. This was the workhorse for loading JPEGs, PNGs, TIFFs, and everything that wasn't DPX or EXR.
- **FLU** (FLTK Utility Widgets) — proprietary widget library from Ohio State University's Supercomputer Center. Enhanced file choosers, tree browsers, and combo boxes for the FLTK GUI toolkit.
- **Botan** — BSD-licensed crypto library, but only used for the license activation system. No activation needed in an open-source project.
- **RakNet** — networking library with GPL v2 licensing. Used for the remote control and synchronization features, which are a core differentiator.

Additionally, the code used Boost heavily (filesystem, threading, program_options) and vendored an ancient copy of GLEW from 2008.

## The Plan

We decided on GPL v2 for the license (compatible with RakNet), with a Contributor License Agreement so we could relicense to Apache 2.0 later once RakNet is eventually replaced.

The replacement strategy:
- GFL → **OpenImageIO** (the VFX industry standard, used by Nuke, Blender, Maya)
- FLU → **Native FLTK widgets** (FLTK 1.3+ now has equivalents for everything FLU provided)
- Boost → **C++20 STL** (std::filesystem, std::thread, std::mutex, std::condition_variable)
- Boost.ProgramOptions → **CLI11** (BSD, single-header)
- GLEW → **GLAD** (public domain, generated for exactly the GL version you need)
- Botan + activation system → **Removed entirely**
- Build system → **CMake** (replacing VS2010, Autotools, and shell scripts)

Target: macOS first (my development machine), then Linux, then Windows.

## The Cleanup

Phase 1 was aggressive pruning. We removed:
- The entire `common/website/` directory (old PHP website, CGI scripts, payment processing)
- `common/licenseGenerator/` (Perl scripts for generating license keys)
- `win/buildDependencies/` (vendored Windows binaries for all libraries)
- The bundled NSIS installer distribution
- All three old build systems (VS2010 .sln, Autotools configure/Makefile, mac scripts)
- The activation system (activator windows, public key files, license checking code, demo watermark)
- All GFL SDK files (headers, C++ wrappers, loaders, savers, processors)
- The vendored GLEW source

About 330,000 lines of code removed across 1,900+ files. The old `.gitignore` used a deny-all pattern (`*.*` then `!*.ext` for each allowed extension) — we replaced it with a standard modern `.gitignore`.

Added: GPL v2 LICENSE, CLA.md, README.md with build instructions, THIRD_PARTY_LICENSES for vendored code (RakNet, xmlParser, Rijndael).

## The CMake Migration

One `CMakeLists.txt` to replace three platform-specific build systems. It uses `find_package` for all external dependencies and includes GLAD and CLI11 in-tree (they're tiny). The CMake file was written targeting the final dependency set from the start — it never referenced the old libraries.

```bash
brew install fltk openimageio openexr curl zlib cmake
cmake -B build
cmake --build build
```

That's it. Three commands to build a project that previously required platform-specific toolchain setup.

## The Dependency Swaps

### GLEW → GLAD
Mechanical find-and-replace across 27 files. The one gotcha: GLAD function pointers are NULL until you call `gladLoadGL()`, unlike GLEW which uses stubs. FLTK's `Fl_Gl_Window` calls `draw()` before our initialization code runs, so we had to initialize GLAD on the first `draw()` call rather than in `main()`.

Also had to use GLAD's compatibility profile instead of core — the app uses legacy OpenGL extensively (fixed-function pipeline, `glBegin`/`glEnd`, `ftransform()` in shaders).

### Boost → C++20 STL
The threading migration was the most delicate. `boost::try_mutex::scoped_try_lock` became `std::unique_lock<std::mutex>` with `std::try_to_lock`. The pattern difference is subtle:

```cpp
// Boost (try-lock)
boost::try_mutex::scoped_try_lock lock(mutex);
if (lock.locked()) { ... }

// C++20 (try-lock)
std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
if (lock.owns_lock()) { ... }
```

Had to be careful not to confuse the try-lock pattern with the always-lock pattern (`scoped_lock` → `std::lock_guard`), or with `std::condition_variable::wait()` which requires `unique_lock`, not `lock_guard`.

`boost::filesystem` → `std::filesystem` was mostly mechanical. The one API difference: `branch_path()` became `parent_path()`.

### FLU → Native FLTK
Mostly drop-in replacements. `Flu_Tree_Browser` → `Fl_Tree` required adapting the selection handling code. The `.fl` files (FLTK's FLUID visual designer format) needed manual editing.

### GFL → OpenImageIO
This was the biggest change. Created a new `gfcImageLoaderOIIO` class implementing the same `gfcImageLoader` interface. OIIO's API is clean:

```cpp
auto inp = OIIO::ImageInput::open(filename);
inp->read_image(0, 0, 0, 4, OIIO::TypeDesc::UINT8, buffer);
```

The tricky part was pixel format — JefeCheck uses BGRA internally (matching GFL's convention), but OIIO reads as RGBA. Added an RGB↔BGR swizzle pass after reading.

For the DPX loader (which does its own DPX parsing but used GFL for bitmap allocation, resizing, and cropping), we created `gfcpixelbuffer.h` — a drop-in header providing `GFL_BITMAP`, `GFL_COLOR`, `gflResize()`, `gflCrop()`, etc. as simple standalone implementations. This let the DPX loader work without any GFL library.

### CLI11
Rewrote the argument parser in `main.cpp`. CLI11's API is more intuitive than Boost.ProgramOptions:

```cpp
CLI::App app{"JefeCheck"};
app.add_option("-r,--frameRate", frameRate, "Playback frame rate");
app.add_option("input", inputFiles, "Input files")->expected(-1);
CLI11_PARSE(app, argc, argv);
```

## Getting It to Compile

The first build attempt after all the swaps produced a cascade of errors. Most were mechanical:
- C++20 is stricter about rvalue-to-lvalue conversions (RakNet's `BitStream` had `ReadCompressed((float)cy)`)
- The `register` keyword was removed in C++17 (used in DPX parsing code)
- Apple Silicon (aarch64) wasn't in RakNet's endianness detection
- Template dependent base class access needed explicit `this->` (RakNet's `AVLBalancedBinarySearchTree`)
- `GLhandleARB` is `void*` on macOS but `GLuint` elsewhere (needed casts for `glShaderSource`)
- String literal concatenation with macros needed spaces in C++11+

One interesting discovery: `gfcTrack.cpp` and `gfcSequence.cpp` were identical files (same line count, only difference was the comment on line 3). The branch was called `subsequence` — apparently a refactoring that was never finished.

## Getting It to Run

First crash: SIGSEGV at address 0x0 during `Fl_Gl_Window::flush()`. GLAD's function pointers were all NULL because `gladLoadGL()` hadn't been called yet when FLTK first drew the GL window. Fixed by initializing GLAD in the first `draw()` call.

Second crash: `std::filesystem::create_directory` on a path whose parent didn't exist. Changed to `create_directories`.

Third issue: images loaded (verified via histogram) but didn't display. The OIIO loader wasn't setting `texCoords` for the `GL_TEXTURE_RECTANGLE_ARB` target. Rectangle textures use pixel-space coordinates (0 to width, 0 to height), not normalized (0 to 1). The default was (-1, -1, -1, -1) — a zero-size invisible quad.

Fourth issue: `glTexImage2D` returned `GL_INVALID_ENUM` with `GL_RGBA8` as internal format for rectangle textures on Apple's Metal-backed GL driver. Changed to `GL_RGBA`.

## The Shader System

The super shader (dynamically generated GLSL for color correction) crashed with a trace trap. Root cause: uninitialized `GLhandleARB` member variables in the `gfcPlate` constructor. The first shader build called `glDeleteObjectARB()` on garbage handles. Zero-initialized them and it worked.

Also fixed mixed ARB/modern GL calls — our GLAD migration had replaced some `glShaderSourceARB()` calls with `glShaderSource()`, but the handle types were still `GLhandleARB` (which is `void*` on macOS, not `GLuint`). Reverted to consistent ARB function usage.

Same uninitialized-handle fix was needed in `gfcFX` for the plugin effects system.

FX shader files (`.frag`, `.vert`, `.jfx`) live in `common/FX/` but the app looks for them in `./Resources/FX/` (macOS bundle convention). A symlink bridges the gap for development.

## The Result

Everything works:
- App launches on macOS (Apple M3, Metal-backed OpenGL 2.1)
- Images load via OpenImageIO (JPEG, PNG, EXR, TIFF, DPX, and 100+ other formats)
- Images display in the OpenGL viewport
- Sequence playback works
- Color correction (gamma, exposure, brightness, contrast, saturation) via GPU shaders
- 1D and 3D LUT support
- The entire FX plugin pipeline (shader-based effects loaded from .jfx files)
- CLI argument loading
- Networking code compiles (untested)

24 commits on the `modernize-opensource` branch. All proprietary dependencies removed. Ready for open-source release under GPL v2.

## What's Next

- Linux and Windows builds (CMake should make this straightforward)
- CI via GitHub Actions
- Image saving via OIIO (currently stubbed out)
- Image-based LUT loading via OIIO (currently stubbed out)
- Eventually replace RakNet with a modern networking library and relicense to Apache 2.0
- Maybe a GitHub Pages website
- Maybe Metal support someday (OpenGL is deprecated on macOS but still works)

## Reflections

The codebase aged surprisingly well. The core architecture — manager pattern, image loader interface, shader-based FX pipeline — is still sound. The main pain points were all about dependencies and build systems, not the application logic.

C++20 is a much nicer language than C++03 was. `std::filesystem`, `std::thread`, structured bindings, `auto` — the code is genuinely cleaner after the migration. The Boost dependency was the heaviest thing in the build and everything it provided is now in the standard library.

OpenImageIO is a massive upgrade over GFL. Not just for licensing — it's actively maintained, supports modern formats (HEIF, JPEG XL, OpenEXR 3.x), and is the standard in the VFX industry.

The whole modernization took about 6 hours with Claude Code. Without AI assistance, this would have been weeks of work — reading old build scripts, figuring out which files reference which libraries, tracking down every GFL type and function call, fixing C++20 compatibility issues in code written for C++03. The ability to search, read, edit, compile, and debug in a tight loop made it tractable as a single session.

Ten years of dust, cleaned off in an afternoon.
