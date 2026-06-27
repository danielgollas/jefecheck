#!/bin/bash
set -e

VERSION="${1:-1.7.0}"
APP="JefeCheck.app"

echo "Creating macOS app bundle ($VERSION)..."

mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"

# The macOS build sets MACOSX_BUNDLE, so the binary lands inside a .app at
# build/jefecheck.app/Contents/MacOS/jefecheck. Older non-bundle builds put
# it at build/jefecheck. Accept either.
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

# Bundle a static FFmpeg (video export) into Resources/, where
# VideoEncoder_Qt::findFfmpeg() looks first. GPL static build — compatible
# with this GPL v2 app. Set JEFECHECK_FFMPEG_BIN to use a local binary
# instead of downloading (offline/dev packaging).
FFMPEG_VERSION="b6.1.1"
if [ -n "${JEFECHECK_FFMPEG_BIN:-}" ] && [ -f "$JEFECHECK_FFMPEG_BIN" ]; then
    cp "$JEFECHECK_FFMPEG_BIN" "$APP/Contents/Resources/ffmpeg"
else
    curl -fL -o "$APP/Contents/Resources/ffmpeg" \
        "https://github.com/eugeneware/ffmpeg-static/releases/download/${FFMPEG_VERSION}/ffmpeg-darwin-arm64"
fi
chmod +x "$APP/Contents/Resources/ffmpeg"
cp packaging/ffmpeg-NOTICE.txt "$APP/Contents/Resources/" 2>/dev/null || true

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
