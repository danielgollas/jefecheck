#!/bin/bash
set -e
PREFIX="${1:-/usr/local}"
echo "Installing JefeCheck to $PREFIX..."
install -Dm755 bin/jefecheck "$PREFIX/bin/jefecheck"
mkdir -p "$PREFIX/share/jefecheck"
cp -r share/jefecheck/FX "$PREFIX/share/jefecheck/"
cp -r share/jefecheck/fonts "$PREFIX/share/jefecheck/"
echo "JefeCheck installed to $PREFIX"
echo "Run: jefecheck"
