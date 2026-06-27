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
cp packaging/ffmpeg-NOTICE.txt "$PACKAGE/" 2>/dev/null || true

tar czf "${PACKAGE}.tar.gz" "$PACKAGE"

echo "Created ${PACKAGE}.tar.gz"
