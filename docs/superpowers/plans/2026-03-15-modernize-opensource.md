# JefeCheck Open-Source Modernization Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modernize JefeCheck for open-source release under GPL v2, replacing proprietary dependencies and unifying the build system under CMake, achieving sequence playback on macOS as the MVP.

**Architecture:** Keep the existing manager-pattern architecture (PlaybackManager, TrackManager, PlateManager, etc.) and FLTK-based GUI. Replace proprietary image I/O (GFL) with OpenImageIO, swap FLU widgets for native FLTK, migrate Boost to C++20 STL, and replace GLEW with GLAD. Unify all platform builds under CMake.

**Tech Stack:** C++20, CMake, FLTK (GUI), OpenImageIO (image I/O), GLAD (OpenGL loading), OpenGL, OpenEXR, libcurl, zlib, RakNet (networking), CLI11 (argument parsing)

**Spec:** `docs/superpowers/specs/2026-03-15-modernize-opensource-design.md`

---

**Note:** The repository will be in a non-compilable state from Task 4 through Task 14. This is expected — files are removed first (Phase 1), build system targets the final dependency set (Phase 2), then code-level swaps restore compilability (Phase 3), and finally compilation is achieved (Phase 4).

---

## Chunk 1: Repository Cleanup (Phase 1)

### Task 1: Remove dead directories

**Files:**
- Remove: `common/website/` (entire directory)
- Remove: `common/licenseGenerator/` (entire directory)
- Remove: `win/buildDependencies/` (entire directory)
- Remove: `win/InstallerFolder/NSIS/` (bundled NSIS dist, keep `.nsi` scripts in `win/InstallerFolder/`)

- [ ] **Step 1: Remove `common/website/`**

```bash
git rm -r common/website/
```

- [ ] **Step 2: Remove `common/licenseGenerator/`**

```bash
git rm -r common/licenseGenerator/
```

- [ ] **Step 3: Remove `win/buildDependencies/`**

```bash
git rm -r win/buildDependencies/
```

- [ ] **Step 4: Remove `win/InstallerFolder/NSIS/`**

```bash
git rm -r win/InstallerFolder/NSIS/
```

- [ ] **Step 5: Commit**

```bash
git commit -m "chore: remove dead directories (website, license generator, build deps, NSIS dist)"
```

---

### Task 2: Remove old build system files

**Files:**
- Remove: `win/jefecheck.sln`
- Remove: `win/jefecheck.vcxproj`
- Remove: `win/jefecheck.vcxproj.filters`
- Remove: `src/Makefile.am`
- Remove: `linux/jefecheck/Makefile.am`
- Remove: `linux/jefecheck/configure.in`
- Remove: `linux/jefecheck/Makefile.cvs`
- Remove: `linux/jefecheck/Makefile.in`
- Remove: `linux/jefecheck/config.h.in`
- Remove: `linux/jefecheck/configure`
- Remove: `linux/jefecheck/config.guess`
- Remove: `linux/jefecheck/config.sub`
- Remove: `linux/jefecheck/depcomp`
- Remove: `linux/jefecheck/install-sh`
- Remove: `linux/jefecheck/missing`
- Remove: `linux/jefecheck/mkinstalldirs`
- Remove: `linux/jefecheck/Doxyfile`
- Remove: `linux/jefecheck/AUTHORS`
- Remove: `linux/jefecheck/COPYING`
- Remove: `linux/jefecheck/INSTALL`
- Remove: `linux/jefecheck/macOSXbuildScript.sh`
- Remove: `linux/createInstallPackage.sh`

- [ ] **Step 1: Remove Windows build files**

```bash
git rm win/jefecheck.sln win/jefecheck.vcxproj win/jefecheck.vcxproj.filters
```

- [ ] **Step 2: Remove Makefile.am files**

```bash
git rm src/Makefile.am linux/jefecheck/Makefile.am
```

- [ ] **Step 3: Remove Linux autotools files**

```bash
git rm linux/jefecheck/configure.in linux/jefecheck/Makefile.cvs \
  linux/jefecheck/Makefile.in linux/jefecheck/config.h.in \
  linux/jefecheck/configure linux/jefecheck/config.guess \
  linux/jefecheck/config.sub linux/jefecheck/depcomp \
  linux/jefecheck/install-sh linux/jefecheck/missing \
  linux/jefecheck/mkinstalldirs linux/jefecheck/Doxyfile \
  linux/jefecheck/AUTHORS linux/jefecheck/COPYING \
  linux/jefecheck/INSTALL linux/jefecheck/macOSXbuildScript.sh \
  linux/createInstallPackage.sh
```

- [ ] **Step 4: Commit**

```bash
git commit -m "chore: remove old build systems (VS2010, autotools, mac scripts)"
```

---

### Task 3: Remove activation system and Botan references

