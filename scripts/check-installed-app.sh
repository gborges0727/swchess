#!/bin/sh
# Checks a downloaded Star Wars Chess the way a player receives it.
#
#   ./scripts/check-installed-app.sh <app or dmg> --cd <CD folder>
#
# It mounts the disk image when you give it one, copies the app to a
# temporary folder, and works only from that copy. The copy stands in for
# /Applications. The checks are:
#
#   1. codesign accepts the signature.
#   2. spctl says what Gatekeeper would do. An ad-hoc signature fails that
#      check on purpose, and the script says so instead of stopping.
#   3. No binary loads a library from Homebrew, /usr/local or a build
#      directory.
#   4. Every file in the bundle appears in packaging/bundle-allowlist.txt.
#   5. The game starts from the copy and draws a picture, with SWCHESS_REPO
#      unset and the working directory set to /, so it cannot reach the
#      source tree.
set -eu

target=${1:?usage: check-installed-app.sh <app or dmg> --cd <CD folder>}
shift
cd_dir=""
while [ $# -gt 0 ]; do
    case $1 in
        --cd)
            cd_dir=${2:?--cd needs a folder}
            shift 2
            ;;
        *)
            echo "unknown argument $1" >&2
            exit 1
            ;;
    esac
done

if [ -z "$cd_dir" ] || [ ! -d "$cd_dir" ]; then
    echo "give --cd the folder holding your copy of the CD" >&2
    exit 1
fi
cd_dir=$(cd "$cd_dir" && pwd)

root=$(cd "$(dirname "$0")/.." && pwd)
allowlist="$root/packaging/bundle-allowlist.txt"
work=$(mktemp -d /tmp/swchess-installed.XXXXXX)
mount_point=""
cleanup() {
    if [ -n "$mount_point" ]; then
        hdiutil detach "$mount_point" -quiet || true
        rmdir "$mount_point" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

case $target in
    *.dmg)
        echo "== mounting $target"
        mount_point=$(mktemp -d /tmp/swchess-mount.XXXXXX)
        hdiutil attach "$target" -nobrowse -readonly -mountpoint "$mount_point" >/dev/null
        source_app=$(find "$mount_point" -maxdepth 1 -name '*.app' | head -n 1)
        if [ -z "$source_app" ]; then
            echo "the disk image holds no application" >&2
            exit 1
        fi
        if [ ! -f "$mount_point/Install.txt" ]; then
            echo "the disk image has no Install.txt" >&2
            exit 1
        fi
        if [ ! -L "$mount_point/Applications" ]; then
            echo "the disk image has no Applications shortcut" >&2
            exit 1
        fi
        ;;
    *)
        source_app=$target
        ;;
esac

echo "== copying the app to $work"
ditto "$source_app" "$work/$(basename "$source_app")"
app="$work/$(basename "$source_app")"

echo "== checking the signature"
codesign --verify --deep --strict --verbose=2 "$app"
signature=$(codesign --display --verbose=2 "$app" 2>&1 | awk -F= '$1 == "Signature" { print $2 }')

echo "== asking Gatekeeper about the app"
if spctl -a -vv "$app"; then
    echo "Gatekeeper accepts the app"
elif [ "$signature" = "adhoc" ]; then
    echo "Gatekeeper rejects the ad-hoc signature, which is expected here"
    echo "a release needs a Developer ID certificate and a notarization ticket"
else
    echo "Gatekeeper rejects the app and its signature is not ad-hoc" >&2
    exit 1
fi

echo "== checking what the binaries load"
problems=$(mktemp "$work/problems.XXXXXX")
find "$app" -type f -print | sort | while read -r path; do
    file -b "$path" | grep -q 'Mach-O' || continue
    # Only the indented lines name a library. The others name the file
    # itself and its architectures.
    bad=$(otool -L "$path" | grep '^	' | awk '{ print $1 }' |
        grep -E '^(/opt/homebrew|/usr/local|.*/build)' || true)
    if [ -n "$bad" ]; then
        printf '%s loads %s\n' "${path#"$app/"}" \
            "$(printf '%s' "$bad" | tr '\n' ' ')" >>"$problems"
    fi
done
if [ -s "$problems" ]; then
    cat "$problems" >&2
    echo "those libraries are not in the bundle, so the app needs the developer's Mac" >&2
    exit 1
fi

echo "== checking the bundle against the allowed file list"
allowed=$(mktemp "$work/allowed.XXXXXX")
grep -v '^#' "$allowlist" | grep -v '^$' | sort >"$allowed"
present=$(mktemp "$work/present.XXXXXX")
(cd "$app" && find . \( -type f -o -type l \) -print | sed 's|^\./||' | sort) >"$present"
extra=$(comm -23 "$present" "$allowed" || true)
if [ -n "$extra" ]; then
    echo "the bundle holds files that packaging/bundle-allowlist.txt does not name:" >&2
    printf '%s\n' "$extra" >&2
    exit 1
fi

echo "== playing the game from the copy"
picture="$work/out.ppm"
(
    cd /
    env -u SWCHESS_REPO -u SWCHESS_CD -u SWCHESS_ASSETS \
        "$app/Contents/MacOS/Star Wars Chess" \
        --cd "$cd_dir" --skip-title --dump-at 500 "$picture"
)
if [ ! -s "$picture" ]; then
    echo "the game wrote no picture at $picture" >&2
    exit 1
fi
head -c 2 "$picture" | grep -q P6 || {
    echo "$picture is not a binary PPM" >&2
    exit 1
}

echo "== the installed app is signed, self-contained and it runs"
