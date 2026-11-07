# Self-contained releases: design notes (future work)

Date: 2026-11-07. Status: **planned, not scheduled.** Pick it up as its own project (brainstorm, then plan, then PRs).

## Why

The macOS app only runs on a Mac that has Homebrew with the same libraries installed, and a `brew upgrade` can break it. The goal is one download per platform that runs on a clean machine.

## Where it stands (measured 2026-11-07, Qt build)

- **macOS:** `scripts/create_macos_bundle.sh` bundles FX, fonts, an `ffmpeg` binary and the libav* dylibs (`scripts/bundle_macos_libav.sh`, 14 dylibs), then ad-hoc signs the bundle.
  - Everything else still loads from `/opt/homebrew`: **80 libraries from 50 formulae**. Six are linked directly (qtbase, qtmultimedia, openimageio, openexr, imath, freetype). The rest come in through them, including opencolorio, libheif, libraw, icu4c, tbb, glib and harfbuzz.
  - `release.yml` wraps that bundle in a `.dmg` (`hdiutil`) on `macos-latest`. Nothing checks it for Homebrew paths.
- **Windows:** `release.yml` (MSYS2 UCRT64) copies the DLLs that `ldd` lists from `/ucrt64` and the Qt Multimedia plugin into a zip. The Qt **platform** plugin (`qwindows.dll`) isn't shipped, so the zip doesn't start on a clean machine.
- **Linux:** `scripts/create_linux_package.sh` makes a tarball that relies on system packages.

## Decisions

1. **A self-contained package per platform, not one static binary.**
   - A static build would mean rebuilding Qt, OpenImageIO and their roughly 50 dependencies as static libraries (e.g. with vcpkg static triplets), and macOS still needs the `.app` folder for its icon, plugins and Gatekeeper.
   - Static Qt is allowed under the LGPL, because JefeCheck's GPL source lets users relink, but the engineering cost isn't worth it.
   - Revisit only if the minimum-OS problem in step 3 can't be solved any other way.
2. **The `.app` is the single thing on macOS,** shipped in a signed, notarized `.dmg`. Windows ships a zip, Linux an AppImage.

## Steps, in order

1. **macOS bundling with `macdeployqt`** (no Apple account needed).
   - Run Homebrew's `macdeployqt` (`/opt/homebrew/opt/qt/bin/macdeployqt`) on the bundle. It copies the Qt frameworks, the platform, multimedia and image-format plugins, and the non-Qt dylibs, and rewrites their install names to `@rpath`/`@executable_path`.
   - Fold `bundle_macos_libav.sh` into it, or run it after if it misses anything.
   - Done when the app launches on a Mac or VM without Homebrew, opens an EXR and a ProRes movie, and plays sound.
2. **A leak check.**
   - A script walks `otool -L` from the main binary through every bundled dylib, framework and plugin, and fails if any path points outside `@rpath`, `@executable_path`, `@loader_path`, `/usr/lib` or `/System`.
   - It runs in `release.yml` after bundling and in a CI job on every PR that touches packaging.
3. **The minimum macOS version.** This is the open decision.
   - Homebrew's current Qt bottle for arm64 is built only for macOS 26 (`arm64_tahoe`), so a bundle made from Homebrew needs macOS 26 or newer.
   - Today's `Info.plist` claims `LSMinimumSystemVersion` 11.0, which is false; set it from the oldest `minos` among the bundled dylibs (`otool -l` / `vtool`), so it's true by construction.
   - To support older Macs, build Qt, OIIO and their dependencies from source with `MACOSX_DEPLOYMENT_TARGET` set (vcpkg with a custom triplet, or a cached CI build), instead of using Homebrew bottles.
   - Decide which macOS versions reviewers actually run before choosing.
4. **Signing and notarization.**
   - **Owner:** the Apple Developer Program ($99 a year), a "Developer ID Application" certificate, and an App Store Connect API key for `notarytool`. Store them as repository secrets.
   - **CI:** sign with the hardened runtime (`codesign --options runtime --timestamp`), innermost dylibs and frameworks first. Add entitlements only if needed; audio output needs none. Then `xcrun notarytool submit --wait`, `xcrun stapler staple`, and build the `.dmg` from the stapled app and sign it too.
   - Without this, Gatekeeper blocks the downloaded app; ad-hoc signing only works on the build machine.
5. **Windows:** run `windeployqt` (the MSYS2 `qt6-tools` package ships it) on `jefecheck.exe` after the build, to add the platform plugin, styles and the rest. Keep the `ldd` copy for the non-Qt DLLs. Done when it runs on a clean Windows VM.
   - Optional: an installer (Inno Setup or NSIS) and Authenticode signing, which needs a code-signing certificate (owner).
6. **Linux AppImage:** `linuxdeploy` with `linuxdeploy-plugin-qt`, built on the oldest Ubuntu the release targets (glibc sets the floor). Ship it beside the tarball.
7. **Licences.** The bundles redistribute Qt (LGPL v3), FFmpeg built with x264/x265 (GPL), OIIO/OpenEXR/OCIO (BSD) and others.
   - Generate `THIRD_PARTY_LICENSES` from the bundled set, and keep the FFmpeg notice and source offer (`packaging/ffmpeg-NOTICE.txt`).
   - The leak-check script can list what's bundled for this.

## Expected cost and size

- **Size:** the macOS app grows from about 41 MB to about 150–250 MB; the Windows zip grows similarly.
- **Effort:** steps 1–2 are a day or two. Step 3 depends on the decision: none if macOS 26+ is acceptable, days if a source-built dependency stack is needed. Step 4 is a day once the certificate exists. Steps 5–6 are a day each.

## Out of scope

- Mac App Store distribution, which needs sandboxing.
- Intel macOS builds.
- Auto-update (Sparkle).