**Files:**
- Remove: `src/activatorWindow.cxx`
- Remove: `src/activatorWindow.fl`
- Remove: `src/activatorWindow.h`
- Remove: `src/activatorCallbacks.cpp`
- Remove: `src/activatorCallbacks.h`
- Remove: `src/demoversion.h`
- Remove: `src/CheckMateBetaDemoVersionWatermark.h`
- Remove: `src/JefeCorp_JefeCheck1LicenseDSA_public.h`
- Remove: `src/JefeCorp_JefeCheck_LicenseRSA_public.h`
- Remove: `src/JefeCorp_JefeCheck1_ActivatorRSA_public.h`
- Modify: `src/main.cpp` — remove Botan includes (lines 56-58), public key includes (lines 369-370), and entire `checkLicense()` function (lines 371-600+), remove license check call from `main()`
- Modify: `src/gfcStructures.h` — remove any Botan includes
- Modify: `src/gfcfx.cpp` — remove Botan includes (lines 9-11: `botan/botan.h`, `botan/look_pk.h`, `botan/dsa.h`)

- [ ] **Step 1: Remove activation source files**

```bash
git rm src/activatorWindow.cxx src/activatorWindow.fl src/activatorWindow.h \
  src/activatorCallbacks.cpp src/activatorCallbacks.h \
  src/demoversion.h src/CheckMateBetaDemoVersionWatermark.h \
  src/JefeCorp_JefeCheck1LicenseDSA_public.h \
  src/JefeCorp_JefeCheck_LicenseRSA_public.h \
  src/JefeCorp_JefeCheck1_ActivatorRSA_public.h
```

- [ ] **Step 2: Remove Botan includes and license check from `main.cpp`**

Remove lines 56-58 (Botan includes):
```cpp
// DELETE these lines:
#include <botan/rsa.h>
#include <botan/pk_filts.h>
#include <botan/filters.h>
```

Remove lines 369-370 (public key includes):
```cpp
// DELETE these lines:
#include "JefeCorp_JefeCheck1LicenseDSA_public.h"
#include "JefeCorp_JefeCheck_LicenseRSA_public.h"
```

Remove the entire `checkLicense()` function (lines ~371-600+) and its call site in `main()`.

Remove any `#include "activatorCallbacks.h"` and `#include "activatorWindow.h"` references.

Remove any `#include "demoversion.h"` and conditional compilation blocks that check demo/license status.

- [ ] **Step 3: Remove Botan includes from `gfcStructures.h` and `gfcfx.cpp`**

In `gfcStructures.h`, remove any lines including `<botan/...>` headers.

In `gfcfx.cpp`, remove lines 9-11:
```cpp
// DELETE these lines:
#include <botan/botan.h>
#include <botan/look_pk.h>
#include <botan/dsa.h>
```

- [ ] **Step 4: Search for remaining activation/Botan references**

```bash
grep -rn "botan\|activator\|checkLicense\|demoversion\|CheckMateBeta\|DEMO_VERSION" src/ --include="*.cpp" --include="*.h" --include="*.cxx"
```

Fix any remaining references found.

- [ ] **Step 5: Commit**

```bash
git commit -m "chore: remove activation system and Botan crypto dependency"
```

---

### Task 4: Remove GFL SDK files

**Files:**
- Remove: `src/gflC.h`
- Remove: `src/gflCBitmap.cpp`, `src/gflCBitmap.h`
- Remove: `src/gflCFormat.cpp`, `src/gflCFormat.h`
- Remove: `src/gflCFileInformation.cpp`, `src/gflCFileInformation.h`
- Remove: `src/gflCColor.h`, `src/gflCColormap.h`
- Remove: `src/gflCLibrary.h`
- Remove: `src/gflCLoadParams.h`, `src/gflCSaveParams.h`
- Remove: `src/gflCException.h`
- Remove: `src/libgfl.h`, `src/libgfle.h`
- Remove: `src/gfcimageloadergfl.cpp`, `src/gfcimageloadergfl.h`
- Remove: `src/gfcimagesaver_gfl.cpp`, `src/gfcimagesaver_gfl.h`
- Remove: `src/gfcimageprocessor.cpp`, `src/gfcimageprocessor.h`
- Remove: `src/gfcimageloaderfil.cpp`, `src/gfcimageloaderfil.h`

Note: Do NOT remove `gfcimageloaderdpx.*`, `gfcimageloaderexr.*`, `gfcimagesaver.*`, or `gfcimageloader.h` — these are kept.

- [ ] **Step 1: Remove GFL SDK wrapper files**

```bash
git rm src/gflC.h src/gflCBitmap.cpp src/gflCBitmap.h \
  src/gflCFormat.cpp src/gflCFormat.h \
  src/gflCFileInformation.cpp src/gflCFileInformation.h \
  src/gflCColor.h src/gflCColormap.h \
  src/gflCLibrary.h src/gflCLoadParams.h src/gflCSaveParams.h \
  src/gflCException.h src/libgfl.h src/libgfle.h
```

