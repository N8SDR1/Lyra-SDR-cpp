# ASIO for VAC — plan (draft for review)

Status: PLAN ONLY — nothing implemented.  2026-11-14.
Revision 2 — after second-agent review (verdict "sound with changes");
the four HIGH findings were re-verified by hand.  See §0 and §9.

## 0. Already in the tree (found in review — fix first)

**Done 2026-11-14 (branch feature/asio-vac):** old SDK removed; SDK
2.3.4 host files vendored at `third_party/asiosdk/` (GPLv3 option
confirmed from its own `LICENSE.txt`; see `LYRA-NOTES.md`); the
driver-hiding block removed from `pa_asio.cpp`; NOTICE / CREDITS carry
the SDK + trademark attribution.  Trademark rules read from the SDK's
Usage Guidelines: ASIO is opt-in in Lyra, so the ASIO Compatible logo
must appear in every dialog that enables / configures ASIO, ® on first
use, and the attribution sentence in credits and docs.

1. **An ASIO SDK is already committed and published.**
   `third_party/portaudio/src/hostapi/asio/ASIOSDK/{common,host,host/pc,
   host/sample}` (incl. a pre-patched `patched_asiolist.cpp`) came in with
   the PortAudio copy taken from the reference tree.  Its `asio.h` is
   "(c) 1997 - 2019 Steinberg … v2.3" — the pre-GPL, proprietary SDK —
   with no LICENSE file.  Not compiled today (`PA_USE_ASIO=OFF`) but it
   IS redistributed in the public repo.  Step 0: replace it with the
   GPLv3-licensed SDK + its LICENSE (one location only), or remove it if
   the GPLv3 option can't be confirmed.
2. **The vendored `pa_asio.cpp` carries a reference-app patch that hides
   the driver the reference app is using** (`pa_asio.cpp:1274-1304`
   reads `SOFTWARE\OpenHPSDR\Thetis-x64\ASIOdrivername` and skips that
   driver).  The target user has exactly that set for his WING, so Lyra
   would silently not list it.  Remove the patch; record it in a
   local-patches note.

## 1. Why

An operator with a Behringer WING Rack runs his mic into the PC over the
WING's ASIO driver (Thetis CMASIO).  Lyra's audio library (PortAudio
19.7.0, vendored, `third_party/portaudio`) is built with `PA_USE_ASIO=OFF`,
so Lyra can only reach the WING through its Windows (WASAPI / WDM-KS /
MME) endpoints, which on his setup do not carry the mic.  Goal: let VAC1
(and VAC2) open an ASIO device, so the TX mic can come straight from an
ASIO interface, and pick WHICH channels on a many-channel interface.

Out of scope: the main RX speaker output (Qt `QAudioSink`) moving to ASIO;
ASIO for the HL2 codec path; ASIO driver control panels beyond a button
that opens the driver's own panel (stretch).

## 2. Licence + trademark (gate before any code)

- Steinberg made the ASIO SDK dual-licence (proprietary / GPLv3) on
  2025-10-15.  Lyra is GPL-3.0-or-later → compatible; a build with ASIO
  is distributed under GPLv3.
- **Gate:** download the current SDK zip and read its own `LICENSE.txt`
  to confirm the GPLv3 option (we only have press coverage so far).
  Record the SDK version + licence text in `NOTICE.md` / `CREDITS.md`.
- **Gate:** read Steinberg's ASIO trademark guidelines before the word
  "ASIO" appears in Lyra's UI / docs.  Fallback if unclear: the host
  list shows PortAudio's own host-API name, which we don't control the
  spelling of, and the docs say "ASIO-compatible driver".
- PortAudio's CMake comment ("not allowed to be redistributed") predates
  the GPLv3 option — superseded only if the SDK LICENSE says so.

## 3. Getting the SDK into the build

Option A (preferred): vendor a pinned copy in `third_party/asiosdk/`
(only `common/`, `host/`, `host/pc/`, LICENSE) once the GPLv3 option is
confirmed — reproducible release builds (Cursor builds releases too), no
network at configure time.

Option B: PortAudio's own configure-time download
(`https://www.steinberg.net/asiosdk`).  Rejected as the default: the URL
can change or fail, which breaks configure for every builder.

CMake: do NOT rely on PortAudio's FindASIO (it globs
`${CMAKE_PREFIX_PATH}/asiosdk*`, which breaks when CMAKE_PREFIX_PATH is a
list — it is, with Qt — and writes `patched_asiolist.cpp` into the
source tree).  Define the `ASIO::host` INTERFACE IMPORTED target in the
top-level CMakeLists before `add_subdirectory(third_party/portaudio)`
(~line 62); PortAudio's `NOT TARGET ASIO::host` guard
(`third_party/portaudio/CMakeLists.txt:207`) then skips the download.
Ship the SDK's asiolist already patched.  The vendored copy replaces the
old one (§0.1) — one location only.  Add a top-level
`LYRA_WITH_ASIO` option (default ON) so a builder can turn it off.

