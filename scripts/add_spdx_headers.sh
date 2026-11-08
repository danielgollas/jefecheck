#!/usr/bin/env bash
# Usage: scripts/add_spdx_headers.sh [--check]   (--check lists files lacking the line, exits 1)
# Adds the GPL-2.0-or-later SPDX line to project-own C++ sources; vendored code keeps its own terms.
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