- [ ] **Step 2: Remove GFL-based loaders/savers/processors**

```bash
git rm src/gfcimageloadergfl.cpp src/gfcimageloadergfl.h \
  src/gfcimagesaver_gfl.cpp src/gfcimagesaver_gfl.h \
  src/gfcimageprocessor.cpp src/gfcimageprocessor.h \
  src/gfcimageloaderfil.cpp src/gfcimageloaderfil.h
```

- [ ] **Step 3: Remove GFL references from remaining source files**

Search for remaining GFL references:
```bash
grep -rn "gflC\|libgfl\|libgfle\|GFL_\|gfl_\|gfcimageloadergfl\|gfcimagesaver_gfl\|gfcimageprocessor\|gfcimageloaderfil" src/ --include="*.cpp" --include="*.h" --include="*.cxx"
```

Comment out or remove `#include` directives and code blocks referencing GFL in files like:
- `src/gfcimageloaderdpx.cpp` — uses `GFL_COLOR` and `gflCrop()`, these need to be replaced with direct pixel manipulation
- `src/gfcimagesaver.cpp` — dispatches to GFL saver, needs to be updated to use OIIO
- `src/gfcimageloader.h` — may reference GFL loader types

Leave `// TODO: replace with OIIO` comments where GFL functionality is removed from kept files.

- [ ] **Step 4: Commit**

```bash
git commit -m "chore: remove GFL SDK files and references"
```

---

### Task 5: Remove vendored GLEW source

**Files:**
- Remove: `src/glew.c`
- Remove: `src/glew.h`
- Remove: `src/glext.h`
- Remove: `src/glxew.h`
- Remove: `src/wglew.h`

Note: Do NOT fix the 40+ files that `#include "glew.h"` yet — that happens in Phase 3 (Task 9).

- [ ] **Step 1: Remove GLEW source files**

```bash
git rm src/glew.c src/glew.h src/glext.h src/glxew.h src/wglew.h
```

- [ ] **Step 2: Commit**

```bash
git commit -m "chore: remove vendored GLEW source (will be replaced by GLAD)"
```

---

### Task 6: Add open-source project files

**Files:**
- Create: `LICENSE` (GPL v2 full text)
- Create: `CLA.md`
- Create: `README.md`
- Create: `THIRD_PARTY_LICENSES`
- Modify: `.gitignore` — rewrite for modern project structure

- [ ] **Step 1: Add GPL v2 LICENSE file**

Download the standard GPL v2 text:
```bash
curl -sL https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt > LICENSE
```

- [ ] **Step 2: Create CLA.md**

Write a simple Contributor License Agreement that grants the project owner the right to relicense contributions. The CLA should state:
- Contributors grant a perpetual, worldwide, non-exclusive license
- The project owner may relicense the project under different terms
- Contributors represent they have the right to make the contribution

- [ ] **Step 3: Create README.md**

Include sections for:
- Project description (professional video frame processing and playback)
- Screenshot placeholder
- Features (real-time playback, color correction, effects, multi-track, networking)
- Building from source (macOS instructions with `brew install` + `cmake`)
- Usage (basic CLI: `jefecheck [options] <files>`)
- CLI options (from the existing `--help` output)
- Contributing (reference CLA.md)
- License (GPL v2)

- [ ] **Step 4: Create THIRD_PARTY_LICENSES**

Document vendored third-party code:
- xmlParser (BSD 3-Clause, Frank Vanden Berghen)
- RakNet (GPL v2, Jenkins Software)
- Rijndael/AES (AES crypt project license)

- [ ] **Step 5: Rewrite .gitignore**

Replace the current deny-all pattern with a standard `.gitignore`:
```
# Build
build/
cmake-build-*/

# IDE
.idea/
.vscode/
*.xcodeproj/
*.xcworkspace/

# OS
.DS_Store
Thumbs.db

# Compiled
*.o
*.obj
*.exe
*.dll
*.dylib
*.so

# Visual Studio
*.sdf
*.opensdf
*.suo
*.user
*.ncb

# Generated
*.autosave
```

- [ ] **Step 6: Commit**

```bash
git add -f LICENSE CLA.md README.md THIRD_PARTY_LICENSES .gitignore
git commit -m "chore: add open-source project files (LICENSE, README, CLA, .gitignore)"
```

---

## Chunk 2: CMake Build System (Phase 2)

### Task 7: Create CMakeLists.txt

**Files:**
- Create: `CMakeLists.txt` (project root)
- Create: `cmake/FindOpenImageIO.cmake` (if needed — OIIO may ship its own config)
- Create: `third_party/glad/` (generated GLAD loader)
- Create: `third_party/cli11/` (CLI11 header)

The CMakeLists.txt should reference the FINAL dependency set. Do not include GFL, FLU, GLEW, or Boost. Source files that reference these removed libraries will have compile errors — that's expected and will be fixed in Phase 3.