## 4. Startup safety (the main risk)

With ASIO compiled in, `Pa_Initialize()` enumerates every ASIO driver on
the machine — it **loads each driver DLL**.  Some ASIO drivers are known to
hang or crash on load (or grab the device so other apps lose it).
`ivacInitPortAudio()` runs at startup for every user, so a bad driver
could stop Lyra starting for people who never use ASIO.

Plan:
1. **Opt-in setting**: Settings → Audio "Enable ASIO devices (restart)",
   default OFF.
2. Patch `src/os/win/pa_win_hostapis.c:84-85`: replace the
   `PaAsio_Initialize` entry with a wrapper.  Disabled → return
   `paNoError` with `*hostApi = NULL` (`pa_front.c:224-230` tolerates a
   NULL host API); no driver is loaded — identical to today.  Enabled →
   call the real initializer, and downgrade any ASIO init error to "no
   ASIO" + a log line, because a non-`paNoError` from any host
   initializer aborts the whole `Pa_Initialize` (`pa_front.c:225-226`)
   and would take WASAPI VAC down with it.  Also cover a hang at
   shutdown (`Pa_Terminate` / `ASIOExit`, `main.cpp:1114`).
3. Use the existing startup crash ladder: set a "pending" marker before
   PortAudio init with ASIO on; if the previous start never completed,
   come up with ASIO disabled and tell the operator (mirrors the graphics
   safe-mode ladder in `main.cpp`).
4. Log the ASIO drivers found at init.

## 5. Opening an ASIO stream for VAC

Current path: `WdspEngine::rebuildVac()` → `resolveVacPaSel()` (host API
by name, then input + output device by name) → `StartAudioIVAC()`
(`src/wire/Ivac.cpp`) → one full-duplex `Pa_OpenStream`, 2 channels each
way, `paFloat64`, `vac_rate`, `vac_size` frames.

ASIO differences to handle:
- **Same device for in + out.** PortAudio's ASIO host is full duplex only
  on one driver.  An ASIO "device" = one driver with both directions, so
  for ASIO the UI should pick ONE device and use it for both; reject a
  mismatched in/out pair with a clear log + status message.
- **Channel selection.** A WING exposes dozens of ASIO channels; PortAudio
  opens channels 0..n-1 unless given `PaAsioStreamInfo.channelSelectors`.
  Add settings "ASIO input" and "ASIO output" (start channel, 1-based in
  the UI) and pass selectors for both directions — each direction gets
  its own `PaAsioStreamInfo` with `paAsioUseChannelSelectors`, and
  `channelCount` must equal the selector length (supported in duplex,
  `pa_asio.cpp:2086-2116`, `:2240-2259`, validated at `:1899`).  **Input
  may be a single (mono) channel** — a mic usually is; a length-1
  selector gives `channelCount=1`, which `CallbackIVAC` already copies to
  both sides (`Ivac.cpp:218-252`).  Output stays a stereo pair.  Thetis
  CMASIO has the same idea (base channels + left/right/both input mode).
- **One ASIO stream per program.** PortAudio's ASIO host refuses a second
  open while one is open (`pa_asio.cpp:1503-1506`, `:2048-2050`).  ASIO
  is for ONE VAC at a time; VAC2 on ASIO while VAC1 holds it is refused
  with a clear message.  The "Driver panel…" button loads the driver
  itself, so the stream must be closed first.
- **Sample rate.** PortAudio calls `ASIOSetSampleRate(48000)` whenever
  the driver is at another rate (`pa_asio.cpp:1946-1990`) — on a WING that
  re-clocks the whole console.  Read `ASIOGetSampleRate` first; if not
  48 kHz, do NOT change it — refuse with "set the interface to 48 kHz"
  (operator's choice) and log the current rate.  (rmatch already absorbs
  clock drift.)
- **Driver reset / buffer change / unplug.** PortAudio only acknowledges
  `kAsioResetRequest` / `kAsioBufferSizeChange`
  (`pa_asio.cpp:3228-3262`), leaving a dead stream.  Add a watchdog
  (callback heartbeat or rmatch underflow run) that calls `rebuildVac`
  and logs it.
- **Buffer size.** Keep `vac_size`; PortAudio adapts to the driver's
  preferred buffer.  Log the driver's min/preferred/max buffer at open.
