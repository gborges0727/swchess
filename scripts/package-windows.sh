#!/bin/sh
# Builds the Windows zip a player unzips and runs.
#
#   ./scripts/package-windows.sh <build directory> <output directory> <version> \
#       [--bundle-data <CD folder> <assets folder>]
#
# The build directory is the one cmake --preset windows-mingw-release wrote,
# and it has to hold swchess.exe and swchess-viewer.exe. The script renames
# swchess.exe to "Star Wars Chess.exe" and puts packaging/windows/README.txt
# beside it.
#
# --bundle-data makes the private full build. It copies the CD files into cd
# and the decoded artwork into assets, both beside the exe. src/app/startup.cpp
# finds those two folders next to the program and then reads nothing else, so
# the player is never asked to pick a folder. That zip holds copyrighted CD
# files, so keep it off the internet. Its name ends in -full.zip.
set -eu

usage='usage: package-windows.sh <build dir> <out dir> <version> [--bundle-data <cd> <assets>]'
build_dir=${1:?$usage}
out_dir=${2:?$usage}
version=${3:?$usage}
shift 3
bundle_cd=""
bundle_assets=""
while [ $# -gt 0 ]; do
    case $1 in
        --bundle-data)
            bundle_cd=${2:?--bundle-data needs a CD folder and an assets folder}
            bundle_assets=${3:?--bundle-data needs a CD folder and an assets folder}
            shift 3
            ;;
        *)
            echo "unknown argument $1" >&2
            echo "$usage" >&2
            exit 1
            ;;
    esac
done

root=$(cd "$(dirname "$0")/.." && pwd)
readme="$root/packaging/windows/README.txt"

for exe in swchess.exe swchess-viewer.exe; do
    if [ ! -f "$build_dir/$exe" ]; then
        echo "$build_dir/$exe is missing. Build it first with:" >&2
        echo "  cmake --preset windows-mingw-release -B $build_dir" >&2
        echo "  cmake --build $build_dir --target swchess swchess-viewer" >&2
        exit 1
    fi
done

if [ -n "$bundle_cd" ]; then
    if [ ! -d "$bundle_cd" ]; then
        echo "$bundle_cd is not a folder" >&2
        exit 1
    fi
    if [ ! -d "$bundle_assets" ]; then
        echo "$bundle_assets is not a folder" >&2
        exit 1
    fi
    bundle_cd=$(cd "$bundle_cd" && pwd)
    bundle_assets=$(cd "$bundle_assets" && pwd)
fi

build_dir=$(cd "$build_dir" && pwd)
mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)

name="StarWarsChess-$version-win64"
stage=$(mktemp -d /tmp/swchess-win.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM
folder="$stage/$name"
mkdir -p "$folder"

# Copies a directory tree. It asks the filesystem to clone the files first,
# which costs no disk space and no time on APFS. The assets tree holds about
# 40,000 files, so a plain copy of it takes minutes.
copy_tree() {
    if ! cp -Rc "$1" "$2" 2>/dev/null; then
        rm -rf "$2"
        cp -R "$1" "$2"
    fi
}

echo "== staging $name"
cp "$build_dir/swchess.exe" "$folder/Star Wars Chess.exe"
cp "$build_dir/swchess-viewer.exe" "$folder/swchess-viewer.exe"
cp "$readme" "$folder/README.txt"

if [ -n "$bundle_cd" ]; then
    echo "== copying the CD files and the artwork beside the exe"
    copy_tree "$bundle_cd" "$folder/cd"
    copy_tree "$bundle_assets" "$folder/assets"
    du -sh "$folder" | awk '{ print "the folder now holds " $1 }'
    zip="$out_dir/$name-full.zip"
else
    zip="$out_dir/$name.zip"
fi

echo "== writing the zip"
rm -f "$zip"
if command -v ditto >/dev/null 2>&1; then
    ditto -c -k --keepParent "$folder" "$zip"
else
    (cd "$stage" && zip -q -r "$zip" "$name")
fi

shasum -a 256 "$zip" >"$zip.sha256"
printf 'wrote %s\n' "$zip"
du -h "$zip" | awk '{ print "size " $1 }'

if [ -n "$bundle_cd" ]; then
    echo "This zip holds the files from the Star Wars Chess CD."
    echo "Keep it on your own machine and do not share it."
fi
