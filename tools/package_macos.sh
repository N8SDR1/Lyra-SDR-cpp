#!/usr/bin/env bash
#
# package_macos.sh — turn build/lyra.app into a self-contained macOS bundle.
#
# CMake links Lyra against the Homebrew Qt, so a freshly built lyra.app only
# runs on a machine that has that exact Qt installed.  This stages Qt, the
# QML modules, and the FFTW the WDSP engine needs into the bundle, then
# re-signs it.
#
# The recipe follows three working references rather than being invented here:
#
#   * deskHPSDR.app (DL1BZ, shipping/notarized) — the target END STATE:
#     exactly one LC_RPATH, "@executable_path/../Frameworks/", and zero
#     absolute Homebrew references anywhere in the bundle.  Both are asserted
#     below.
#   * AetherSDR (.github/workflows/macos-dmg.yml) — macdeployqt must come from
#     the SAME Qt the app linked or it stages a different build than the
#     binary resolves against; -always-overwrite so a half-staged bundle from
#     an earlier run is replaced rather than merged with; and sign frameworks
#     individually instead of `codesign --deep`, which Apple deprecates.
#   * Zeus / station-engine (NATIVE-BUILD.md) — relocate the WDSP engine's
#     FFTW with @loader_path and the dylibs sitting BESIDE libwdsp.dylib.
#     @loader_path (not @executable_path) is what makes this correct for a
#     library Lyra dlopen()s at runtime: it resolves relative to the dylib
#     doing the loading, so it holds wherever the bundle is moved to.
#     deskHPSDR avoids this problem entirely by static-linking its DSP; Lyra
#     loads its engine at runtime and cannot.
#
# Every step that can silently half-succeed is followed by an assertion.  The
# failure mode this script exists to prevent — duplicate Qt — produces a
# bundle that looks fine to `ls` and dies on launch.
#
set -euo pipefail

APP="${1:-build/lyra.app}"
QT_PREFIX="${QT_PREFIX:-/opt/homebrew/opt/qt}"
FFTW_PREFIX="${FFTW_PREFIX:-/opt/homebrew/opt/fftw}"
BUNDLE_ID="com.n8sdr1.lyra"
# Ad-hoc by default.  Set SIGN_ID to a Developer ID to produce something
# distributable; notarisation is out of scope here.
SIGN_ID="${SIGN_ID:--}"

die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
step() { printf '\n== %s\n' "$*"; }

[ -d "$APP" ] || die "no bundle at $APP — build it first (cmake --build build)"
DEPLOYQT="$QT_PREFIX/bin/macdeployqt"
[ -x "$DEPLOYQT" ] || die "macdeployqt not found at $DEPLOYQT"

EXE="$APP/Contents/MacOS/lyra"
FW="$APP/Contents/Frameworks"
NATIVE="$APP/Contents/MacOS/_native"

# ---------------------------------------------------------------------------
step "Qt: staging frameworks and QML modules"
# -no-codesign: macdeployqt's ad-hoc pass would be invalidated by the
# install_name_tool edits below anyway; we sign once, at the end.
#
# macdeployqt scans the whole QML import tree and reports "Cannot resolve
# rpath" for modules Lyra does not use and Homebrew does not ship (Qt3D,
# VirtualKeyboard, Pdf, StateMachine, Timeline).  Those are noise; the
# assertions below are what decide whether the bundle is good.
"$DEPLOYQT" "$APP" -qmldir=src/qml -always-overwrite -no-codesign \
    -libpath="$QT_PREFIX/lib" -libpath=/opt/homebrew/lib 2>&1 |
    grep -vE 'Cannot resolve rpath|^ERROR:  using QList' || true

[ -f "$FW/QtCore.framework/Versions/A/QtCore" ] ||
    die "macdeployqt staged no QtCore — nothing was deployed"

# ---------------------------------------------------------------------------
step "rpath: pointing the bundle at itself"
# CMake sets BUILD_RPATH/INSTALL_RPATH to @executable_path/../Frameworks, but
# a bundle built before that change — or by a different generator — can still
# carry the Qt prefix.  Strip every non-bundle rpath rather than trusting the
# build to have done it.
while read -r rp; do
    case "$rp" in
        @executable_path/../Frameworks|@executable_path/../Frameworks/) ;;
        *) echo "  removing rpath: $rp"
           install_name_tool -delete_rpath "$rp" "$EXE" ;;
    esac
done < <(otool -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}')

if ! otool -l "$EXE" | grep -q '@executable_path/../Frameworks'; then
    echo "  adding rpath: @executable_path/../Frameworks"
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXE"
fi