- [ ] **Step 1: Generate GLAD loader**

Use the GLAD 1.x Python package (not glad2, which has a different API):
```bash
pip3 install glad
python3 -m glad --generator c --out-path third_party/glad --api gl=3.3 --profile core
```

This generates:
- `third_party/glad/include/glad/glad.h`
- `third_party/glad/include/KHR/khrplatform.h`
- `third_party/glad/src/glad.c`

- [ ] **Step 2: Download CLI11 header**

```bash
mkdir -p third_party/cli11
curl -sL https://github.com/CLIUtils/CLI11/releases/latest/download/CLI11.hpp -o third_party/cli11/CLI11.hpp
```

- [ ] **Step 3: Write CMakeLists.txt**

Create the root `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.20)
project(jefecheck VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find system packages
find_package(OpenGL REQUIRED)
find_package(FLTK REQUIRED)
find_package(OpenImageIO REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

# GLAD (in-tree)
add_library(glad STATIC third_party/glad/src/glad.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# CLI11 (header-only, in-tree)
add_library(cli11 INTERFACE)
target_include_directories(cli11 INTERFACE third_party/cli11)

# Collect source files
# Exclude removed files: GFL (gfl*), FLU-only files, activation, GLEW, old build
file(GLOB JEFECHECK_SOURCES
    src/*.cpp
    src/*.cxx
    src/*.c
)

# Platform-specific source exclusions
if(NOT WIN32)
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "WSAStartup|ExtendedOverlappedPool|wglew")
endif()
if(NOT APPLE)
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "osxGetPrimaryMacAddress")
endif()

# Main executable
add_executable(jefecheck ${JEFECHECK_SOURCES})

target_include_directories(jefecheck PRIVATE
    src/
    ${FLTK_INCLUDE_DIR}
    ${OPENIMAGEIO_INCLUDE_DIR}
    ${OPENEXR_INCLUDE_DIRS}
)

target_link_libraries(jefecheck PRIVATE
    glad
    cli11
    OpenGL::GL
    ${FLTK_LIBRARIES}
    OpenImageIO::OpenImageIO
    OpenEXR::OpenEXR
    CURL::libcurl
    ZLIB::ZLIB
)

# macOS frameworks
if(APPLE)
    target_link_libraries(jefecheck PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# Linux libraries
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(jefecheck PRIVATE
        Threads::Threads
        X11
    )
endif()
```

Note: This CMakeLists.txt will NOT compile cleanly until Phase 3 dependency swaps are done. It establishes the target build configuration.

- [ ] **Step 4: Verify CMake configures (expect build errors)**

```bash
brew install fltk openimageio openexr curl zlib
cmake -B build 2>&1 | head -50
```

CMake should configure successfully (find all packages). The actual `cmake --build build` will fail with many errors — that's expected.

- [ ] **Step 5: Commit**

```bash
git add -f CMakeLists.txt third_party/
git commit -m "feat: add CMake build system targeting final dependency set"
```

---

## Chunk 3: Dependency Swaps — GLEW to GLAD and Boost to C++20 (Phase 3, steps 1-2)

### Task 8: Replace GLEW includes with GLAD

**Files to modify** (files that directly `#include "glew.h"` or `#include <GL/glew.h>` — files deleted in earlier tasks are excluded):
- `src/main.cpp`
- `src/GlViewport.cpp`
- `src/gfcPlate.cpp`, `src/gfcPlate.h`
- `src/gfcStructures.h`, `src/gfcStructures.cpp`
- `src/gfcSequence.cpp`
- `src/gfcTrack.cpp`
- `src/gfcframe.h`, `src/gfcframe.cpp`
- `src/gfcfx.h`, `src/gfcfx.cpp`
- `src/gfctrackmanager.h`
- `src/gfchistogram.h`
- `src/gfcplategui.h`
- `src/gfcglframeinfo.h`
- `src/gfcpickmanager.cpp`
- `src/gfcglsubwindow.cpp`
- `src/gfcimagesaver.cpp`
- `src/gfcimagesaver_exr.cpp`
- `src/gfcplaybackmanager.cpp`
- `src/playlistwindow.h`
- `src/fxcontrolwindow.h`
- `src/UICallbacks.h`
- `src/trackwidget.h`
- `src/trilerp.h`, `src/trilerp.cpp`
- `src/mtpoly.cpp`

- [ ] **Step 1: Replace all `#include "glew.h"` with `#include <glad/glad.h>`**

In every file listed above, replace:
```cpp
#include "glew.h"
// or
#include <GL/glew.h>
```
with:
```cpp
#include <glad/glad.h>
```

- [ ] **Step 2: Replace `#include "wglew.h"` references**

In `src/GlViewport.cpp`, the Windows-specific `#include "wglew.h"` should be wrapped:
```cpp
#ifdef _WIN32
// wglew functionality now handled by GLAD
#endif
```

