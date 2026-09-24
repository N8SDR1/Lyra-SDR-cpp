# Building and running Lyra on macOS (Apple Silicon)

Status: **builds, launches, and runs the RX DSP chain natively on
arm64.** Not yet validated against real Hermes Lite 2 hardware — see
"What is not verified" at the bottom.

Verified on macOS 26.6 / Apple M5 / Xcode 26.6 (Apple clang 21),
Qt 6.11.1, Lyra v0.21.1.

---

## 1. Prerequisites

```bash
brew install cmake ninja qt fftw pkg-config
```

Homebrew's `qt` is currently 6.11.1, which satisfies the project's
`find_package(Qt6 6.11 REQUIRED)` pin exactly. Xcode command line tools
supply clang.

## 2. Build the WDSP DSP engine

Lyra loads WDSP at runtime. `_native/` in the source tree contains
**Windows DLLs only**, so the macOS engine is compiled from upstream
source:

```bash
tools/build_wdsp_macos.sh
```

This fetches TAPR `OpenHPSDR-wdsp` (WDSP 2.00) into `build/wdsp-macos/`,
compiles its 74 translation units against the POSIX shim in
`third_party/wdsp_posix/include/`, links `build/_native/libwdsp.dylib`,
and verifies the exports Lyra requires are present.

Set `WDSP_ARCH="arm64 x86_64"` for a universal build.

## 3. Build the app

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
```

```bash
cmake --build build -j8
```

The result is `build/lyra.app`. The build stages `libwdsp.dylib` into
`lyra.app/Contents/MacOS/_native/` where the loader expects it.

## 4. Run

```bash
./build/lyra.app/Contents/MacOS/lyra
```

## 5. Package (optional)

The bundle from step 3 resolves Qt from the Homebrew prefix, so it only runs
on a machine that has that exact Qt. To make it self-contained:

```bash
tools/package_macos.sh
```

This stages Qt and the QML modules with `macdeployqt`, relocates the FFTW the
WDSP engine needs, and re-signs the bundle — then **asserts** the result
rather than assuming it: exactly one `LC_RPATH`
(`@executable_path/../Frameworks`), zero absolute Homebrew references
anywhere in the bundle, and a signature carrying the real bundle identifier.

The failure it exists to prevent is subtle. `macdeployqt` rewrites the main
binary's Qt references but leaves CMake's Homebrew rpath in place, so the
bundled Qt *plugins* — which reference Qt as `@rpath/QtCore.framework/…` —
still resolve to Homebrew. Qt then loads **twice**, registering duplicate
Objective-C classes, and the app dies on launch. The bundle looks complete;
only running it reveals the problem. Hence `BUILD_RPATH` in CMakeLists.txt
and the assertions in the script.

Verify a packaged bundle by running it from outside the build tree — that is
the only real test of self-containment:

```bash
ditto build/lyra.app /tmp/lyra-test/lyra.app && open /tmp/lyra-test/lyra.app
```

Signing is ad-hoc by default; set `SIGN_ID` to a Developer ID for something
distributable. Notarisation and DMG creation are not covered here.

First launch performs a one-time FFTW "wisdom" build (an `FFTW_PATIENT`
plan search tuned to your CPU). It takes **about 3.5 minutes on an M5**
and shows a progress notice; the result is cached under
`~/Library/Application Support/N8SDR/Lyra-cpp/fftw/`. Later launches
import it in well under a second.

macOS will prompt for **local network** access on first discovery — the
HL2 is found by UDP broadcast, so this must be allowed.

---

## How the port works

Everything platform-specific lives behind `src/platform/`. On Windows
those headers compile to nothing and the original Win32 code is used, so
**the Windows build is unaffected**.

| Concern | Windows | macOS |
|---|---|---|
| Sync primitives in the verbatim ChannelMaster port | Win32 critical sections / semaphores | `src/platform/win32_compat.h` → recursive `pthread_mutex`, GCD dispatch semaphores |
| Sockets in `hl2_stream.cpp` | Winsock2 | `src/platform/socket_compat.h` → BSD sockets |
| Loading the DSP engine | `LoadLibraryExW` / `GetProcAddress` | `src/platform/dynamic_library.h` → `dlopen` / `dlsym` |
| Wire-thread scheduling | MMCSS "Pro Audio" | `src/platform/thread_priority.h` → Mach time-constraint policy |
| Kernel UDP drop counters | `GetUdpStatisticsEx` | `net.inet.udp.stats` sysctl (`udps_fullsock` ≡ `dwInErrors`) |
| Audio host API | WASAPI | CoreAudio |
| Graphics | Direct3D | Metal (Qt RHI selects automatically) |

The compat headers deliberately implement **only** the surface the code
actually uses, so an unported call site fails at compile time rather than
silently misbehaving.

### Why the ported code was not rewritten

The files under `src/wire/` are a deliberate line-for-line port of the
openHPSDR Thetis reference and are audited against it. Rewriting their
Win32 primitives into `std::mutex` / `std::counting_semaphore` would
destroy that parity and make every upstream merge a manual
reconciliation. Supplying the primitives instead keeps the diff to the
ported files at essentially zero.

---

## Known differences from the Windows build

* **PureSignal entry points are absent.** Upstream WDSP 2.00 does not
  export `SetPSIntsAndSpi`, `SetPSMapMode`, `SetPSPinMode`, `SetPSPtol`,
  `SetPSStabilize` or `psccF` — they come from the Thetis WDSP variant
  the bundled Windows DLL was built from. Lyra logs six
  `UNRESOLVED WDSP export` warnings at startup and continues. This is
  harmless today because PureSignal is not implemented (see
  KNOWN_ISSUES.md); the call table entries have no call sites. It does
  mean `test_ivac` fails its "0 unresolved" assertion on macOS.

* **FTDI USB-BCD amp control** is stubbed on POSIX (pre-existing
  `#ifdef`, unchanged by this port).

