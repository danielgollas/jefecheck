#!/usr/bin/env bash
# Adds "// SPDX-License-Identifier: GPL-2.0-or-later" as line 1 of every
# project-own C++ source. Vendored third-party code (RakNet, xmlParser,
# Rijndael, DS_*, GLAD, CLI11, third_party/) is never matched and keeps its
# own terms. --check lists files lacking the line and exits 1.
set -u
cd "$(git rev-parse --show-toplevel)"
LINE='// SPDX-License-Identifier: GPL-2.0-or-later'
files=$(git ls-files 'src/gfc*' 'src/qt/*' 'src/auth/*' 'src/ui/*' 'src/main_qt.cpp' 'src/macos_bundle_path.mm' 'src/trilerp.*' | grep -E '\.(cpp|h|mm)$')
missing=()
for f in $files; do
  [ -f "$f" ] || continue
  if ! head -n 5 "$f" | grep -q 'SPDX-License-Identifier'; then missing+=("$f"); fi
done
if [ "${1:-}" = "--check" ]; then
  if [ ${#missing[@]} -gt 0 ]; then printf '%s\n' "${missing[@]}"; exit 1; fi
  exit 0
fi
for f in "${missing[@]+"${missing[@]}"}"; do
  [ -n "$f" ] || continue
  if head -n 1 "$f" | grep -q '^#!'; then echo "skip (shebang): $f" >&2; continue; fi
  { printf '%s\n' "$LINE"; cat "$f"; } > "$f.spdx.tmp" && mv "$f.spdx.tmp" "$f"
  echo "$f"
done
