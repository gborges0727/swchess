#!/bin/sh
# Builds Star Wars Chess.app for Apple silicon and reports how big it is.
#
#   ./scripts/build-app.sh
#
# Set SWCHESS_CMAKE_ARGS to pass extra options to the configure step. The
# fresh checkout test uses it to point the build at another repository's CD
# files and asset cache.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/build"
app="$build/Star Wars Chess.app"

# shellcheck disable=SC2086
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    ${SWCHESS_CMAKE_ARGS:-}

cmake --build "$build" --target StarWarsChess

if [ ! -d "$app" ]; then
    echo "the build finished but $app is missing" >&2
    exit 1
fi

printf 'built %s\n' "$app"
du -sh "$app" | awk '{ print "size " $1 }'
