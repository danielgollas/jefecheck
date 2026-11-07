#!/bin/bash
set -e

VERSION="${1:-1.7.0}"
PACKAGE="jefecheck-${VERSION}-linux-x64"

echo "Creating Linux package ($VERSION)..."

mkdir -p "$PACKAGE/bin"
mkdir -p "$PACKAGE/share/jefecheck"

cp build/jefecheck "$PACKAGE/bin/"
cp -r src/FX "$PACKAGE/share/jefecheck/"
cp -r src/fonts "$PACKAGE/share/jefecheck/"
cp scripts/install_linux.sh "$PACKAGE/install.sh"
chmod +x "$PACKAGE/install.sh"

# Bundle a static FFmpeg (video export) next to the binary, where
# VideoEncoder_Qt::findFfmpeg() looks first. GPL static build. Override
# with JEFECHECK_FFMPEG_BIN for offline/dev packaging.
FFMPEG_VERSION="b6.1.1"
if [ -n "${JEFECHECK_FFMPEG_BIN:-}" ] && [ -f "$JEFECHECK_FFMPEG_BIN" ]; then
    cp "$JEFECHECK_FFMPEG_BIN" "$PACKAGE/bin/ffmpeg"
else
    curl -fL -o "$PACKAGE/bin/ffmpeg" \
        "https://github.com/eugeneware/ffmpeg-static/releases/download/${FFMPEG_VERSION}/ffmpeg-linux-x64"
fi
chmod +x "$PACKAGE/bin/ffmpeg"
cp packaging/ffmpeg-NOTICE.txt "$PACKAGE/"

# FFmpeg and Qt Multimedia come from the distribution (Ubuntu 24.04), not the package; install.sh checks them.
cat > "$PACKAGE/DEPENDENCIES" << DEPS
Runtime packages (Ubuntu 24.04):
  libavformat60 libavcodec60 libavutil58 libswresample4 libswscale7 libqt6multimedia6

Install with:
  sudo apt install libavformat60 libavcodec60 libavutil58 libswresample4 libswscale7 libqt6multimedia6
DEPS

# Record the linked FFmpeg package versions, and ffmpeg -version when it is installed.
{
    echo "FFmpeg libraries this build was linked against (not bundled)"
    echo
    if command -v dpkg-query >/dev/null 2>&1; then
        dpkg-query -W -f '${Package} ${Version}\n' libavformat60 libavcodec60 libavutil58 libswresample4 libswscale7 libqt6multimedia6 2>/dev/null || true
    fi
    if command -v pkg-config >/dev/null 2>&1; then
        for l in libavformat libavcodec libavutil libswresample libswscale; do
            echo "$l $(pkg-config --modversion "$l" 2>/dev/null || echo unknown)"
        done
    fi
    if [ -x /usr/bin/ffmpeg ]; then
        echo
        /usr/bin/ffmpeg -hide_banner -version
    fi
} > "$PACKAGE/ffmpeg-libs-VERSION.txt"

tar czf "${PACKAGE}.tar.gz" "$PACKAGE"

echo "Created ${PACKAGE}.tar.gz"