- **Exclusive flags** in `StartAudioIVAC` are WASAPI-only already — no
  change; ensure `hostApiSpecificStreamInfo` gets the ASIO struct instead.
- **One client per driver.** Many ASIO drivers allow a single app.  If
  Thetis (or a DAW) holds it, open fails → log "ASIO device busy — close
  other ASIO programs" in the VAC failure message.
- **The `in_dev > 0` / `out_dev > 0` checks** in `StartAudioIVAC`
  (`Ivac.cpp:282`, `:293`) treat global index 0 as "none".  ASIO is never
  index 0 in practice (MME / DirectSound come first), but fix to `>= 0`,
  reject negative results from `Pa_HostApiDeviceIndexToDeviceIndex`, and
  attach `PaAsioStreamInfo` by the device's host-API type.  Adding ASIO
  shifts WASAPI's host-API index — harmless, resolution is by name.
- **64-bit drivers only** (x64 build) — note in the docs.  COM: PortAudio
  init / open / close run on the Qt main thread; its STA `CoInitialize`
  is compatible with Qt's OLE STA (review confirmed).
- **Input-only use (mic only).** The WING user only needs input.  Today
  VAC requires both an input and an output.  For ASIO this is natural
  (same driver both ways); keep the full-duplex model and just default
  the output to the same device.  (Input-only WASAPI is a separate,
  smaller change — out of scope here.)

## 6. UI (Settings → Audio, VAC1 / VAC2 groups)

- Host-API combo already lists every PortAudio host API with devices, so
  "ASIO" appears automatically once enabled.  When ASIO is selected:
  - device list = ASIO drivers; selecting one sets both in and out;
  - show "Input channels" and "Output channels" pair pickers (e.g. "1-2",
    "3-4", …) filled from the driver's channel counts;
  - show the driver's current sample rate / buffer (read at open);
  - optional "Driver panel…" button (`PaAsio_ShowControlPanel`).
- Persist new keys next to the existing GLOBAL VAC device settings:
  `vacN/asioInChannel`, `vacN/asioInMono`, `vacN/asioOutChannel`.  NO
  profile change — devices are deliberately global station setup, not
  profile fields (`src/profile/Profile.h:20-21`).
- Status text when ASIO is off: the host list simply has no ASIO entry,
  plus a hint next to "Enable ASIO devices".

## 7. Testing

- Unit: channel-selector builder (pair → selectors, bounds vs device
  channel count), host/device resolution for ASIO (same device both ways,
  mismatch rejected), persistence round-trip incl. profile schema bump.
- Bench (Rick): ASIO4ALL or any ASIO interface: VAC1 on ASIO, mic in →
  TX MIC meter + on-air into dummy load; RX → VAC out channels; switch
  channel pairs live; busy-driver message with another ASIO app open;
  ASIO setting OFF → startup log shows no ASIO drivers loaded.
- Field (WING user): VAC1 = WING ASIO, input pair = his mic channels,
  with Thetis closed.
- Regression: WASAPI VAC unchanged (digital-mode users), startup time
  with ASIO off unchanged.

Added from review: Thetis `ASIOdrivername` registry value present → the
driver is still listed; buffer size changed in the driver panel
mid-stream → watchdog recovers; VAC2 on ASIO while VAC1 holds it →
clear refusal; interface at 44.1 / 96 kHz → refused, console not
re-clocked; mono mic channel → audio on both sides of TX.

## 8. Order of work

0. §0: deal with the already-committed proprietary SDK and the
   driver-hiding patch.
1. Licence + trademark gates (§2).  Stop if the GPLv3 option is not in
   the SDK's LICENSE.
1b. Throwaway bench spike: ASIO4ALL + a hard-coded VAC1 open, to prove
   the PortAudio ASIO path works in Lyra before any UI work.
2. Vendor SDK + CMake + PortAudio opt-in patch (§3, §4) — Lyra builds,
   ASIO off by default, nothing else changes.
3. ASIO open path in `StartAudioIVAC` / `rebuildVac` + logging (§5).
4. Settings UI + global persistence (§6; no profile change).
5. Tests + bench + field test (§7), help doc (`docs/help/audio.md`).

## 9. Review record

Second-agent review 2026-11-14: "sound with changes".  Accepted all
findings: committed proprietary SDK (§0.1), driver-hiding patch (§0.2),
one ASIO stream per process (§5), devices are global not profile (§6),
init-error downgrade + shutdown hang (§4), sample-rate re-clock (§5),
reset/resync watchdog (§5), mono mic input (§5), CMake target instead of
FindASIO (§3), index checks (§5).  Confirmed OK: the driver-loading
claim, selectors in duplex, same-device rule, COM threading, Float64.