# ---------------------------------------------------------------------------
step "install names: normalising staged dylibs"
# macdeployqt copies plain dylibs (brotli, webp, icu, …) into Frameworks but
# does not always rewrite their own LC_ID_DYLIB, leaving an id that still
# points into the Homebrew prefix.  Referrers usually say @rpath/<name>, so
# the bundle happens to run — but the stale id is a live trap: anything that
# resolves by install name picks up the system copy instead, which is how a
# second copy of a library gets loaded.  deskHPSDR.app has none of these.
for dylib in "$FW"/*.dylib; do
    [ -e "$dylib" ] || continue
    id="$(otool -D "$dylib" | sed -n '2p')"
    case "$id" in
        ""|@*) continue ;;                      # already relocatable
    esac
    base="$(basename "$dylib")"
    chmod u+w "$dylib"
    install_name_tool -id "@rpath/$base" "$dylib"
    echo "  id: $base <- $id"
done

# ---------------------------------------------------------------------------
step "WDSP engine: bundling FFTW beside it"
if [ -f "$NATIVE/libwdsp.dylib" ]; then
    for lib in libfftw3.3.dylib libfftw3f.3.dylib; do
        src="$FFTW_PREFIX/lib/$lib"
        [ -f "$src" ] || die "missing $src (brew install fftw)"
        cp -L "$src" "$NATIVE/$lib"
        chmod u+w "$NATIVE/$lib"
        install_name_tool -id "@loader_path/$lib" "$NATIVE/$lib"
        # macdeployqt may already have rewritten this to @executable_path/...;
        # -change is a no-op when the old path is absent, so try both spellings.
        install_name_tool \
            -change "$src" "@loader_path/$lib" \
            -change "@executable_path/../Frameworks/$lib" "@loader_path/$lib" \
            "$NATIVE/libwdsp.dylib"
        echo "  bundled $lib"
    done
else
    echo "  (no libwdsp.dylib staged — run tools/build_wdsp_macos.sh first)"
fi

# ---------------------------------------------------------------------------
step "codesign: nested code first, then the bundle"
# Frameworks and loadable code individually — NOT `codesign --deep`, which
# Apple deprecates and which applies the top-level identifier too broadly.
# Every install_name_tool edit above invalidated whatever signature was there.
find "$FW" "$APP/Contents/PlugIns" "$APP/Contents/Resources" "$NATIVE" \
     -type f \( -name '*.dylib' -o -name '*.so' \) -print0 2>/dev/null |
    xargs -0 -r codesign --force --timestamp=none --sign "$SIGN_ID" 2>/dev/null || true

find "$FW" -maxdepth 1 -name '*.framework' -print0 2>/dev/null |
    xargs -0 -r codesign --force --timestamp=none --sign "$SIGN_ID" 2>/dev/null || true

# The bundle last, with the real identifier.  Without --identifier the
# linker-signed name ("lyra") is kept, which is not the CFBundleIdentifier the
# system records TCC grants against.
codesign --force --timestamp=none --sign "$SIGN_ID" --identifier "$BUNDLE_ID" "$APP"

# ---------------------------------------------------------------------------
step "verify"
fail=0

rpaths="$(otool -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}')"
echo "  rpaths: $(echo "$rpaths" | tr '\n' ' ')"
if [ "$(echo "$rpaths" | grep -c .)" -ne 1 ] ||
   ! echo "$rpaths" | grep -q '@executable_path/../Frameworks'; then
    echo "  FAIL: expected exactly one rpath, @executable_path/../Frameworks"
    fail=1
fi

# The real test: nothing anywhere in the bundle may point at Homebrew.  A
# single leftover reference is enough to load a second copy of a library.
leaks="$(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 2>/dev/null |
         xargs -0 -r otool -L 2>/dev/null | grep -c '/opt/homebrew' || true)"
echo "  homebrew references in bundle: $leaks"
[ "$leaks" -eq 0 ] || { echo "  FAIL: bundle still references Homebrew"; fail=1; }

sig="$(codesign -dv --verbose=2 "$APP" 2>&1)"
echo "  $(echo "$sig" | grep -E '^Identifier=')"
echo "  $(echo "$sig" | grep -E 'Sealed Resources')"
echo "$sig" | grep -q "Identifier=$BUNDLE_ID" ||
    { echo "  FAIL: signing identifier is not $BUNDLE_ID"; fail=1; }
codesign --verify --deep --strict "$APP" 2>/dev/null ||
    { echo "  FAIL: signature does not verify"; fail=1; }

[ "$fail" -eq 0 ] || die "bundle is NOT self-contained — see failures above"

printf '\nOK — %s is self-contained (%s)\n' "$APP" "$(du -sh "$APP" | cut -f1)"
echo "Launch-test it from outside the build tree before trusting it:"
echo "  ditto $APP /tmp/lyra-package-test/lyra.app && open /tmp/lyra-package-test/lyra.app"
