#!/bin/bash
set -e
PREFIX="${1:-/usr/local}"

# The FFmpeg libraries (movie reading and sound) and Qt Multimedia come from
# the distribution, not this package (see DEPENDENCIES).
APT_LINE="sudo apt install libavformat60 libavcodec60 libavutil58 libswresample4 libswscale7 libqt6multimedia6"
MISSING=""
for lib in libavcodec.so libavformat.so libavutil.so libswresample.so libswscale.so libQt6Multimedia.so; do
    ldconfig -p | grep -q "$lib" || MISSING="$MISSING $lib"
done
if [ -n "$MISSING" ]; then
    echo "Missing runtime libraries:$MISSING"
    echo "Install them first (Ubuntu 24.04):"
    echo "  $APT_LINE"
    exit 1
fi

echo "Installing JefeCheck to $PREFIX..."
install -Dm755 bin/jefecheck "$PREFIX/bin/jefecheck"
mkdir -p "$PREFIX/share/jefecheck"
cp -r share/jefecheck/FX "$PREFIX/share/jefecheck/"
cp -r share/jefecheck/fonts "$PREFIX/share/jefecheck/"
for f in ffmpeg-NOTICE.txt ffmpeg-libs-VERSION.txt DEPENDENCIES; do
    if [ -f "$f" ]; then install -Dm644 "$f" "$PREFIX/share/jefecheck/$f"; fi
done
echo "JefeCheck installed to $PREFIX"
echo "Run: jefecheck"