- [ ] **Step 3: Replace `glewInit()` with `gladLoadGL()`**

Find `glewInit()` call (likely in `GlViewport.cpp` or `main.cpp`). **Important:** GLAD must be initialized AFTER an OpenGL context is created. FLTK creates the context when `Fl_Gl_Window` is first shown or made current. The `glewInit()` call should already be in the right place — replace it with:
```cpp
if (!gladLoadGL()) {
    fprintf(stderr, "Failed to initialize OpenGL loader\n");
    // handle error appropriately for the context (may not be in main())
}
```

Do NOT move this call to the top of `main()` — it must remain where the OpenGL context is already active.

- [ ] **Step 4: Commit**

```bash
git commit -am "refactor: replace GLEW with GLAD for OpenGL loading"
```

---

### Task 9: Replace Boost with C++20 STL — threading and synchronization

**Files to modify:**
- `src/gfcframe.h` — `boost::try_mutex`
- `src/gfcSequence.h`, `src/gfcSequence.cpp` — `boost::thread`, `boost::try_mutex`, `boost::condition`
- `src/gfcTrack.h`, `src/gfcTrack.cpp` — `boost::thread`, `boost::try_mutex`, `boost::condition`
- `src/gfcmemorymanager.h`, `src/gfcmemorymanager.cpp` — `boost::mutex::scoped_lock`
- `src/gfctrackmanager.h` — `boost::thread`, `boost::try_mutex`
- `src/gfcimageloader.h` — `boost::condition`
- `src/gfcimageloaderdpx.cpp` — `boost::thread`, `boost::bind`
- `src/gfcimageloaderexr.cpp` — `boost::try_mutex::scoped_lock` (always-lock, not try-lock)
- `src/GlViewport.cpp` — `boost::try_mutex`, `boost::condition`

- [ ] **Step 1: Replace boost thread/mutex/condition includes**

In every file above, replace:
```cpp
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
```
with:
```cpp
#include <thread>
#include <mutex>
#include <condition_variable>
```

- [ ] **Step 2: Replace `boost::try_mutex` with `std::mutex`**

`boost::try_mutex` is just `boost::mutex` (try_lock is a method on all mutex types in C++). Replace:
```cpp
boost::try_mutex myMutex;
```
with:
```cpp
std::mutex myMutex;
```

- [ ] **Step 3: Replace `boost::try_mutex::scoped_try_lock` with `std::unique_lock`**

This is the most delicate change. Replace patterns like:
```cpp
boost::try_mutex::scoped_try_lock lock(myMutex);
if (lock.locked()) {
    // critical section
}
```
with:
```cpp
std::unique_lock<std::mutex> lock(myMutex, std::try_to_lock);
if (lock.owns_lock()) {
    // critical section
}
```

**Important:** There are TWO patterns to handle:

1. `scoped_try_lock` (try to acquire, check if succeeded) → `std::unique_lock` with `std::try_to_lock` as shown above
2. `scoped_lock` (always acquire, RAII release) → `std::lock_guard<std::mutex>` — used in `gfcmemorymanager.cpp` and `gfcimageloaderexr.cpp`:
```cpp
// Before:
boost::try_mutex::scoped_lock lock(myMutex);
// After:
std::lock_guard<std::mutex> lock(myMutex);
```

Check each usage site carefully — using try-lock where always-lock was intended (or vice versa) will cause deadlocks or race conditions.

- [ ] **Step 4: Replace `boost::condition` with `std::condition_variable`**

Replace:
```cpp
boost::condition myCondition;
myCondition.notify_one();
myCondition.wait(lock);
```
with:
```cpp
std::condition_variable myCondition;
myCondition.notify_one();
myCondition.wait(lock);
```

Note: `std::condition_variable::wait()` requires a `std::unique_lock<std::mutex>`, not a `std::lock_guard`. Verify the lock type at each wait site.

- [ ] **Step 5: Replace `boost::thread` with `std::thread`**

Replace:
```cpp
boost::thread myThread(function, args...);
```
with:
```cpp
std::thread myThread(function, args...);
```

For thread joining, ensure `join()` or `detach()` is called before the thread object is destroyed (C++20 `std::jthread` auto-joins, consider using it where appropriate).

- [ ] **Step 6: Commit**

```bash
git commit -am "refactor: replace Boost threading with C++20 std::thread/mutex/condition_variable"
```

---

### Task 10: Replace Boost with C++20 STL — filesystem

**Files to modify:**
- `src/gfcStructures.h` (lines 17, 73-75) — `boost::filesystem`
- `src/gfcStructures.cpp` (lines 23-25) — `boost::filesystem`
- `src/UICallbacks.cpp` (lines 21-23) — `boost::filesystem`

- [ ] **Step 1: Replace boost::filesystem includes**

Replace:
```cpp
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
```
with:
```cpp
#include <filesystem>
```

