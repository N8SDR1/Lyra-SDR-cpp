#!/usr/bin/env bash
#
# make_dmg.sh — wrap a packaged build/lyra.app in a distributable .dmg.
#
# This runs AFTER tools/package_macos.sh.  That script is what makes the
# bundle self-contained; this one only puts the finished bundle in a disk
# image, so it re-asserts the two properties that matter before shipping
# rather than trusting that packaging was run:
#
#   * the signature verifies, and
#   * nothing in the bundle points at /opt/homebrew.
#
# A bundle that fails either one launches on this machine (Homebrew is here)
# and dies on the tester's.  That is the whole failure mode a DMG exists to
# expose, so both are checked again on the MOUNTED image — after the copy
# into the image and back out of it — not on the source tree.
#
# hdiutil only: no create-dmg, no new Homebrew dependency.
#
set -euo pipefail

APP="${APP:-build/lyra.app}"
OUT_DIR="${OUT_DIR:-dist}"
# Ad-hoc by default, matching package_macos.sh.  Set SIGN_ID to a Developer ID
# to sign the image; notarisation is out of scope here (it uploads to Apple).
SIGN_ID="${SIGN_ID:-}"

die()  { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
step() { printf '\n== %s\n' "$*"; }

[ -d "$APP" ] || die "no bundle at $APP — build it, then run tools/package_macos.sh"

VERSION="$(awk '/^[[:space:]]*VERSION[[:space:]]+[0-9]/{print $2; exit}' CMakeLists.txt)"
[ -n "$VERSION" ] || die "could not read VERSION from CMakeLists.txt"
ARCH="$(uname -m)"
VOLNAME="Lyra $VERSION"
DMG="${DMG:-$OUT_DIR/Lyra-$VERSION-$ARCH.dmg}"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

# ---------------------------------------------------------------------------
step "Source bundle: $APP ($VERSION, $ARCH, commit $COMMIT)"

codesign --verify --deep --strict "$APP" 2>/dev/null ||
    die "source bundle signature does not verify — re-run tools/package_macos.sh"

leaks="$(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 2>/dev/null |
         xargs -0 -r otool -L 2>/dev/null | grep -c '/opt/homebrew' || true)"
[ "$leaks" -eq 0 ] ||
    die "source bundle has $leaks Homebrew references — re-run tools/package_macos.sh"
echo "  signature OK, 0 Homebrew references, $(du -sh "$APP" | cut -f1)"

# ---------------------------------------------------------------------------
step "Staging image contents"
STAGE="$(mktemp -d -t lyra-dmg)"
# Anything that leaves the staging dir mounted or half-built must not survive
# a failure, or the next run picks up a stale /Volumes entry.
cleanup() {
    [ -n "${MOUNT_DEV:-}" ] && hdiutil detach "$MOUNT_DEV" -quiet 2>/dev/null || true
    rm -rf "$STAGE"
}
trap cleanup EXIT

# ditto, not cp: preserves the code signature, resource forks and xattrs.
# cp -R silently drops extended attributes and invalidates the signature.
ditto "$APP" "$STAGE/$(basename "$APP")"
ln -s /Applications "$STAGE/Applications"
echo "  $(basename "$APP") + Applications symlink"

# ---------------------------------------------------------------------------
step "Building $DMG"
mkdir -p "$OUT_DIR"
rm -f "$DMG"
# UDZO (zlib): mountable by every macOS that can run this app.  ULFO/lzfse
# compresses better but needs 10.11+, which buys nothing here.
hdiutil create \
    -volname "$VOLNAME" \
    -srcfolder "$STAGE" \
    -fs HFS+ \
    -format UDZO -imagekey zlib-level=9 \
    -ov -quiet \
    "$DMG"
[ -f "$DMG" ] || die "hdiutil produced no image"

if [ -n "$SIGN_ID" ]; then
    step "Signing image as $SIGN_ID"
    codesign --force --sign "$SIGN_ID" --timestamp "$DMG"
fi

# ---------------------------------------------------------------------------
step "Verifying the image"
fail=0

hdiutil verify "$DMG" >/dev/null 2>&1 ||
    { echo "  FAIL: checksum does not verify"; fail=1; }

# Mount and re-check the app AS THE TESTER WILL GET IT.  -nobrowse keeps it
# out of the Finder sidebar; -readonly matches how a downloaded DMG mounts.
MOUNT_DIR="$STAGE/mnt"
mkdir -p "$MOUNT_DIR"
MOUNT_DEV="$(hdiutil attach "$DMG" -readonly -nobrowse -noautoopen \
             -mountpoint "$MOUNT_DIR" 2>/dev/null |
             awk '/\/dev\/disk/{print $1; exit}')"
[ -n "$MOUNT_DEV" ] || die "could not mount $DMG"

MOUNTED_APP="$MOUNT_DIR/$(basename "$APP")"
[ -d "$MOUNTED_APP" ] || { echo "  FAIL: app missing inside image"; fail=1; }
[ -L "$MOUNT_DIR/Applications" ] || { echo "  FAIL: Applications symlink missing"; fail=1; }

codesign --verify --deep --strict "$MOUNTED_APP" 2>/dev/null ||
    { echo "  FAIL: signature does not verify inside the image"; fail=1; }

mleaks="$(find "$MOUNTED_APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 2>/dev/null |
          xargs -0 -r otool -L 2>/dev/null | grep -c '/opt/homebrew' || true)"
echo "  homebrew references inside image: $mleaks"
[ "$mleaks" -eq 0 ] || { echo "  FAIL: image references Homebrew"; fail=1; }

sig="$(codesign -dv --verbose=2 "$MOUNTED_APP" 2>&1)"
echo "  $(echo "$sig" | grep -E '^Identifier=' || true)"
echo "  $(echo "$sig" | grep -E 'Signature=|Authority=' | head -1 || true)"

hdiutil detach "$MOUNT_DEV" -quiet
MOUNT_DEV=""

[ "$fail" -eq 0 ] || die "image is NOT usable — see failures above"

# ---------------------------------------------------------------------------
printf '\nOK — %s (%s, from %s)\n' "$DMG" "$(du -h "$DMG" | cut -f1)" "$COMMIT"
if [ -z "$SIGN_ID" ]; then
    cat <<'NOTE'

Ad-hoc signed and NOT notarised.  On this Mac it opens normally.  On any
other Mac, Gatekeeper quarantines it on download — the tester must
right-click the app and choose Open the first time, or run:
  xattr -dr com.apple.quarantine /Applications/lyra.app
To avoid that, re-run both scripts with a Developer ID:
  SIGN_ID="Developer ID Application: ..." tools/package_macos.sh
  SIGN_ID="Developer ID Application: ..." tools/make_dmg.sh
(Notarisation is a further step and uploads the app to Apple.)
NOTE
fi
