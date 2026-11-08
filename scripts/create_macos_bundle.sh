#!/bin/bash
set -e

VERSION="${1:-1.7.0}"
APP="JefeCheck.app"

echo "Creating macOS app bundle ($VERSION)..."

mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"

# The binary lives inside the .app at build/jefecheck.app/Contents/MacOS/jefecheck
# (MACOSX_BUNDLE) or at build/jefecheck; accept either.
if [ -f build/jefecheck ]; then
    SRC_BIN="build/jefecheck"
elif [ -f build/jefecheck.app/Contents/MacOS/jefecheck ]; then
    SRC_BIN="build/jefecheck.app/Contents/MacOS/jefecheck"
else
    echo "error: no built jefecheck binary under build/" >&2
    exit 1
fi
cp "$SRC_BIN" "$APP/Contents/MacOS/"
cp -r src/FX "$APP/Contents/Resources/"
cp -r src/fonts "$APP/Contents/Resources/"
cp packaging/macos/JefeCheck.icns "$APP/Contents/Resources/"

# Bundle a static FFmpeg (GPL build) into Resources/, where VideoEncoder_Qt::findFfmpeg()
# looks first; JEFECHECK_FFMPEG_BIN uses a local binary instead of downloading.
FFMPEG_VERSION="b6.1.1"
if [ -n "${JEFECHECK_FFMPEG_BIN:-}" ] && [ -f "$JEFECHECK_FFMPEG_BIN" ]; then
    cp "$JEFECHECK_FFMPEG_BIN" "$APP/Contents/Resources/ffmpeg"
else
    curl -fL -o "$APP/Contents/Resources/ffmpeg" \
        "https://github.com/eugeneware/ffmpeg-static/releases/download/${FFMPEG_VERSION}/ffmpeg-darwin-arm64"
fi
chmod +x "$APP/Contents/Resources/ffmpeg"
cp packaging/ffmpeg-NOTICE.txt "$APP/Contents/Resources/"

# Bundle the linked FFmpeg libraries; Qt and OpenImageIO are not bundled yet.
bash "$(dirname "$0")/bundle_macos_libav.sh" "$APP"

cat > "$APP/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>JefeCheck</string>
    <key>CFBundleDisplayName</key>
    <string>JefeCheck</string>
    <key>CFBundleIdentifier</key>
    <string>com.danielgollas.jefecheck</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>jefecheck</string>
    <key>CFBundleIconFile</key>
    <string>JefeCheck.icns</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
</dict>
</plist>
PLIST

echo "Ad-hoc signing $APP..."
codesign --force --deep --sign - "$APP"

echo "Created $APP"