- [ ] **Step 2: Replace namespace references**

Replace all `boost::filesystem::` with `std::filesystem::` (or add `namespace fs = std::filesystem;` and use `fs::`).

Common replacements:
- `boost::filesystem::path` → `std::filesystem::path`
- `boost::filesystem::exists()` → `std::filesystem::exists()`
- `boost::filesystem::create_directories()` → `std::filesystem::create_directories()`
- `boost::filesystem::extension()` → `path.extension()`

- [ ] **Step 3: Commit**

```bash
git commit -am "refactor: replace Boost.Filesystem with std::filesystem"
```

---

### Task 11: Replace Boost with C++20 STL — bind, ref, and program_options

**Files to modify:**
- `src/UICallbacks.h` (lines 11-12) — `boost::bind`, `boost::ref`
- `src/UICallbacks.cpp` — `boost::bind`, `boost::ref`
- `src/gfcimageloaderdpx.cpp` (line 14) — `boost::bind`
- `src/main.cpp` (lines 81, 121-123, 236-367) — `boost::program_options`

- [ ] **Step 1: Replace `boost::bind` and `boost::ref`**

Replace:
```cpp
#include <boost/bind.hpp>
#include <boost/ref.hpp>
```
with:
```cpp
#include <functional>
```

Replace `boost::bind(func, args...)` with lambdas or `std::bind(func, args...)`.
Replace `boost::ref(x)` with `std::ref(x)`.

Prefer lambdas over `std::bind` where the code is clearer:
```cpp
// Before:
boost::bind(&Class::method, this, _1)
// After:
[this](auto& arg) { this->method(arg); }
```

- [ ] **Step 2: Replace `boost::program_options` with CLI11**

Rewrite the CLI parsing in `main.cpp` (lines 236-367). The existing options are:

```
--help,-h          : help message
--from,-f VALUE    : start frame per track (vector<int>)
--to,-t VALUE      : stop frame per track (vector<int>)
--scale,-s VALUE   : scale percentages per track (vector<int>)
--frameRate,-r VAL : playback frame rate (int)
--fx,-x VALUE      : FX stack files per track (vector<string>)
--lut,-l VALUE     : LUT files per plate (vector<string>)
<positional>       : input filenames (vector<string>)
```

Replace with CLI11:
```cpp
#include "CLI11.hpp"

// In main() or parseArguments():
CLI::App app{"JefeCheck - Professional Video Frame Player"};

std::vector<int> fromFrames, toFrames, scales;
int frameRate = 0;
std::vector<std::string> fxFiles, lutFiles, inputFiles;

app.add_option("-f,--from", fromFrames, "Start loading from this frame");
app.add_option("-t,--to", toFrames, "Stop loading at this frame");
app.add_option("-s,--scale", scales, "Scale percentages for sequences");
app.add_option("-r,--frameRate", frameRate, "Playback frame rate");
app.add_option("-x,--fx", fxFiles, "FX Stack files");
app.add_option("-l,--lut", lutFiles, "LUT files for plates");
app.add_option("input", inputFiles, "Input files")->expected(-1);

CLI11_PARSE(app, argc, argv);
```

Then adapt the processing logic (lines 285-366) to use the new variables. The logic itself stays the same — it's the option parsing that changes.

- [ ] **Step 3: Remove all remaining boost includes**

Search for any remaining boost references:
```bash
grep -rn "boost" src/ --include="*.cpp" --include="*.h" --include="*.cxx"
```

Remove any remaining `#include <boost/...>` lines.

- [ ] **Step 4: Commit**

```bash
git commit -am "refactor: replace Boost.Bind/Ref with std and Boost.ProgramOptions with CLI11"
```

---

## Chunk 4: Dependency Swaps — FLU and GFL (Phase 3, steps 3-4)

### Task 12: Replace FLU widgets with native FLTK

**Files to modify:**
- `src/playlistwindow.cpp`, `src/playlistwindow.h`
- `src/fxcontrolwindow.cpp`, `src/fxcontrolwindow.h`
- `src/UICallbacks.cpp`
- `src/drawingToolsWindow.cxx`, `src/drawingToolsWindow.h`, `src/drawingToolsWindow.fl`

**Widget replacements:**
| FLU | FLTK | Notes |
|-----|------|-------|
| `Flu_File_Chooser` | `Fl_File_Chooser` | Similar API |
| `Flu_Combo_Box` | `Fl_Input_Choice` | Slightly different API |
| `Flu_Button` | `Fl_Button` | Drop-in |
| `Flu_Choice_Group` | `Fl_Tabs` or `Fl_Group` | May need layout adjustment |
| `Flu_Tree_Browser` | `Fl_Tree` | Different API: node-based vs path-string |
| `Flu_Spinner` | `Fl_Spinner` | Drop-in |
| `Flu_Collapsable_Group` | `Fl_Group` | Remove collapse feature |

