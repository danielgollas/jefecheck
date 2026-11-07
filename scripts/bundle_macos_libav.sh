#!/bin/bash
# Usage: bundle_macos_libav.sh <App.app>
# Bundles the linked FFmpeg libraries and their whole non-system dylib closure into Contents/Frameworks:
# dyld refuses to load libavcodec if any non-weak linked library (encoders, OpenSSL) is missing.
set -euo pipefail

APP="${1:?usage: bundle_macos_libav.sh <App.app>}"
BIN="$APP/Contents/MacOS/jefecheck"
FW="$APP/Contents/Frameworks"
RES="$APP/Contents/Resources"
[ -f "$BIN" ] || { echo "error: $BIN not found" >&2; exit 1; }
mkdir -p "$FW" "$RES"

is_system() { case "$1" in /usr/lib/*|/System/*) return 0 ;; *) return 1 ;; esac; }

# deps <file>: the dylibs a Mach-O file loads (its own id excluded).
deps() {
    local self
    self=$(otool -D "$1" | tail -n +2)
    otool -L "$1" | tail -n +2 | awk '{print $1}' | while read -r d; do
        [ "$d" = "$self" ] || echo "$d"
    done
}

# resolve <dep> <referencing file>: the real path of a dependency.
resolve() {
    local d="$1" from_dir
    from_dir=$(dirname "$2")
    case "$d" in
        @loader_path/*) d="$from_dir/${d#@loader_path/}" ;;
        @rpath/*)
            local n="${d#@rpath/}"
            for c in "$from_dir/$n" "/opt/homebrew/lib/$n" "/usr/local/lib/$n"; do
                [ -e "$c" ] && { d="$c"; break; }
            done ;;
    esac
    [ -e "$d" ] || { echo "error: cannot resolve $1 (from $2)" >&2; exit 1; }
    echo "$d"
}

# Seeds: the libav*/libsw* libraries the binary links directly.
QUEUE=()
for d in $(deps "$BIN"); do
    case "$(basename "$d")" in libav*|libsw*) QUEUE+=("$(resolve "$d" "$BIN")") ;; esac
done
[ ${#QUEUE[@]} -gt 0 ] || { echo "error: $BIN links no libav* libraries" >&2; exit 1; }
LIBAVCODEC_SRC=""
for s in "${QUEUE[@]}"; do case "$(basename "$s")" in libavcodec*) LIBAVCODEC_SRC="$s" ;; esac; done

# Copy the closure, keyed by install-name basename.
BUNDLED=" "
i=0
while [ $i -lt ${#QUEUE[@]} ]; do
    src="${QUEUE[$i]}"; i=$((i + 1))
    name=$(basename "$src")
    case "$BUNDLED" in *" $name "*) continue ;; esac
    BUNDLED="$BUNDLED$name "
    cp -L "$src" "$FW/$name"
    chmod u+w "$FW/$name"
    for d in $(deps "$src"); do
        is_system "$d" && continue
        QUEUE+=("$(resolve "$d" "$src")")
    done
done

# Rewrite every bundled library: own id, references to other non-system libs,
# and drop Homebrew-relative rpaths (e.g. @loader_path/../lib).
for name in $BUNDLED; do
    f="$FW/$name"
    args=(-id "@rpath/$name")
    for d in $(deps "$f"); do
        is_system "$d" && continue
        args+=(-change "$d" "@rpath/$(basename "$d")")
    done
    install_name_tool "${args[@]}" "$f" 2>/dev/null
    for rp in $(otool -l "$f" | awk '/LC_RPATH/{getline; getline; print $2}'); do
        install_name_tool -delete_rpath "$rp" "$f"
    done
done

# Point the binary at the bundled copies.
args=()
for d in $(deps "$BIN"); do
    case "$(basename "$d")" in libav*|libsw*) args+=(-change "$d" "@rpath/$(basename "$d")") ;; esac
done
install_name_tool "${args[@]}" "$BIN" 2>/dev/null
# @executable_path/../Frameworks goes first so @rpath resolves inside the bundle even where a
# build-tree rpath (/opt/homebrew/lib) also has the library; existing rpaths follow in order.
OLD_RPATHS=$(otool -l "$BIN" | awk '/LC_RPATH/{getline; getline; print $2}')
for rp in $OLD_RPATHS; do install_name_tool -delete_rpath "$rp" "$BIN"; done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN"
for rp in $OLD_RPATHS; do
    [ "$rp" = "@executable_path/../Frameworks" ] || install_name_tool -add_rpath "$rp" "$BIN"
done

# Record the shipped FFmpeg version and configure line. The ffmpeg program of
# the package that provided libavcodec prints both (same build, same configure).
VER="$RES/ffmpeg-libs-VERSION.txt"
{
    echo "FFmpeg libraries bundled in Contents/Frameworks/"
    echo "Source: $(dirname "$LIBAVCODEC_SRC")"
    echo
    FFMPEG_CLI="$(dirname "$LIBAVCODEC_SRC")/../bin/ffmpeg"
    if [ -x "$FFMPEG_CLI" ]; then
        "$FFMPEG_CLI" -hide_banner -version | grep -v "^Exiting with exit code"
    else
        echo "(no ffmpeg program next to the libraries; versions from the dylibs)"
    fi
    echo
    echo "Bundled files (install name, current version):"
    for name in $BUNDLED; do
        echo "  $name $(otool -L "$FW/$name" | sed -n 2p | sed 's/.*current version \([^)]*\)).*/\1/')"
    done
} > "$VER"

# install_name_tool invalidates the signatures; re-sign ad hoc or the app will
# not launch on Apple Silicon.
for name in $BUNDLED; do codesign --force -s - "$FW/$name"; done
codesign --force -s - "$BIN"

echo "Bundled $(echo $BUNDLED | wc -w | tr -d ' ') dylibs (libav* and their dependencies) into $FW"
