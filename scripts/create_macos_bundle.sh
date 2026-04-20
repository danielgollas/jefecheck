#!/bin/bash
set -e

VERSION="${1:-1.7.0}"
APP="JefeCheck.app"

echo "Creating macOS app bundle ($VERSION)..."

mkdir -p "$APP/Contents/MacOS"
mkdir -p "$APP/Contents/Resources"

cp build/jefecheck "$APP/Contents/MacOS/"
cp -r src/FX "$APP/Contents/Resources/"
cp -r src/fonts "$APP/Contents/Resources/"

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
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
</dict>
</plist>
PLIST

echo "Created $APP"