- [ ] **Step 1: Replace FLU includes with FLTK equivalents**

In all files above, replace:
```cpp
#include <FLU/Flu_File_Chooser.h>
#include <FLU/Flu_Button.h>
// etc.
```
with:
```cpp
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Tree.H>
#include <FL/Fl_Spinner.H>
#include <FL/Fl_Tabs.H>
// etc.
```

Also remove:
```cpp
#include <FLU/flu_pixmaps.h>
```

- [ ] **Step 2: Replace `Flu_Tree_Browser` with `Fl_Tree` in drawingToolsWindow**

This is the most complex swap. `Flu_Tree_Browser` uses path-string insertion:
```cpp
tree->add("path/to/node");
```

`Fl_Tree` uses a similar path-based API but with different method names:
```cpp
tree->add("path/to/node");  // Same!
tree->callback(cb);         // Similar
```

The main differences are in selection handling and node access. Update `drawingToolsWindow.fl` to declare `Fl_Tree` instead of `Flu_Tree_Browser`, then update the `.cxx` callbacks accordingly.

- [ ] **Step 3: Replace `Flu_Combo_Box` with `Fl_Input_Choice`**

```cpp
// Before:
Flu_Combo_Box *combo = new Flu_Combo_Box(x, y, w, h, "Label");
combo->input.value("default");
// After:
Fl_Input_Choice *combo = new Fl_Input_Choice(x, y, w, h, "Label");
combo->value("default");
```

- [ ] **Step 4: Replace remaining FLU widgets (Button, Spinner, etc.)**

These are mostly drop-in replacements. Change the class names and verify the API calls match.

- [ ] **Step 5: Search for remaining FLU references**

```bash
grep -rn "FLU\|Flu_\|flu_" src/ --include="*.cpp" --include="*.h" --include="*.cxx" --include="*.fl"
```

Fix any remaining references.

- [ ] **Step 6: Commit**

```bash
git commit -am "refactor: replace FLU widgets with native FLTK equivalents"
```

---

### Task 13: Replace GFL image loading with OpenImageIO

**Files:**
- Create: `src/gfcimageloaderoiio.cpp`
- Create: `src/gfcimageloaderoiio.h`
- Modify: `src/gfcimageloader.h` — update loader type registry
- Modify: `src/gfcimagesaver.cpp` — replace GFL save path with OIIO
- Modify: `src/gfcimageloaderdpx.cpp` — remove GFL_COLOR and gflCrop() references, use direct pixel ops

- [ ] **Step 1: Read the existing `gfcimageloader.h` interface**

Understand the base class interface that all loaders implement. Key methods to implement:
- Loading a frame from a file path into a pixel buffer
- Format detection
- Metadata extraction (resolution, bit depth, etc.)

- [ ] **Step 2: Create `gfcimageloaderoiio.h`**

```cpp
#ifndef GFCIMAGELOADEROIIO_H
#define GFCIMAGELOADEROIIO_H

#include "gfcimageloader.h"
#include <OpenImageIO/imageio.h>

class gfcImageLoaderOIIO : public gfcImageLoader {
public:
    gfcImageLoaderOIIO();
    ~gfcImageLoaderOIIO();

    // Implement the same interface as gfcimageloadergfl
    // Match method signatures from gfcimageloader.h base class
};

#endif
```

- [ ] **Step 3: Create `gfcimageloaderoiio.cpp`**

Implement image loading using OIIO:
```cpp
#include "gfcimageloaderoiio.h"
#include <OpenImageIO/imageio.h>

// Use OIIO::ImageInput to read files:
auto inp = OIIO::ImageInput::open(filename);
if (!inp) { /* error handling */ }
const OIIO::ImageSpec &spec = inp->spec();
int width = spec.width;
int height = spec.height;
int channels = spec.nchannels;
// Read pixels into buffer
inp->read_image(0, 0, 0, channels, OIIO::TypeDesc::UINT8, buffer);
inp->close();
```

Match the pixel format and buffer layout that the rest of JefeCheck expects (check what `gfcimageloadergfl.cpp` produced).

- [ ] **Step 4: Update `gfcimagesaver.cpp` to use OIIO for saving**

Replace the GFL save dispatch with OIIO:
```cpp
#include <OpenImageIO/imageio.h>

auto out = OIIO::ImageOutput::create(filename);
if (!out) { /* error */ }
OIIO::ImageSpec spec(width, height, channels, OIIO::TypeDesc::UINT8);
out->open(filename, spec);
out->write_image(OIIO::TypeDesc::UINT8, pixels);
out->close();
```

- [ ] **Step 5: Fix `gfcimageloaderdpx.cpp` GFL references**

Remove `gflCrop()` calls (line 141) and `GFL_COLOR` references (lines 162-173). Replace with direct pixel manipulation:
- For cropping: implement a simple pixel buffer crop (copy sub-rectangle)
- For color swapping: implement direct RGB/BGR channel swap

