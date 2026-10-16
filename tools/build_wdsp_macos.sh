#!/usr/bin/env bash
#
# Lyra — build the WDSP DSP engine as a native macOS dylib.
#
# WDSP (TAPR OpenHPSDR-wdsp, Dr. Warren Pratt NR0V, GPL v3+) ships as
# Windows binaries in _native/.  This script builds the same library from
# upstream source for macOS, using the POSIX shim in
# third_party/wdsp_posix/include (see that directory for the mapping).
#
# The WDSP source is fetched, not vendored: it is a large GPL tree with its
# own upstream, and Lyra links it at runtime rather than compiling it in.
#
# Usage:
#   tools/build_wdsp_macos.sh [output-dir]
#
# Environment:
#   WDSP_ARCH   arm64 (default) | x86_64 | "arm64 x86_64" for a universal build
#   WDSP_TAG    upstream WDSP version directory (default "wdsp 2.00")
#
set -euo pipefail

readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly SHIM_DIR="${REPO_ROOT}/third_party/wdsp_posix/include"
readonly WORK_DIR="${REPO_ROOT}/build/wdsp-macos"
readonly SRC_DIR="${WORK_DIR}/OpenHPSDR-wdsp"
readonly UPSTREAM="https://github.com/TAPR/OpenHPSDR-wdsp.git"

OUTPUT_DIR="${1:-${REPO_ROOT}/build/_native}"
WDSP_ARCH="${WDSP_ARCH:-arm64}"
WDSP_TAG="${WDSP_TAG:-wdsp 2.00}"

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
info() { printf '==> %s\n' "$*"; }

# ---------------------------------------------------------------- checks
[[ "$(uname -s)" == "Darwin" ]] || die "this script targets macOS; see CROSS_PLATFORM.md for Linux"
command -v clang >/dev/null || die "clang not found — install Xcode command line tools"
command -v git   >/dev/null || die "git not found"
[[ -d "${SHIM_DIR}" ]]      || die "POSIX shim missing at ${SHIM_DIR}"

# FFTW is WDSP's only external dependency.
FFTW_PREFIX="$(brew --prefix fftw 2>/dev/null || true)"
[[ -n "${FFTW_PREFIX}" && -d "${FFTW_PREFIX}" ]] \
    || die "FFTW not found — run: brew install fftw"

# ----------------------------------------------------------- fetch source
mkdir -p "${WORK_DIR}"
if [[ ! -d "${SRC_DIR}/.git" ]]; then
    info "fetching WDSP source from ${UPSTREAM}"
    git clone --depth=1 "${UPSTREAM}" "${SRC_DIR}"
else
    info "reusing WDSP source at ${SRC_DIR}"
fi

readonly WDSP_SRC="${SRC_DIR}/${WDSP_TAG}/Source"
[[ -d "${WDSP_SRC}" ]] || die "WDSP source not found at ${WDSP_SRC} (check WDSP_TAG)"

# ---------------------------------------------------------------- compile
OBJ_DIR="${WORK_DIR}/obj"
rm -rf "${OBJ_DIR}"
mkdir -p "${OBJ_DIR}" "${OUTPUT_DIR}"

arch_flags=()
for arch in ${WDSP_ARCH}; do
    arch_flags+=(-arch "${arch}")
done

# -fvisibility=hidden plus the __declspec(dllexport) remap means exactly
# the symbols WDSP marks PORT are exported — the same contract as the
# Windows DLL, so Lyra's loader resolves an identical symbol set.
compile_flags=(
    "${arch_flags[@]}"
    -O2 -std=gnu11 -fPIC
    -fvisibility=hidden
    -D'__declspec(a)=__attribute__((visibility("default")))'
    -I"${SHIM_DIR}"
    -I"${WDSP_SRC}"
    -I"${FFTW_PREFIX}/include"
    # WDSP is long-lived C written for MSVC's permissive front end; clang
    # 21 promotes several of these legacy constructs to errors by default.
    # They are pre-existing upstream style, not portability defects.
    -Wno-implicit-int
    -Wno-implicit-function-declaration
    -Wno-deprecated-non-prototype
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-parentheses
    -Wno-unused-value
    -Wno-ignored-attributes
)

sources=("${WDSP_SRC}"/*.c)
[[ ${#sources[@]} -gt 0 ]] || die "no .c files found in ${WDSP_SRC}"

info "compiling ${#sources[@]} translation units for [${WDSP_ARCH}]"
compiled=0
for source in "${sources[@]}"; do
    object="${OBJ_DIR}/$(basename "${source}" .c).o"
    clang -c "${compile_flags[@]}" -o "${object}" "${source}"
    compiled=$((compiled + 1))
done
info "compiled ${compiled} objects"

# ------------------------------------------------------------------ link
readonly DYLIB="${OUTPUT_DIR}/libwdsp.dylib"
info "linking ${DYLIB}"
clang -dynamiclib "${arch_flags[@]}" \
    -o "${DYLIB}" \
    "${OBJ_DIR}"/*.o \
    -L"${FFTW_PREFIX}/lib" -lfftw3 -lfftw3f -lm -lpthread \
    -install_name "@rpath/libwdsp.dylib"

# --------------------------------------------------------------- verify
# Read the exported text symbols ONCE.  (Piping nm into `grep -q` per
# symbol would let grep close the pipe early, killing nm with SIGPIPE and
# tripping `set -o pipefail` — a false "missing symbol" report.)
exports="$(nm -gU "${DYLIB}" | awk '$2 == "T" { print substr($3, 2) }' | sort -u)"
info "exported symbols: $(printf '%s\n' "${exports}" | wc -l | tr -d ' ')"

# The entry points Lyra treats as fatal if unresolved (wdsp_native.cpp).
missing=0
for symbol in OpenChannel CloseChannel SetChannelState fexchange0 \
              SetRXAMode RXASetPassband SetRXAAGCMode XCreateAnalyzer \
              GetPixels SetAnalyzer Spectrum0 SetTXAMode create_resample; do
    if ! printf '%s\n' "${exports}" | grep -qx "${symbol}"; then
        printf 'MISSING required export: %s\n' "${symbol}" >&2
        missing=$((missing + 1))
    fi
done
[[ ${missing} -eq 0 ]] || die "${missing} required WDSP export(s) missing"

info "OK — $(file -b "${DYLIB}")"
info "wrote ${DYLIB}"
