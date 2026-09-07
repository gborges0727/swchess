#!/bin/sh
# Proves that Star Wars Chess builds and runs from a fresh checkout.
#
#   ./scripts/check-fresh-checkout.sh
#
# It clones this repository's HEAD into a temporary directory, installs the
# Brewfile, builds the application bundle there, runs the test suite, plays
# two moves headless to write a picture, and checks that the game finds its
# CD folder the way a player's copy does. It exits non-zero on the first
# failure.
#
# The clone holds no original CD files and no extracted artwork, because
# .gitignore keeps both out of the repository. The build and the tests read
# those from this working copy instead, which is what a person with their own
# CD would point the game at.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd_dir="$root/original/win3x/cd"
assets_dir="$root/assets"

if [ ! -d "$cd_dir" ]; then
    echo "the original CD files are missing from $cd_dir" >&2
    exit 1
fi

work=$(mktemp -d /tmp/swchess-fresh.XXXXXX)
clone="$work/swchess"
trap 'rm -rf "$work"' EXIT INT TERM

echo "== cloning $root into $clone"
git clone --quiet --local "$root" "$clone"

# The clone holds the last commit. Copy the uncommitted work on top of it so
# the check builds what the working copy actually says, packaging files
# included. Ignored paths such as original/, assets/ and build/ stay out,
# which is the point of the exercise.
if ! git -C "$root" diff --quiet HEAD; then
    echo "== applying uncommitted changes to tracked files"
    git -C "$root" diff HEAD | git -C "$clone" apply
fi
git -C "$root" ls-files --others --exclude-standard | while read -r path; do
    mkdir -p "$clone/$(dirname "$path")"
    cp -p "$root/$path" "$clone/$path"
done

echo "== installing the Brewfile"
(cd "$clone" && brew bundle --file=Brewfile)

echo "== building the application bundle"
SWCHESS_CMAKE_ARGS="-DSWCHESS_CD_DIR=$cd_dir -DSWCHESS_ASSETS_DIR=$assets_dir" \
    "$clone/scripts/build-app.sh"

app="$clone/build/Star Wars Chess.app"

echo "== checking that nothing points into Homebrew"
leaks=$(otool -L "$app/Contents/MacOS/Star Wars Chess" | grep -c homebrew || true)
if [ "$leaks" != "0" ]; then
    otool -L "$app/Contents/MacOS/Star Wars Chess" >&2
    echo "the bundled binary still loads $leaks libraries from Homebrew" >&2
    exit 1
fi

echo "== building and running the tests"
cmake --build "$clone/build"
ctest --test-dir "$clone/build" --output-on-failure

echo "== playing two moves headless"
picture="$work/out.ppm"
"$app/Contents/MacOS/Star Wars Chess" --cd "$cd_dir" \
    --script "e2e4 e7e5" --dump-at 500 "$picture"
if [ ! -s "$picture" ]; then
    echo "the headless run wrote no picture at $picture" >&2
    exit 1
fi
head -c 2 "$picture" | grep -q P6 || {
    echo "$picture is not a binary PPM" >&2
    exit 1
}

# The game finds the CD folder itself now. These two checks run it against a
# home directory of their own, so whatever this machine's owner has already
# saved cannot answer for them.
home="$work/home"
mkdir -p "$home/Library/Application Support/Star Wars Chess"

echo "== refusing a headless run that names no CD folder"
if env -u SWCHESS_CD -u SWCHESS_ASSETS HOME="$home" \
        "$app/Contents/MacOS/Star Wars Chess" \
        --script "e2e4" --dump-at 100 "$work/none.ppm" 2>"$work/none.err"; then
    echo "the game started with no CD folder set" >&2
    exit 1
fi
grep -q -- "--cd" "$work/none.err" || {
    cat "$work/none.err" >&2
    echo "the refusal does not tell the player to pass --cd" >&2
    exit 1
}

echo "== reading the CD folder from the saved configuration"
printf '%s\n%s\n%s\n%s\n' \
    "# Star Wars Chess startup settings. The game writes this file." \
    "version=1" \
    "cd=$cd_dir" \
    "assets=$assets_dir" \
    > "$home/Library/Application Support/Star Wars Chess/startup.conf"
saved="$work/saved.ppm"
env -u SWCHESS_CD -u SWCHESS_ASSETS HOME="$home" \
    "$app/Contents/MacOS/Star Wars Chess" --script "e2e4" --dump-at 100 "$saved"
if [ ! -s "$saved" ]; then
    echo "the saved configuration did not start the game" >&2
    exit 1
fi

echo "== the fresh checkout builds, tests and runs"
