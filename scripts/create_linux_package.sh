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

tar czf "${PACKAGE}.tar.gz" "$PACKAGE"

echo "Created ${PACKAGE}.tar.gz"
