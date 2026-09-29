# #159 — DSP per-mode buffer / filter-size / latency surface (Setup→DSP→Options parity)

**Status:** DESIGN (build-ready). No code yet. Reference-verified
against Thetis 2.10.3.13 on 2026-06-22.

**Scope:** a Settings → DSP surface that exposes, per mode-family,
the WDSP **buffer size**, **filter size (taps)**, **filter type
(Linear Phase / Low Latency)**, plus a global **RX/TX window** and
the **filter-impulse cache** toggles — matching the reference's
"Setup → DSP → Options" tab. This is a DSP-tuning + latency surface,
NOT audio routing; it is independent of the VAC / profile work.

The operator flagged this as "DSP Buffers we are missing this" while
reviewing the reference's multimeter/DSP screenshots.

---

## 1. Reference ground truth (Thetis 2.10.3.13, cited)

Source root `D:\sdrprojects\OpenHPSDR-Thetis-2.10.3.13\Project Files\Source\`.
Full dossier captured 2026-06-22; the load-bearing facts:

### 1.1 The control set (per mode-family, except window = global)

Five mode-families: **Phone** (LSB/USB/DSB/AM/SAM), **FM**, **CW**
(RX only), **Dig** (DIGL/DIGU/DRM). Each has independent RX and TX
values **except CW, which is RX-only** (CW-TX is firmware/keyer
handled — no CW-TX buffer/filter combo exists).

| Control | Items | Default (UI init) | Tooltip |
|---|---|---|---|
| Buffer size | 64 / 128 / 256 / 512 / 1024 | 1024 | "larger yields sharper filters, more latency" |
| Filter size (taps) | 1024 / 2048 / 4096 / 8192 / 16384 | 4096 | same |
| Filter type | Linear Phase / Low Latency | Linear Phase (idx 0) | "Low Latency (Minimum Phase) or Linear Phase" |
| RX window (GLOBAL) | BH-4 / BH-7 | BH-7 fallback | "BH-4 sharper; BH-7 deeper cutoff" |
| TX window (GLOBAL) | BH-4 / BH-7 | BH-7 fallback | same |
| Cache Impulse Data | checkbox | ON | — |
| Save/Restore cache file | checkbox | ON | "WARNING: this file can become very large" |

Controls: `setup.designer.cs` (item lists/labels), `setup.cs`
(handlers). Combos `comboDSP{Phone,FM,CW,Dig}{RX,TX}{Buf,FiltSize,
FiltType}`, `comboDSP{Rx,Tx}Window`, `chkWDSP_cache_impulse`,
`chkWDSP_save_restore_cache_impulse`.

### 1.2 The exact WDSP contract (what each control drives)

| Control | WDSP call (RX / TX) | Semantics |
|---|---|---|
| Buffer size | `SetDSPBuffsize(id, n)` (`channel.c:181`) | sets `dsp_size` = overlap-save **partition length**; **full channel rebuild** (tears down + rebuilds pre/post-main chain) |
| Filter size | `RXASetNC(id, nc)` (`RXA.c:1043`) / `TXASetNC(id, nc)` | `nc` = total FIR coeffs; fans out to every NC-bearing block (NBP, bandpass, EQ, FMSQ, …); `nfor = nc / size` overlap-save partitions |
| Filter type | `RXASetMP(id, bool)` (`RXA.c:1056`) / `TXASetMP(id, bool)` | **Linear_Phase=0 → mp=false; Low_Latency=1 → mp=true**; mp runs `mp_imp()` minimum-phase cepstral conversion (`firmin.c:327`) |
| Window | `SetRXABandpassWindow(id, w)` / `SetTXABandpassWindow(id, w)` | **0 = BH-4** (Blackman-Harris 4-term), **1 = BH-7** (7-term); `fir.c:247/254` |
| Cache Impulse | `use_impulse_cache(0/1)` | enable/disable the FIR-impulse LRU |
| Save/Restore | `read_impulse_cache(path)` / `save_impulse_cache(path)` | binary `impulse_cache.dat` in app-data |

Mode → DSP-setting switch is `console.cs UpdateDSP()` (`:39694`):
selects per-family RX1/RX2/TX values on every mode change, enforces
**`filtsize ≥ bufsize`** (`:39819-39821`, keeps `nfor ≥ 1`), pushes
only changed values.

### 1.3 "Low Latency" is minimum-phase, NOT a smaller FFT

Critical to get right: the **filter type** control does **not**
change FFT/partition size. `firmin.c:322-330`:
`if (mp) mp_imp(nc, impulse, imp, 16, 0); else memcpy(...)`. A
linear-phase FIR has flat group delay of **(N−1)/2 samples** (large
for big `nc`); minimum-phase concentrates energy at the front →
**much lower group delay**, nonlinear phase. That bulk-delay removal
is the "low latency." Block latency is set by **buffer size**, the
separate control.

### 1.4 Impulse cache ≠ FFTW wisdom (two distinct mechanisms)

- **FFTW wisdom** (`wisdom.c WDSPwisdom`) — FFTW plan cache. lyra-cpp
  **already ships this** (the 2026-05-18 WISDOM work; `%APPDATA%`
  private dir, Settings "Clear & rebuild").
- **Filter Impulse Cache** (`impulse_cache.c`, MW0LGE 2024/25) — a
  separate FNV-1a-hash-keyed LRU of computed FIR impulse arrays
  (and their min-phase conversions in a 2nd `MP_CACHE` bucket),
  used by `fir.c fir_bandpass`. Avoids recomputing windowed-sinc
  coeffs + `mp_imp()` on every mode/BW/buffer change. Persisted to
  `impulse_cache.dat`. **Version-locked to wisdom**: if wisdom was
  rebuilt this run, the impulse cache is deleted as invalid
  (`radio.cs:139-146`).

---

## 2. lyra-cpp current state (grounding)

- `OpenChannel` + `SetDSPBuffsize` + `SetInputBuffsize` are **already
  bound** in `src/wire/wdspcalls.h:55-64`. lyra-cpp opens RX/TX
  channels with a hard-coded `dsp_size = 4096` (`src/wire/CMaster.cpp:203`
  "dsp buffer size"; `wdsp_engine.h` `dspSize = 4096`).
- `RXASetNC` / `TXASetNC` / `RXASetMP` / `TXASetMP` /
  `SetRXABandpassWindow` / `SetTXABandpassWindow` are **NOT yet
  bound** — confirmed by grep of `wdspcalls.h` (only `SetDSPBuffsize`
  present). These cdefs + function-pointer loads are the first
  porting step.
- WISDOM (the FFTW plan cache) is shipped. The Filter Impulse Cache
  is **not** ported.
- lyra-cpp has no per-mode DSP-tuning surface today; buffer/filter
  sizes are fixed at channel-open.

---

## 3. Lyra-native design

Same WDSP contract, Lyra-native UI + persistence (Settings → DSP tab,
Profile/Prefs persistence; no Thetis DB).

### 3.1 cdef + loader (wdspcalls)

Add to `src/wire/wdspcalls.{h,cpp}` the missing function pointers,
loaded from `wdsp.dll` beside the existing `SetDSPBuffsize`:
```
extern void (*RXASetNC)(int channel, int nc);
extern void (*TXASetNC)(int channel, int nc);
extern void (*RXASetMP)(int channel, int mp);
extern void (*TXASetMP)(int channel, int mp);
extern void (*SetRXABandpassWindow)(int channel, int wintype);
extern void (*SetTXABandpassWindow)(int channel, int wintype);
```
Verify each symbol resolves against the bundled `wdsp.dll` (dumpbin
/ the existing loader's missing-symbol guard) — same discipline as
the CTUNE `SetRXAShiftFreq` bind.

### 3.2 WdspEngine surface

`WdspEngine` (RX) + `TxChannel` (TX) each gain:
`setDspBufferSize(int)`, `setFilterSize(int nc)`,
`setFilterType(bool minPhase)`, `setBandpassWindow(int wintype)`.
Each calls the bound setter on its channel id. **`setDspBufferSize`
must respect that `SetDSPBuffsize` does a full channel rebuild** —
it re-applies the channel's current mode/filter/gain state after
(lyra-cpp already re-applies on rate-change; reuse that path).

Enforce **`filterSize ≥ bufferSize`** in the engine (clamp + log),
mirroring `UpdateDSP():39819`. `nfor = nc/size` must stay ≥ 1.

### 3.3 Per-mode model + mode-change apply

A `DspOptions` model holding, per family {Phone, FM, CW(RX-only),
Dig}, the {RX,TX} × {buf, filtSize, filtType} triple + the two
global windows + the two cache flags. On **mode change** (the
existing Lyra mode-switch path), resolve the active family's RX/TX
values and push any that differ — the lyra-cpp analogue of
`UpdateDSP()`. CW-TX reuses the current TX values (no CW-TX combo),
exactly as the reference (`:39800`).

### 3.4 Defaults (locked to reference)

buffer **1024**, filter size **4096**, filter type **Linear Phase**,
window **BH-7**, both cache flags **ON**. (These are the reference's
shipped combo defaults; the operator can A/B Low-Latency per mode.)

### 3.5 Persistence

Per-family/direction keys in Prefs/Profile (Lyra-native names, not
Thetis DB columns), e.g. `dsp/phone_rx_buf`, `dsp/phone_rx_filtsize`,
`dsp/phone_rx_filttype`, … `dsp/cw_rx_*`, `dsp/rx_window`,
`dsp/tx_window`, `dsp/cache_impulse`, `dsp/cache_save_restore`.
These are **DSP-tuning**, NOT part of the #49 TX Profile bundle
(operator-locked: profiles are the TX audio chain) — keep them in
the global DSP-options Prefs group.

### 3.6 Filter Impulse Cache (port decision — RECOMMEND DEFER)

The two cache checkboxes can ship **inert/hidden** in v1 and the
`impulse_cache.c` port deferred, because:
- lyra-cpp already has WISDOM (the bigger startup-latency win).
- The impulse cache is a *retune-speed* optimization (avoids
  recomputing FIR coeffs on mode/BW/buffer change) — real but
  second-order, and it adds a version-lock-to-wisdom coupling +ﾟa
  persisted binary file to manage.
- **No-inert-UI rule:** therefore do NOT show the two checkboxes in
  v1. Land buffer/filter/type/window first (the operator-visible
  latency knobs); add the impulse-cache port + its two checkboxes
  as a follow-on only if retune lag is felt.

(If we DO port it later: `impulse_cache.{h,cpp}` verbatim port with
attribution, `use_/read_/save_/init_/destroy_impulse_cache` cdefs,
`impulse_cache.dat` in the same `%APPDATA%\Lyra` dir as wisdom,
deleted-on-wisdom-rebuild coupling preserved.)

---

## 4. UI (Settings → DSP)

A grid mirroring the reference: rows = {Phone, FM, CW, Dig}, columns
= RX Buf / RX Filt / RX Type / TX Buf / TX Filt / TX Type (CW-TX cells
disabled/blank). Plus a global RX-window + TX-window combo and (later)
the two cache checkboxes. Tooltips carry the latency trade-off text.
Live-apply on change (mode-resolved push), persisted immediately.

---

## 5. Build/verify gates

1. cdef bind verify — all six new symbols resolve against `wdsp.dll`.
2. Clean build (`lyra.exe ... [QML]`).
3. RX bench: change RX filter type Linear↔Low-Latency on SSB; confirm
   audible group-delay/latency change, no crash on the
   `SetDSPBuffsize` channel rebuild.
4. `filterSize ≥ bufferSize` clamp unit-tested.
5. Operator A/B: per-mode buffer/filter latency vs sharpness on real
   signals; confirm the defaults feel right.

---

## 6. Scope notes / decisions for operator

- **CW-TX has no buffer/filter combo** (reference parity — firmware/
  keyer handles it). Confirm OK.
- **Impulse cache deferred** (§3.6) — buffer/filter/type/window is the
  v1 deliverable. Confirm OK, or pull the cache port into v1.
- This surface is **independent** of RX2 (#96–#101) and TX Profile
  (#49) — per-mode DSP tuning is global, not per-profile.

## SHIPPED (2026-06-26) — slim v1 + PureSignal forward-compat

Slim v1 shipped: filter-TYPE only (Linear Phase / Low Latency =
minimum-phase via `RXASetMP`/`TXASetMP`), buffer fixed at 4096,
opt-in (default all Linear = legacy-identical). `WdspEngine::
applyDspFilterTypes()` is the single funnel; the TXA push is gated by
`txaChannelOpen_` (flipped by `main()` around `create_xmtr`/
`destroy_xmtr` — the TXA channel chid(1,0) is created lazily at
stream-connect, so an ungated `TXASetMP(1,…)` AV'd inside wdsp.dll
at startup).

**⚠ PureSignal prerequisites (wire these when PS is built — verify
Thetis behavior first):**
1. **Lock PS out in DIGU/DIGL/DRM, CWL/CWU, and FM.** SHIPPED — live
   Arm is operator intent AND-gated with not those modes (`psLiveArmed` /
   `refreshPsWire`). Digital apps recommend against PS — on
   near-constant-envelope digital it can raise IMD. CW is a keyed
   carrier (and on HL2 not the same I/Q PS trains on). FM is
   constant-envelope so there is no AM trajectory to linearize.
   Mirror of the `setMode` digital-bypass idiom (RX-EQ #59 / TX-rack /
   PHROT #109 stays DIG-only). Also makes the minimum-phase+PS combo
   impossible in digital (where Low Latency is the recommended pick).
2. **Force Linear Phase on the TX path while PS is armed** (Phone /
   AM, where PS is allowed), restore on PS-off. Minimum-phase
   shifts TXA group delay; PS feedback alignment is delay-sensitive.
   ~2-line gate at the single `applyDspFilterTypes()` funnel.

At default (all Linear) #159 is a wire no-op, so this is forward-
compat, not a present bug. Full rationale: memory `project-lyra-cpp-tx`.
