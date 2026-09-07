#!/bin/sh
# Builds Star Wars Chess.app and audits every binary inside it.
#
#   ./scripts/build-app.sh [build directory] [architectures] [minimum macOS]
#
# The defaults are build, arm64;x86_64 and 11.0. Separate architectures with
# a semicolon, the way CMake lists them. Two architectures need the vendored
# SDL3, which the build gets from -DSWCHESS_VENDOR_DEPS=ON. An installed
# Homebrew SDL3 is Apple silicon only, so ask for arm64 alone on that path.
#
# Set SWCHESS_CMAKE_ARGS to pass extra options to the configure step. The
# fresh checkout test uses it to point the build at another repository's CD
# files and asset cache.
#
# The audit fails the build when a binary is missing an architecture, loads a
# library from Homebrew or from the build directory, or was compiled for a
# newer macOS than the one asked for.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$root/build}
archs=${2:-arm64;x86_64}
min_macos=${3:-11.0}

case $build in
    /*) ;;
    *) build="$root/$build" ;;
esac
app="$build/Star Wars Chess.app"

# shellcheck disable=SC2086
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_OSX_ARCHITECTURES=$archs" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$min_macos" \
    -DSWCHESS_VENDOR_DEPS=ON \
    ${SWCHESS_CMAKE_ARGS:-}

cmake --build "$build" --target StarWarsChess

if [ ! -d "$app" ]; then
    echo "the build finished but $app is missing" >&2
    exit 1
fi

# Every Mach-O file in the bundle goes through three checks. Set
# SWCHESS_AUDIT to warn to print problems without failing, which the
# developer build needs while it still links the Homebrew SDL3.
audit=${SWCHESS_AUDIT:-strict}
problems=$(mktemp /tmp/swchess-audit.XXXXXX)
trap 'rm -f "$problems"' EXIT INT TERM

echo "== auditing the binaries in the bundle"
find "$app" -type f -print | sort | while read -r path; do
    file -b "$path" | grep -q 'Mach-O' || continue
    name=${path#"$app/"}

    have=$(lipo -archs "$path")
    for want in $(printf '%s' "$archs" | tr ';' ' '); do
        case " $have " in
            *" $want "*) ;;
            *) printf '%s was not built for %s\n' "$name" "$want" >>"$problems" ;;
        esac
    done

    # Only the indented lines name a library. The others name the file
    # itself and its architectures.
    bad=$(otool -L "$path" | grep '^	' | awk '{ print $1 }' |
        grep -E "^(/opt/homebrew|/usr/local)|^$build" || true)
    if [ -n "$bad" ]; then
        printf '%s loads %s from outside the bundle\n' "$name" \
            "$(printf '%s' "$bad" | tr '\n' ' ')" >>"$problems"
    fi

    minos=$(vtool -show-build "$path" 2>/dev/null |
        awk '$1 == "minos" { print $2 }' | sort -u | tr '\n' ' ')
    for value in $minos; do
        if [ "$value" != "$min_macos" ]; then
            printf '%s asks for macOS %s, not %s\n' "$name" "$value" "$min_macos" \
                >>"$problems"
        fi
    done

    printf '   %s: %s, macOS %s\n' "$name" "$have" "$minos"
done

if [ -s "$problems" ]; then
    echo "== the audit found problems" >&2
    cat "$problems" >&2
    if [ "$audit" = "strict" ]; then
        echo "set SWCHESS_AUDIT=warn to build anyway" >&2
        exit 1
    fi
fi

printf 'built %s\n' "$app"
du -sh "$app" | awk '{ print "size " $1 }'