* **Windows performance knobs** (process priority class, the
  `NetworkThrottlingIndex` registry fix) are inert stubs; the macOS
  equivalent is the Mach time-constraint scheduling in
  `src/platform/thread_priority.cpp`.

* **Wisdom filename.** WDSP 2.00 writes `wdspWisdom01`; the bundled
  Windows DLL writes `wdspWisdom00`. `wdsp_native.cpp` now probes for
  both — hardcoding one made every launch repeat the multi-minute plan
  search.

---

## What is not verified

**RX against a real Hermes Lite 2 works.** Confirmed 2026-09-08 against an
HL2 (firmware 7.4) at 192.168.10.146: discovery, connection, a ~5.25 MB/s
EP6 stream at 192 kHz, a live panadapter, and an S-meter reading real
signals. (An earlier revision of this document said no radio had been
connected — that was wrong; the rig record shows one present since
2026-08-10.)

**TX against real hardware is still unvalidated on macOS.** Other confirmed
items:

* WDSP builds native arm64 and runs — the full RXA chain
  (`OpenChannel` → `SetRXAMode` → `fexchange0`) produces correctly
  AGC-normalised demodulated audio.
* The app launches, renders the complete UI on Metal, loads the engine
  and resolves 92/92 required symbols.
* 11 of the project's standalone tests pass. (`test_p2_session`
  requires a radio IP argument; `test_cw_decoder`, `test_profile` and
  `test_profile_rack` do not compile against the current API — these
  three are **pre-existing** staleness in `scratch/`, unrelated to the
  port, and fail on Windows too.)

Still to do before calling macOS supported: bring up against a real
HL2 (discovery → RX audio → TX), confirm the EP2 writer holds its
~2.6 ms cadence under load, and package with `macdeployqt` into a
self-contained `.app` / `.dmg` (the current bundle still resolves Qt
from the Homebrew prefix).
