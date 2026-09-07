#!/bin/sh
# Sends a disk image to Apple for notarization and staples the result.
#
#   ./scripts/notarize-macos.sh <dmg>
#
# Set SWCHESS_NOTARY_PROFILE to the name of a keychain profile that holds the
# Apple ID credentials. Without it the script prints the one command that
# creates that profile and stops. It never submits anything unsigned, because
# Apple rejects a disk image that is not signed with a Developer ID
# Application certificate.
#
# The submission log lands beside the disk image as <name>-notary.json.
set -eu

dmg=${1:?usage: notarize-macos.sh <dmg>}
team_id=${SWCHESS_TEAM_ID:-E85W63H34G}
profile=${SWCHESS_NOTARY_PROFILE:-}

if [ ! -f "$dmg" ]; then
    echo "$dmg is missing" >&2
    exit 1
fi

if [ -z "$profile" ]; then
    cat <<MESSAGE
SWCHESS_NOTARY_PROFILE is not set, so this run notarizes nothing.

Create the profile once. Apple asks for an app-specific password, which you
make at appleid.apple.com under Sign-In and Security.

  xcrun notarytool store-credentials 'swchess-notary' \\
      --apple-id 'YOUR-APPLE-ID@example.com' \\
      --team-id $team_id \\
      --password 'abcd-efgh-ijkl-mnop'

Then run this script again with SWCHESS_NOTARY_PROFILE=swchess-notary.
MESSAGE
    exit 0
fi

log_json="${dmg%.dmg}-notary.json"

echo "== submitting $dmg to Apple"
submit_json=$(mktemp /tmp/swchess-notary.XXXXXX)
xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait \
    --output-format json | tee "$submit_json"

submission=$(/usr/bin/python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["id"])' "$submit_json")
status=$(/usr/bin/python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["status"])' "$submit_json")
printf 'submission %s finished as %s\n' "$submission" "$status"

xcrun notarytool log "$submission" --keychain-profile "$profile" "$log_json" || true
printf 'the log is in %s\n' "$log_json"
rm -f "$submit_json"

if [ "$status" != "Accepted" ]; then
    echo "Apple did not accept the submission, so nothing was stapled" >&2
    echo "read $log_json, fix what it names, rebuild and submit again" >&2
    exit 1
fi

echo "== stapling the ticket to the disk image"
xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"

echo "== checking what Gatekeeper says"
spctl -a -vv -t install "$dmg"

mount_point=$(mktemp -d /tmp/swchess-mount.XXXXXX)
hdiutil attach "$dmg" -nobrowse -readonly -mountpoint "$mount_point" >/dev/null
trap 'hdiutil detach "$mount_point" -quiet || true; rmdir "$mount_point" 2>/dev/null || true' \
    EXIT INT TERM
spctl -a -vv "$mount_point/Star Wars Chess.app"

shasum -a 256 "$dmg" >"$dmg.sha256"
printf 'notarized %s\n' "$dmg"
