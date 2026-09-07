#!/bin/sh
# Signs Star Wars Chess.app and builds a disk image a player can download.
#
#   ./scripts/package-macos.sh <app> <output directory> [version] \
#       [--bundle-data <CD folder> <assets folder>]
#
# --bundle-data makes the private full build. It copies the CD files into
# Contents/Resources/cd and the decoded artwork into Contents/Resources/assets
# before it signs the app, so the app finds its own data and asks the owner
# nothing. That disk image holds copyrighted CD files, so keep it off the
# internet. Its name ends in -full.dmg.
#
# Set SWCHESS_SIGN_IDENTITY to the name of the signing certificate, as
# security find-identity -v -p codesigning prints it. Without it the script
# signs ad-hoc, which proves the signing steps work but does not make a
# release. A release needs a Developer ID Application certificate.
#
# Set SWCHESS_MAKE_ZIP=1 to write a zip beside the disk image.
#
# The version defaults to CFBundleShortVersionString from the app's
# Info.plist. The script signs the helper programs and the libraries first
# and the app itself last, because a signature covers everything under it.
set -eu

usage='usage: package-macos.sh <app> <output directory> [version] [--bundle-data <cd> <assets>]'
app=${1:?$usage}
out_dir=${2:?$usage}
shift 2
version=""
bundle_cd=""
bundle_assets=""
while [ $# -gt 0 ]; do
    case $1 in
        --bundle-data)
            bundle_cd=${2:?--bundle-data needs a CD folder and an assets folder}
            bundle_assets=${3:?--bundle-data needs a CD folder and an assets folder}
            shift 3
            ;;
        -*)
            echo "unknown argument $1" >&2
            echo "$usage" >&2
            exit 1
            ;;
        *)
            version=$1
            shift
            ;;
    esac
done
root=$(cd "$(dirname "$0")/.." && pwd)
entitlements="$root/packaging/entitlements.plist"
install_text="$root/packaging/Install.txt"
identity=${SWCHESS_SIGN_IDENTITY:--}

if [ ! -d "$app" ]; then
    echo "$app is not an application bundle" >&2
    exit 1
fi

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

if [ -z "$version" ]; then
    version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
        "$app/Contents/Info.plist")
fi

mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)

stage=$(mktemp -d /tmp/swchess-dmg.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM
staged=0

# Copies a directory tree. It asks the filesystem to clone the files first,
# which costs no disk space and no time on APFS. The assets tree holds about
# 40,000 files, so a plain copy of it takes minutes.
copy_tree() {
    if ! cp -Rc "$1" "$2" 2>/dev/null; then
        rm -rf "$2"
        ditto "$1" "$2"
    fi
}

# The data goes in before codesign runs, because the signature seals every
# file under Contents. Signing first and copying afterwards breaks the app.
if [ -n "$bundle_cd" ]; then
    echo "== copying the CD files and the artwork into the app"
    ditto "$app" "$stage/$(basename "$app")"
    app="$stage/$(basename "$app")"
    staged=1
    mkdir -p "$app/Contents/Resources"
    copy_tree "$bundle_cd" "$app/Contents/Resources/cd"
    copy_tree "$bundle_assets" "$app/Contents/Resources/assets"
    du -sh "$app" | awk '{ print "the app now holds " $1 }'
fi

if [ "$identity" = "-" ]; then
    echo "== signing ad-hoc, which no other Mac will trust"
else
    echo "== signing with $identity"
fi

# The hardened runtime and a timestamp are both required for notarization.
# An ad-hoc signature cannot carry a timestamp, so ask for one only when
# there is a certificate.
sign() {
    if [ "$identity" = "-" ]; then
        codesign --force --options runtime --entitlements "$entitlements" \
            --sign - "$1"
    else
        codesign --force --options runtime --timestamp \
            --entitlements "$entitlements" --sign "$identity" "$1"
    fi
}

# The bundled CD files and artwork are data, not code, so the search skips
# them. Reading 40,000 files with the file command takes minutes.
echo "== signing the code inside the bundle"
find "$app" -type f \
    ! -path "$app/Contents/Resources/cd/*" \
    ! -path "$app/Contents/Resources/assets/*" -print | sort | while read -r path; do
    file -b "$path" | grep -q 'Mach-O' || continue
    case $path in
        "$app/Contents/MacOS/"*) ;;
        *)
            # A library is code too, but it takes no entitlements.
            if [ "$identity" = "-" ]; then
                codesign --force --options runtime --sign - "$path"
            else
                codesign --force --options runtime --timestamp \
                    --sign "$identity" "$path"
            fi
            continue
            ;;
    esac
    sign "$path"
done

echo "== signing the application"
sign "$app"
codesign --verify --deep --strict --verbose=2 "$app"
codesign --display --verbose=2 "$app"

echo "== staging the disk image"
if [ "$staged" = "0" ]; then
    ditto "$app" "$stage/$(basename "$app")"
fi
ln -s /Applications "$stage/Applications"
cp "$install_text" "$stage/Install.txt"

if [ -n "$bundle_cd" ]; then
    dmg="$out_dir/StarWarsChess-$version-full.dmg"
else
    dmg="$out_dir/StarWarsChess-$version.dmg"
fi
rm -f "$dmg"
hdiutil create -volname 'Star Wars Chess' -srcfolder "$stage" \
    -fs HFS+ -format UDZO -ov "$dmg"

if [ "$identity" != "-" ]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi

if [ "${SWCHESS_MAKE_ZIP:-0}" = "1" ]; then
    zip="${dmg%.dmg}.zip"
    rm -f "$zip"
    ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
    shasum -a 256 "$zip" >"$zip.sha256"
    printf 'wrote %s\n' "$zip"
fi

shasum -a 256 "$dmg" >"$dmg.sha256"
printf 'wrote %s\n' "$dmg"
du -h "$dmg" | awk '{ print "size " $1 }'

if [ -n "$bundle_cd" ]; then
    echo "This disk image holds the files from the Star Wars Chess CD."
    echo "Keep it on your own Mac and do not share it."
fi