- [ ] **Step 6: Update loader registration**

In whatever file registers/selects image loaders (likely `gfcimageloader.h` or a manager), replace GFL loader references with OIIO loader.

- [ ] **Step 7: Verify no GFL references remain**

```bash
grep -rn "gfl\|GFL" src/ --include="*.cpp" --include="*.h" --include="*.cxx"
```

- [ ] **Step 8: Commit**

```bash
git add src/gfcimageloaderoiio.cpp src/gfcimageloaderoiio.h
git commit -am "feat: replace GFL image loading with OpenImageIO"
```

---

## Chunk 5: Get It Running — MVP on macOS (Phase 4)

### Task 14: Get the project compiling on macOS

- [ ] **Step 1: Attempt a full build**

```bash
cmake -B build
cmake --build build 2>&1 | head -100
```

- [ ] **Step 2: Fix compile errors iteratively**

Common expected errors:
- Missing includes (removed files still referenced)
- Type mismatches from Boost→STL migration
- FLTK API differences from FLU→FLTK migration
- OpenImageIO API usage issues
- GLAD initialization order issues
- C++20 compatibility issues in older code

Fix each error, working through the compiler output. Each logical group of fixes should be its own commit:
```bash
git commit -am "fix: resolve compile errors from [specific area]"
```

- [ ] **Step 3: Get to a clean compile (zero errors)**

```bash
cmake --build build 2>&1
```

Expected: Build succeeds, producing `build/jefecheck` binary.

- [ ] **Step 4: Commit any remaining fixes**

```bash
git commit -am "fix: achieve clean compile on macOS"
```

---

### Task 15: Get the app launching and playing sequences

- [ ] **Step 1: Launch the app**

```bash
./build/jefecheck
```

Fix any runtime crashes (segfaults, missing resources, initialization order issues).

- [ ] **Step 2: Test file chooser**

Open a file via the UI. Verify `Fl_File_Chooser` works as a replacement for `Flu_File_Chooser`.

- [ ] **Step 3: Test image loading**

Load a DPX or EXR sequence. Verify frames load into memory via the OIIO or dedicated loaders.

- [ ] **Step 4: Test viewport rendering**

Verify frames render to the OpenGL viewport via GLAD. Check that `gladLoadGL()` initializes properly and textures display correctly.

- [ ] **Step 5: Test playback controls**

Verify play, stop, scrub, and frame step work for a single sequence on a single plate.

- [ ] **Step 6: Commit MVP**

```bash
git commit -am "milestone: MVP achieved — sequence playback working on macOS"
```

---

## Chunk 6: Cross-Platform (Phase 5)

### Task 16: Linux build

- [ ] **Step 1: Test CMake configuration on Linux**

```bash
sudo apt install libfltk1.3-dev libopenimageio-dev libopenexr-dev libcurl4-openssl-dev zlib1g-dev
cmake -B build
```

- [ ] **Step 2: Fix any Linux-specific compile errors**

Address platform-specific issues:
- Header path differences
- Missing platform defines
- X11/Wayland considerations for FLTK
- pthread linking

- [ ] **Step 3: Test launch and basic playback**

```bash
./build/jefecheck <sequence_files>
```

- [ ] **Step 4: Commit Linux fixes**

```bash
git commit -am "fix: Linux build support"
```

---

### Task 17: Windows build

- [ ] **Step 1: Set up vcpkg and install dependencies**

```powershell
vcpkg install fltk openimageio openexr curl zlib
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg-toolchain]
```

- [ ] **Step 2: Fix Windows-specific compile errors**

Address:
- Windows socket initialization (WSAStartup)
- Windows-specific RakNet code
- MSVC compiler warnings/errors
- Resource file (`jefecheck.rc`) linking

- [ ] **Step 3: Test launch and basic playback**

- [ ] **Step 4: Commit Windows fixes**

```bash
git commit -m "fix: Windows build support"
```

---

### Task 18: CI via GitHub Actions

**Files:**
- Create: `.github/workflows/build.yml`

- [ ] **Step 1: Create multi-platform CI workflow**

```yaml
name: Build
on: [push, pull_request]
jobs:
  build:
    strategy:
      matrix:
        os: [macos-latest, ubuntu-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Install deps (macOS)
        if: runner.os == 'macOS'
        run: brew install fltk openimageio openexr curl zlib
      - name: Install deps (Linux)
        if: runner.os == 'Linux'
        run: sudo apt-get install -y libfltk1.3-dev libopenimageio-dev libopenexr-dev libcurl4-openssl-dev zlib1g-dev
      - name: Install deps (Windows)
        if: runner.os == 'Windows'
        run: |
          vcpkg install fltk openimageio openexr curl zlib
      - name: Configure
        run: cmake -B build
      - name: Build
        run: cmake --build build
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "ci: add multi-platform build workflow"
```
