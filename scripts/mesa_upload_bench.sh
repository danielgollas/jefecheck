#!/usr/bin/env bash
# Runs the upload bench (--upload-bench) on Mesa's llvmpipe software
# renderer under Xvfb, once per staging level. llvmpipe has
# ARB_buffer_storage, so this checks the persistent-mapped staging path
# that macOS's GL 2.1 can't reach. It proves correctness, not speed:
# llvmpipe renders on the CPU, so the timings mean nothing.
#
# Usage: scripts/mesa_upload_bench.sh <path/to/jefecheck>
# Needs: xvfb (xvfb-run), Mesa (libgl1-mesa-dri), oiiotool.
set -euo pipefail

bin="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Twelve identical UHD frames whose values reach 4.0, so the bench can check
# that half-float keeps highlights and that the last (pooled) frame matches
# the first. Red reaches 4.0 across, blue only 1.0 down, so their means
# differ and a swapped channel order shows.
mkdir -p "$work/frames"
oiiotool --pattern fill:topleft=0,0,0:topright=4,0.5,0:bottomleft=0,0.5,1:bottomright=4,4,1 3840x2160 3 \
    -d half -o "$work/frames/bench.0001.exr"
for i in $(seq -w 2 12); do cp "$work/frames/bench.0001.exr" "$work/frames/bench.00$i.exr"; done

# A fresh config would open the Load Window at startup; keep it closed.
export HOME="$work/home"
mkdir -p "$HOME/.config/JefeCheck"
printf '[%%General]\nopenLoadWindowAtStartup=0\n' > "$HOME/.config/JefeCheck/JefeCheck.conf"

export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export QT_QPA_PLATFORM=xcb

status=0
run() {
    local label="$1" expect="$2"; shift 2
    echo "=== $label ==="
    local log="$work/$label.log"
    env "$@" xvfb-run -a -s "-screen 0 1920x1080x24" "$bin" --upload-bench "$work/frames" 2>&1 | tee "$log" || true
    if ! grep -q "UPLOAD-BENCH: PASS" "$log"; then
        echo "::error::$label: bench did not pass"; status=1
    fi
    if ! grep -q "gfcUpload: staging buffers $expect" "$log"; then
        echo "::error::$label: expected staging level '$expect'"; status=1
    fi
}

run persistent persistent
run classic classic JEFECHECK_FORCE_CLASSIC_PBO=1
run no-pools off JEFECHECK_NO_PBO=1 JEFECHECK_NO_TEXTURE_POOL=1
exit $status
