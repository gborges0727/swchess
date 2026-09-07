#!/bin/sh
# Signs Star Wars Chess.app and builds a disk image a player can download.
#
#   ./scripts/package-macos.sh <app> <output directory> [version]
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

app=${1:?usage: package-macos.sh <app> <output directory> [version]}
out_dir=${2:?usage: package-macos.sh <app> <output directory> [version]}
root=$(cd "$(dirname "$0")/.." && pwd)
entitlements="$root/packaging/entitlements.plist"
install_text="$root/packaging/Install.txt"
identity=${SWCHESS_SIGN_IDENTITY:--}

if [ ! -d "$app" ]; then
    echo "$app is not an application bundle" >&2
    exit 1
fi

version=${3:-}
if [ -z "$version" ]; then
    version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
        "$app/Contents/Info.plist")
fi

mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)

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

echo "== signing the code inside the bundle"
find "$app" -type f -print | sort | while read -r path; do
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
stage=$(mktemp -d /tmp/swchess-dmg.XXXXXX)
trap 'rm -rf "$stage"' EXIT INT TERM
ditto "$app" "$stage/$(basename "$app")"
ln -s /Applications "$stage/Applications"
cp "$install_text" "$stage/Install.txt"

dmg="$out_dir/StarWarsChess-$version.dmg"
rm -f "$dmg"
hdiutil create -volname 'Star Wars Chess' -srcfolder "$stage" \
    -fs HFS+ -format UDZO -ov "$dmg"

if [ "$identity" != "-" ]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi

if [ "${SWCHESS_MAKE_ZIP:-0}" = "1" ]; then
    zip="$out_dir/StarWarsChess-$version.zip"
    rm -f "$zip"
    ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
    shasum -a 256 "$zip" >"$zip.sha256"
    printf 'wrote %s\n' "$zip"
fi

shasum -a 256 "$dmg" >"$dmg.sha256"
printf 'wrote %s\n' "$dmg"
du -h "$dmg" | awk '{ print "size " $1 }'
