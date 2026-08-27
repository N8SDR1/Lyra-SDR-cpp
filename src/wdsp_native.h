// Lyra — WDSP DLL loader (Step 3a).
//
// Loads `wdsp.dll` (and its dependent `libfftw3-3.dll` /
// `libfftw3f-3.dll` / `rnnoise.dll` / `specbleach.dll`) from
// `<lyra.exe dir>/_native/` at application startup.  Step 3a's
// scope is ONLY load-or-fail — no function pointers resolved yet
// (that's Step 3b), no DSP yet (Step 3c+).
//
// The DLLs are GPL v3+ (NR0V's WDSP DSP engine + FFTW dependencies)
// and are LINK-COMPATIBLE with Lyra (also GPL v3+).  Per the locked
// architecture rule from FEATURES.md §0 they are LINKED DIRECTLY
// into the binary at runtime via LoadLibrary (the cffi pivot that
// rescued the Python tree from per-sample Python/GIL overhead now
// happens natively in C++ — no cffi, no GIL, no Python anywhere).
//
// Why explicit dynamic-load instead of implicit link:
//   * We don't have an `wdsp.lib` import library from the Python
//     tree — only the DLLs.  Generating one via `dumpbin /exports`
//     + `lib /def:` is doable but adds toolchain complexity for
//     zero functional benefit.
//   * Explicit LoadLibrary + GetProcAddress (Step 3b) keeps the
//     binding boundary explicit + searchable in code.
//   * Matches the proven Python tree pattern (cffi `dlopen` →
//     `ffi.cdef`) line-for-line: declare the C ABI ourselves,
//     resolve symbols at runtime.

#pragma once

#include <QObject>
#include <QString>

namespace lyra::dsp {

// WDSP C ABI function pointer types.  All extern "C" -- no name
// mangling, matching the wdsp.dll exports directly.  Signatures
// verified against the upstream WDSP source (Warren Pratt NR0V,
// GPL v3+).  Parameter types are LOAD-BEARING -- a single
// `int` vs `double` mismatch on Windows x86_64 causes a
// register-class calling-convention bug (cf. the v0.0.9.8.1
// SetRXAAGCSlope regression in the Python tree, CLAUDE.md
// §15.26).  Do NOT modify these without re-verifying.
extern "C" {
using fn_OpenChannel_t = void (*)(
    int channel, int in_size, int dsp_size,
    int input_samplerate, int dsp_rate, int output_samplerate,
    int type, int state,
    double tdelayup, double tslewup,
    double tdelaydown, double tslewdown,
    int block);
using fn_CloseChannel_t        = void (*)(int channel);
using fn_SetChannelState_t     = int  (*)(int channel, int state, int dmode);
using fn_fexchange0_t          = void (*)(int channel,
                                          double *in_buff,
                                          double *out_buff,
                                          int *error);
using fn_SetRXAMode_t          = void (*)(int channel, int mode);
using fn_RXASetPassband_t      = void (*)(int channel,
                                          double f_low, double f_high);
using fn_SetRXAAGCMode_t       = void (*)(int channel, int mode);
using fn_SetRXAPanelBinaural_t = void (*)(int channel, int bin);
// CTUNE / center-tune lock (#174) — RXA receiver-oscillator shift: the
// Thetis RXOsc analog.  Demodulate at an Hz offset WITHIN the captured IQ
// span while the hardware DDC centre stays locked.  Freq sets the baseband
// shift (Hz); Run gates it on/off.  Both verified present in the bundled
// wdsp.dll (dumpbin /exports, 2026-06-21: SetRXAShiftFreq #282,
// SetRXAShiftRun #281).  Sign vs the HL2 mirrored baseband is bench-
// verified before any caller exists (CTUNE step 2).
using fn_SetRXAShiftFreq_t     = void (*)(int channel, double fshift);
using fn_SetRXAShiftRun_t      = void (*)(int channel, int run);
// #174 CTUNE — Thetis pairs RXANBPSetShiftFrequency with SetRXAShiftFreq
// (radio.cs:1417-1418) so the front-of-chain notch bandpass + manual notches
// track the demod shift.  Export #45 in the bundled wdsp.dll (dumpbin 2026-06-22).
using fn_RXANBPSetShiftFrequency_t = void (*)(int channel, double fshift);
// #159 slim DSP — filter type (Linear Phase mp=0 / Low Latency = minimum
// phase mp=1).  RXASetMP / TXASetMP exported by the bundled wdsp.dll
// (dumpbin 2026-06-26).  Null-safe like the CTUNE binds.
using fn_RXASetMP_t            = void (*)(int channel, int mp);
using fn_TXASetMP_t            = void (*)(int channel, int mp);
using fn_WDSPwisdom_t          = int  (*)(char *directory);
using fn_wisdom_get_status_t   = char *(*)();   // live "Planning FFT size N" string
// WDSP impulse cache (impulse_cache.c -- Warren Pratt NR0V 2024 +
// Richard Samphire MW0LGE 2025).  Caches computed filter impulse
// responses (fir_bandpass / min-phase / EQ / filter-curve buckets,
// FNV-1a hashed) to impulse_cache.dat so the per-launch filter
// recompute the reference avoids is avoided here too.  SEPARATE from
// FFTW wisdom: wisdom caches FFT *plans* (minutes); this caches filter
// *impulse responses* (per-launch recompute).  All are
// __declspec(dllexport) in impulse_cache.h -- soft-resolved (optional)
// so a wdsp.dll predating the impulse cache still loads + runs.
using fn_init_impulse_cache_t    = void (*)(int use);
using fn_read_impulse_cache_t    = int  (*)(const char *path);
using fn_save_impulse_cache_t    = int  (*)(const char *path);
using fn_use_impulse_cache_t     = void (*)(int use);
using fn_destroy_impulse_cache_t = void (*)();
// Step 3e level-cal: AGC threshold/slope + panel gain.  SetRXAAGCThresh
// computes the AGC max_gain from (thresh, size, rate) + the slope-
// derived var_gain — replacing WDSP's hot create-time default
// (max_gain = 10000 / 80 dB) that makes the audio overshoot 0 dBFS.
// SLOPE IS `int` — a `double` binding produces a register-class
// calling-convention bug on Windows x86_64 (CLAUDE.md §15.26 / the
// Python v0.0.9.8.1 SetRXAAGCSlope regression).  Do NOT change to double.
using fn_SetRXAAGCThresh_t     = void (*)(int channel, double thresh,
                                          double size, double rate);
using fn_SetRXAAGCSlope_t      = void (*)(int channel, int slope);
using fn_SetRXAPanelGain1_t    = void (*)(int channel, double gain);
// RX meter read-back: GetRXAMeter(channel, meterType) → value.
// meterType 0 = RXA_S_PK (peak signal strength, dBm-ish), 1 = RXA_S_AV.
// The in-passband S-meter source standard HF SDR apps read.
using fn_GetRXAMeter_t         = double (*)(int channel, int meterType);
// AGC max-gain read-back (out-param via pointer, WDSP convention).  This is
// the ceiling SetRXAAGCThresh derived from (thresh, size, rate) + slope — the
// number the reference shows on its AGC-T display (clamped -20..120 there).
// The Auto AGC-T knee sets the thresh; this is the resulting max-gain.
using fn_GetRXAAGCTop_t        = void (*)(int channel, double *max_agc);
// RX noise reduction (EMNR / "NR2") — the operator NR-mode surface.
// gainMethod 0..3 = Lyra NR Mode 1..4 (Wiener+SPP / Wiener / MMSE-LSA /
// trained); npeMethod 0=OSMS 1=MCRA; aeRun = AEPF anti-musical-noise
// post-filter; position 1 = after AGC (standard position).  See the
// project notes for the NR-mode mapping ported from the Python tree.
using fn_SetRXAEMNRRun_t        = void (*)(int channel, int run);
using fn_SetRXAEMNRgainMethod_t = void (*)(int channel, int method);
using fn_SetRXAEMNRnpeMethod_t  = void (*)(int channel, int method);
using fn_SetRXAEMNRaeRun_t      = void (*)(int channel, int run);
using fn_SetRXAEMNRPosition_t   = void (*)(int channel, int position);
// EMNR post-filter ("post2") — WDSP's dedicated anti-musical-noise
// stage.  Stock WDSP leaves it OFF; enabling it (with WDSP's
// gentle create defaults) cuts musical noise while MMSE-LSA preserves
// voice.  We drive post2Run with the AEPF control; params stay at the
// WDSP create defaults (0.15 / 0.15 / 5.0 / 0.12) — not overridden.
using fn_SetRXAEMNRpost2Run_t   = void (*)(int channel, int run);
// AGC time constants — set explicitly per mode so Fast/Med/Slow are
// audibly distinct (decay/hang in ms; hang threshold 0..100).
using fn_SetRXAAGCDecay_t         = void (*)(int channel, int decay);
using fn_SetRXAAGCHang_t          = void (*)(int channel, int hang);
using fn_SetRXAAGCHangThreshold_t = void (*)(int channel, int hangthreshold);
// AGC fixed-gain setter for mode 0 (FIXD / our "AGC OFF").  WDSP's
// mode-0 path multiplies samples by a static `fixed_gain` (no
// envelope tracking).  Without an explicit setter call, fixed_gain
// stays at the WDSP create-time default (1000.0 linear = +60 dB,
// RXA.c:366) which makes AGC OFF much LOUDER than the envelope
// modes that actively reduce gain on signals.  Reference's
// RXFixedAGC defaults to +20 dB; pushing 20.0 here matches that
// exactly.  Takes dB (the C function does `pow(10, fixed_agc/20)`
// internally — wcpAGC.c:549).
using fn_SetRXAAGCFixed_t         = void (*)(int channel, double fixed_agc);
// Auto-notch (ANF) + LMS line enhancer (ANR) — adaptive LMS predictors.
// Vals = (taps, delay, gain/adapt-rate, leakage).  ANF nulls carriers;
// ANR lifts periodic content (CW/tones) above broadband noise.  Both
// run inside the RXA chain (a run flag, no IQ-path splicing).  Standard
// defaults: taps 64, delay 16; ANF gain 1e-3 leak 1e-7, ANR gain 16e-4
// leak 1e-6.
using fn_SetRXAANFRun_t  = void (*)(int channel, int run);
using fn_SetRXAANFVals_t = void (*)(int channel, int taps, int delay,
                                    double gain, double leakage);
using fn_SetRXAANRRun_t  = void (*)(int channel, int run);
using fn_SetRXAANRVals_t = void (*)(int channel, int taps, int delay,
                                    double gain, double leakage);
// Manual notch — the NBP0 notched-bandpass database.  fcenter/fwidth in
// Hz (baseband offset from the tuned centre).  The NBP filter WDSP opens
// at channel start already uses a deep config (rectangular window,
// auto-increase, 2048-tap FIR), so these notches are sharp by construction.
// `active`/`run` are 4-byte ints on the wire (C# BOOL marshalling).
using fn_RXANBPAddNotch_t     = int  (*)(int channel, int notch,
                                         double fcenter, double fwidth,
                                         int active);
using fn_RXANBPEditNotch_t    = int  (*)(int channel, int notch,
                                         double fcenter, double fwidth,
                                         int active);
using fn_RXANBPDeleteNotch_t  = int  (*)(int channel, int notch);
using fn_RXANBPGetNumNotches_t = void (*)(int channel, int *nnotches);
using fn_RXANBPSetNotchesRun_t = void (*)(int channel, int run);
// All-mode squelch — routed by demod mode: SSQL (SSB/CW/DIG/SPEC),
// FMSQ (FM), AMSQ (AM/SAM/DSB).  One operator threshold maps onto each
// module's units; the inactive modules are disabled to avoid crosstalk.
using fn_SetRXASSQLRun_t       = void (*)(int channel, int run);
using fn_SetRXASSQLThreshold_t = void (*)(int channel, double threshold);
using fn_SetRXASSQLTauMute_t   = void (*)(int channel, double tau);
using fn_SetRXASSQLTauUnMute_t = void (*)(int channel, double tau);
using fn_SetRXAFMSQRun_t       = void (*)(int channel, int run);
using fn_SetRXAFMSQThreshold_t = void (*)(int channel, double threshold);
using fn_SetRXAAMSQRun_t       = void (*)(int channel, int run);
using fn_SetRXAAMSQThreshold_t = void (*)(int channel, double threshold);
using fn_SetRXAAMSQMaxTail_t   = void (*)(int channel, double tail);
// Noise blanker (EXT NOB-II) — an impulse blanker that runs on the raw
// IQ BEFORE the RXA chain.  Unlike the in-chain runs, the host creates it
// (own id space), then calls xnobEXT(id, in, out) on each IQ block before
// fexchange0.  Lower threshold = more aggressive blanking (light≈10,
// heavy≈3).  Defaults (slew/hang/adv 0.0001, backtau 0.020) are the
// bench-validated impulse-only tuning ported from the Python tree.
using fn_create_nobEXT_t      = void (*)(int id, int run, int mode,
                                         int buffsize, double samplerate,
                                         double slewtime, double hangtime,
                                         double advtime, double backtau,
                                         double threshold);
using fn_destroy_nobEXT_t     = void (*)(int id);
using fn_xnobEXT_t            = void (*)(int id, double *in, double *out);
using fn_SetEXTNOBRun_t       = void (*)(int id, int run);
using fn_SetEXTNOBThreshold_t = void (*)(int id, double thresh);
// APF — CW audio peaking filter (a single in-chain biquad).  Centre on
// the CW pitch, narrow bandwidth, peak gain (dB→linear at the call site).
// Mode-gated to CWU/CWL by the engine.
using fn_SetRXABiQuadRun_t       = void (*)(int channel, int run);
using fn_SetRXABiQuadFreq_t      = void (*)(int channel, double freq);
using fn_SetRXABiQuadBandwidth_t = void (*)(int channel, double bw);
using fn_SetRXABiQuadGain_t      = void (*)(int channel, double gain);

// Step 5: WDSP spectral analyzer (panadapter source).  Standard
// WDSP analyzer pipeline — XCreateAnalyzer + SetAnalyzer to
// configure, Spectrum0 to feed IQ (interleaved doubles, like
// fexchange0), GetPixels to retrieve a display-width dB array.
// Signatures verified against
// wdsp/analyzer.h.  GetPixels writes FLOAT pixels (dOUTREAL=float);
// Spectrum0 takes DOUBLE IQ.  Do NOT swap those.
using fn_XCreateAnalyzer_t        = void (*)(int disp, int *success,
                                             int m_size, int m_LO,
                                             int m_stitch,
                                             char *app_data_path);
using fn_DestroyAnalyzer_t        = void (*)(int disp);
// SIGNATURE VERIFIED against wdsp/analyzer.c:1189 (the C# param NAMES
// in PanDisplay.cs are misleading): param 5 `flp` is an int* vector
// (one high-side-LO flag per FFT), NOT an int — passing an int there
// is a null-pointer deref crash.  fsc_lin/fsc_hin are DOUBLE, not int.
using fn_SetAnalyzer_t            = void (*)(int disp, int n_pixout,
                                             int n_fft, int typ,
                                             int *flp, int sz, int bf_sz,
                                             int win_type, double pi,
                                             int ovrlp, int clp,
                                             double fsc_lin, double fsc_hin,
                                             int n_pix, int n_stch,
                                             int calset, double fmin,
                                             double fmax, int max_w);
using fn_Spectrum0_t              = void (*)(int run, int disp, int ss,
                                             int LO, double *pbuff);
using fn_GetPixels_t              = void (*)(int disp, int pixout,
                                             float *pix, int *flag,
                                             double *pixel_ref);
using fn_SetDisplayDetectorMode_t = void (*)(int disp, int pixout, int mode);
using fn_SetDisplayAverageMode_t  = void (*)(int disp, int pixout, int mode);
using fn_SetDisplayNumAverage_t   = void (*)(int disp, int pixout, int num);
// Exponential back-multiplier for recursive display averaging.  Paired
// with SetDisplayNumAverage; both derived from a time constant tau:
//   mult = exp(-1 / (frame_rate * tau)),  num = frame_rate * tau.
using fn_SetDisplayAvBackmult_t   = void (*)(int disp, int pixout,
                                             double mult);

// ============================================================
// TX-1 (design v2 §5.2): WDSP TXA cdef surface.
// ============================================================
// SSB voice TX requires the TXA chain setters listed below.
// Open/Close/SetChannelState/fexchange0 are SHARED with RX
// (already cdef'd above) — TXA channels open via OpenChannel
// with type=1 (vs RX type=0).  Signatures verified against
// wdsp/TXA.c + wdsp/wcpAGC.c + wdsp/iir.c on 2026-05-29.
//
// **DELIBERATELY OMITTED** (do NOT add):
//  • `SetTXABandpassRun` — the §15.23 trap.  Verified zero
//    call sites tree-wide in the working C-source reference;
//    calling it toggles stale-`bp1` cascading wrong-sideband /
//    no-output.  bp0 (the SSB sideband selector) is always-on
//    by `create_txa` and reconfigured via `SetTXABandpassFreqs`
//    / `SetTXAMode` only; bp1 stays compressor-aux-only.
//  • `SetTXAPanelSelect` — `create_txa` default `inselect=2`
//    routes I=mic + Q=0 correctly via patchpanel case-0 math;
//    verified zero call sites in the C-source reference;
//    calling it would only swap I/Q for balance-test mode
//    (`copy=3`) — never wanted.
//  • `SetTXAALCThresh` — does NOT exist.  ALC ceiling is
//    governed solely by `SetTXAALCMaxGain`.  (Mis-listed in
//    earlier doc drafts via confusion with RX `SetRXAAGCThresh`.)
//
// **Symbol-name hygiene** (pre-cdef-audit lesson, §15.18):
//  • UPPERCASE `PHROT` (NOT `PhRot`) per wdsp/iir.c:665+.
//  • `SetTXAPHROTCorner` (NOT `Freq`).
//  • Run-state suffix on AGC/leveler is `St` (NOT `Run`):
//    `SetTXAALCSt`, `SetTXALevelerSt` per wdsp/wcpAGC.c.
// Signature-verified row-by-row against the WDSP C source
// before this cdef block was committed — see commit message.

// SSB sideband selector + mode.  Per §5.3 init order, freqs
// MUST be pushed BEFORE the mode setter to avoid a ~2.6 ms
// window where bp0 runs on stale defaults (TXA.c:786 calls
// TXASetupBPFilters with whatever f_low/f_high it has).
using fn_SetTXAMode_t           = void (*)(int channel, int mode);
using fn_SetTXABandpassFreqs_t  = void (*)(int channel,
                                           double low, double high);

// PHROT (phase rotator) — default ON (matches the verified
// working reference),
// operator Settings toggle.  4-stage all-pass network that
// flattens speech PEP-to-PAR by ~3-4 dB.  Defaults per
// create_txa: fc=338 Hz, nstages=8, run=0; we run=1 in init.
using fn_SetTXAPHROTRun_t       = void (*)(int channel, int run);
using fn_SetTXAPHROTCorner_t    = void (*)(int channel, double corner);
using fn_SetTXAPHROTNstages_t   = void (*)(int channel, int nstages);

// ALC — always-on splatter protection.  attack/decay/hang are
// **int milliseconds** per wcpAGC.c:578-610 (do NOT make these
// double — the SetRXAAGCSlope register-class regression in
// the Python tree per §14.9 / v0.0.9.8.1 is the canonical
// wrong-cdef-type bug class).  MaxGain alone governs the ALC
// ceiling (no separate Thresh; see omitted list above).
using fn_SetTXAALCAttack_t      = void (*)(int channel, int attack_ms);
using fn_SetTXAALCDecay_t       = void (*)(int channel, int decay_ms);
using fn_SetTXAALCHang_t        = void (*)(int channel, int hang_ms);
using fn_SetTXAALCMaxGain_t     = void (*)(int channel, double max_gain);
using fn_SetTXAALCSt_t          = void (*)(int channel, int run);

// Leveler — operator opt-in (default OFF per §6.3).  Ships
// wired with a Settings UI on/off toggle.  Same int-ms
// timing convention as ALC.  Top = max-gain ceiling.
using fn_SetTXALevelerAttack_t  = void (*)(int channel, int attack_ms);
using fn_SetTXALevelerDecay_t   = void (*)(int channel, int decay_ms);
using fn_SetTXALevelerHang_t    = void (*)(int channel, int hang_ms);
using fn_SetTXALevelerTop_t     = void (*)(int channel, double max_gain);
using fn_SetTXALevelerSt_t      = void (*)(int channel, int run);

// Panel gain — operator mic gain.  WDSP `create_panel`
// default is gain1=1.0 (NOT 4.0 as earlier docs claimed);
// pin explicitly for clarity + centralised operator-facing
// trim.  Hardware preamp (`mic_boost`) is a separate path
// via EP2 C&C case 10 C2 bit 0 (see hl2_stream.cpp).
using fn_SetTXAPanelGain1_t     = void (*)(int channel, double gain);

// TXA meter readout — PWR / SWR / MIC / ALC / etc.  Returns
// the current level for the requested meter type.  The
// concrete enum (TxaMeterType) lives in wdsp_engine; the
// cdef stays untyped because WDSP exposes it as `int`.
using fn_GetTXAMeter_t          = double (*)(int channel, int meter_type);
// WDSP TX sip1 reader (siphon.c:268-282 TXAGetaSipF1).  Reserved for
// future phase-display or external-pull use; NOT used by Task #44
// Phase 2 (we use the reference auto-feed mechanism via
// TXASetSipMode + TXASetSipDisplay instead — see below).  Kept
// declared+resolved so future code can use it without re-wiring.
using fn_TXAGetaSipF1_t         = void (*)(int channel, float *out, int size);
// WDSP TX sip1 auto-feed mode setters (siphon.c:235-251).  This is
// the REFERENCE-FAITHFUL mechanism for feeding the TX panadapter
// analyzer: call TXASetSipMode(channel, 1) to put the siphon in
// "internally call Spectrum0 every xsiphon" mode, and
// TXASetSipDisplay(channel, disp) to point at the target analyzer
// disp id.  After that, the WDSP TX channel feeds the analyzer for
// free every xtxa cycle (which only fires while the TX channel is
// running = during MOX).  Zero per-block CPU cost on Lyra's side;
// matches cmaster.cs:539-540 exactly.  Pre-iqc tap point (TXA.c:586
// xsiphon is upstream of xiqc at TXA.c:587), so the panadapter
// trace stays clean whether PureSignal is on or off — same posture
// as the reference's TX panadapter.
using fn_TXASetSipMode_t        = void (*)(int channel, int mode);
using fn_TXASetSipDisplay_t     = void (*)(int channel, int disp);
// WDSP per-analyzer sample-rate setter (analyzer.c:1905).  Tells the
// analyzer what rate to assume for incoming samples — separate from
// SetAnalyzer (which configures FFT shape).  Thetis calls it in
// SpecHPSDR.SampleRate setter (specHPSDR.cs:453).  Required when the
// analyzer's feed rate changes between RX/TX MOX edges.
using fn_SetDisplaySampleRate_t = void (*)(int disp, int rate);
// WDSP polyphase float-vector resampler (resample.c).  Opaque void*
// handle returned by create_resampleFV; xresampleFV consumes
// `numsamps` input samples and writes the resulting count into
// `*outsamps` (≤ ceil(numsamps * out_rate / in_rate) + slack).
using fn_create_resampleFV_t   = void* (*)(int in_rate, int out_rate);
using fn_xresampleFV_t         = void  (*)(float *input, float *output,
                                           int numsamps, int *outsamps,
                                           void *ptr);
using fn_destroy_resampleFV_t  = void  (*)(void *ptr);

// ============================================================
// Stage B (Thetis ChannelMaster port — AAMix): WDSP polyphase
// complex-double resampler (resample.c — DIFFERENT entry points
// from the float-vector variant above).  Used by the ported
// AAMix per-input resampler bank (one create_resample per stream,
// xresample on every push, destroy on teardown, flush on ring
// reset).  Signatures verified against wdsp/resample.h:60-70.
// All 4 are __declspec(dllexport) in the upstream WDSP source —
// confirmation that the bundled wdsp.dll exports them lands via
// the resolveSymbols() probe at app load (Stage B.0 gate).
//
// The corresponding `resample` struct layout (with `in`/`out`/
// `size` fields written directly by the consumer per the WDSP
// public ABI in resample.h) is replicated under
// src/wire/resample.h (P0.c direct port) with full GPL attribution —
// required because the accessor setters
// (setBuffers_resample / setSize_resample / setInRate_resample /
// setOutRate_resample) are `extern` only, NOT
// __declspec(dllexport), so they cannot be resolved across the
// DLL boundary.  Direct field access is the upstream-faithful
// pattern used by aamix.c itself (e.g. line 249
// `a->rsmp[stream]->in = data;`).
using fn_create_resample_t   = void* (*)(int run, int size,
                                         double *in, double *out,
                                         int in_rate, int out_rate,
                                         double fc, int ncoef,
                                         double gain);
using fn_destroy_resample_t  = void  (*)(void *ptr);
using fn_flush_resample_t    = void  (*)(void *ptr);
using fn_xresample_t         = int   (*)(void *ptr);
} // extern "C"

// Resolved function pointers.  Step 3b populates these via
// GetProcAddress at load() time; nullptr until then.  Step 3c+
// reads them via WdspNative::api().
struct WdspApi {
    fn_OpenChannel_t         OpenChannel         = nullptr;
    fn_CloseChannel_t        CloseChannel        = nullptr;
    fn_SetChannelState_t     SetChannelState     = nullptr;
    fn_fexchange0_t          fexchange0          = nullptr;
    fn_SetRXAMode_t          SetRXAMode          = nullptr;
    fn_RXASetPassband_t      RXASetPassband      = nullptr;
    fn_SetRXAAGCMode_t       SetRXAAGCMode       = nullptr;
    fn_SetRXAPanelBinaural_t SetRXAPanelBinaural = nullptr;
    fn_SetRXAShiftFreq_t     SetRXAShiftFreq     = nullptr;  // #174 CTUNE
    fn_SetRXAShiftRun_t      SetRXAShiftRun      = nullptr;  // #174 CTUNE
    fn_RXANBPSetShiftFrequency_t RXANBPSetShiftFrequency = nullptr;  // #174 CTUNE notch track
    fn_RXASetMP_t            RXASetMP            = nullptr;  // #159 RX filter type
    fn_TXASetMP_t            TXASetMP            = nullptr;  // #159 TX filter type
    fn_WDSPwisdom_t          WDSPwisdom          = nullptr;
    fn_wisdom_get_status_t   wisdom_get_status   = nullptr;   // optional
    fn_init_impulse_cache_t    init_impulse_cache    = nullptr;   // optional
    fn_read_impulse_cache_t    read_impulse_cache    = nullptr;   // optional
    fn_save_impulse_cache_t    save_impulse_cache    = nullptr;   // optional
    fn_use_impulse_cache_t     use_impulse_cache     = nullptr;   // optional
    fn_destroy_impulse_cache_t destroy_impulse_cache = nullptr;   // optional
    fn_SetRXAAGCThresh_t     SetRXAAGCThresh     = nullptr;
    fn_SetRXAAGCSlope_t      SetRXAAGCSlope      = nullptr;
    fn_SetRXAPanelGain1_t    SetRXAPanelGain1    = nullptr;
    fn_GetRXAMeter_t         GetRXAMeter         = nullptr;
    fn_GetRXAAGCTop_t        GetRXAAGCTop        = nullptr;
    fn_SetRXAEMNRRun_t        SetRXAEMNRRun        = nullptr;
    fn_SetRXAEMNRgainMethod_t SetRXAEMNRgainMethod = nullptr;
    fn_SetRXAEMNRnpeMethod_t  SetRXAEMNRnpeMethod  = nullptr;
    fn_SetRXAEMNRaeRun_t      SetRXAEMNRaeRun      = nullptr;
    fn_SetRXAEMNRPosition_t   SetRXAEMNRPosition   = nullptr;
    fn_SetRXAEMNRpost2Run_t   SetRXAEMNRpost2Run   = nullptr;
    fn_SetRXAAGCDecay_t         SetRXAAGCDecay         = nullptr;
    fn_SetRXAAGCHang_t          SetRXAAGCHang          = nullptr;
    fn_SetRXAAGCFixed_t         SetRXAAGCFixed         = nullptr;
    fn_SetRXAAGCHangThreshold_t SetRXAAGCHangThreshold = nullptr;
    fn_SetRXAANFRun_t   SetRXAANFRun   = nullptr;
    fn_SetRXAANFVals_t  SetRXAANFVals  = nullptr;
    fn_SetRXAANRRun_t   SetRXAANRRun   = nullptr;
    fn_SetRXAANRVals_t  SetRXAANRVals  = nullptr;
    fn_RXANBPAddNotch_t      RXANBPAddNotch      = nullptr;
    fn_RXANBPEditNotch_t     RXANBPEditNotch     = nullptr;
    fn_RXANBPDeleteNotch_t   RXANBPDeleteNotch   = nullptr;
    fn_RXANBPGetNumNotches_t RXANBPGetNumNotches = nullptr;
    fn_RXANBPSetNotchesRun_t RXANBPSetNotchesRun = nullptr;
    fn_SetRXASSQLRun_t       SetRXASSQLRun       = nullptr;
    fn_SetRXASSQLThreshold_t SetRXASSQLThreshold = nullptr;
    fn_SetRXASSQLTauMute_t   SetRXASSQLTauMute   = nullptr;
    fn_SetRXASSQLTauUnMute_t SetRXASSQLTauUnMute = nullptr;
    fn_SetRXAFMSQRun_t       SetRXAFMSQRun       = nullptr;
    fn_SetRXAFMSQThreshold_t SetRXAFMSQThreshold = nullptr;
    fn_SetRXAAMSQRun_t       SetRXAAMSQRun       = nullptr;
    fn_SetRXAAMSQThreshold_t SetRXAAMSQThreshold = nullptr;
    fn_SetRXAAMSQMaxTail_t   SetRXAAMSQMaxTail   = nullptr;
    fn_create_nobEXT_t       create_nobEXT       = nullptr;
    fn_destroy_nobEXT_t      destroy_nobEXT      = nullptr;
    fn_xnobEXT_t             xnobEXT             = nullptr;
    fn_SetEXTNOBRun_t        SetEXTNOBRun        = nullptr;
    fn_SetEXTNOBThreshold_t  SetEXTNOBThreshold  = nullptr;
    fn_SetRXABiQuadRun_t       SetRXABiQuadRun       = nullptr;
    fn_SetRXABiQuadFreq_t      SetRXABiQuadFreq      = nullptr;
    fn_SetRXABiQuadBandwidth_t SetRXABiQuadBandwidth = nullptr;
    fn_SetRXABiQuadGain_t      SetRXABiQuadGain      = nullptr;
    // Step 5: spectral analyzer (panadapter).
    fn_XCreateAnalyzer_t        XCreateAnalyzer        = nullptr;
    fn_DestroyAnalyzer_t        DestroyAnalyzer        = nullptr;
    fn_SetAnalyzer_t            SetAnalyzer            = nullptr;
    fn_Spectrum0_t              Spectrum0              = nullptr;
    fn_GetPixels_t              GetPixels              = nullptr;
    fn_SetDisplayDetectorMode_t SetDisplayDetectorMode = nullptr;
    fn_SetDisplayAverageMode_t  SetDisplayAverageMode  = nullptr;
    fn_SetDisplayNumAverage_t   SetDisplayNumAverage   = nullptr;
    fn_SetDisplayAvBackmult_t   SetDisplayAvBackmult   = nullptr;
    // TX-1 (design v2 §5.2): WDSP TXA surface for the SSB voice
    // modulator chain.  All nullptr until WdspNative::load() succeeds;
    // TxChannel (component 2) reads through wdsp.api().SetTXA*.  See
    // the typedef block above for the deliberately-omitted set.
    fn_SetTXAMode_t           SetTXAMode           = nullptr;
    fn_SetTXABandpassFreqs_t  SetTXABandpassFreqs  = nullptr;
    fn_SetTXAPHROTRun_t       SetTXAPHROTRun       = nullptr;
    fn_SetTXAPHROTCorner_t    SetTXAPHROTCorner    = nullptr;
    fn_SetTXAPHROTNstages_t   SetTXAPHROTNstages   = nullptr;
    fn_SetTXAALCAttack_t      SetTXAALCAttack      = nullptr;
    fn_SetTXAALCDecay_t       SetTXAALCDecay       = nullptr;
    fn_SetTXAALCHang_t        SetTXAALCHang        = nullptr;
    fn_SetTXAALCMaxGain_t     SetTXAALCMaxGain     = nullptr;
    fn_SetTXAALCSt_t          SetTXAALCSt          = nullptr;
    fn_SetTXALevelerAttack_t  SetTXALevelerAttack  = nullptr;
    fn_SetTXALevelerDecay_t   SetTXALevelerDecay   = nullptr;
    fn_SetTXALevelerHang_t    SetTXALevelerHang    = nullptr;
    fn_SetTXALevelerTop_t     SetTXALevelerTop     = nullptr;
    fn_SetTXALevelerSt_t      SetTXALevelerSt      = nullptr;
    fn_SetTXAPanelGain1_t     SetTXAPanelGain1     = nullptr;
    fn_GetTXAMeter_t          GetTXAMeter          = nullptr;
    fn_TXAGetaSipF1_t         TXAGetaSipF1         = nullptr;  // reserved
    fn_TXASetSipMode_t        TXASetSipMode        = nullptr;  // Task #44 Phase 2 auto-feed (reference mechanism)
    fn_TXASetSipDisplay_t     TXASetSipDisplay     = nullptr;  // Task #44 Phase 2 auto-feed (reference mechanism)
    fn_SetDisplaySampleRate_t SetDisplaySampleRate = nullptr;  // Task #44 Phase 2 rate setter
    // WDSP polyphase float-vector resampler (resample.c PORT block
    // 342-361).  Used by TciServer to resample inbound non-48 kHz
    // TX_AUDIO_STREAM frames to the TXA input rate (48 kHz) — the
    // reference (cmaster.cs:1431-1473 resampleTCITxSamples) uses
    // the exact same three entry points, lazy-created/destroyed
    // on rate changes.
    fn_create_resampleFV_t    create_resampleFV    = nullptr;
    fn_xresampleFV_t          xresampleFV          = nullptr;
    fn_destroy_resampleFV_t   destroy_resampleFV   = nullptr;
    // Complex-double resampler (resample.c) cdefs.  The P0.c
    // direct-port AAMix at src/wire/AAMix.* resolves these through
    // wire/wdspcalls.h instead (the operator-approved seam); these
    // WdspApi entries remain for any future non-ported consumer.
    // See typedef block header for the public-resample-struct
    // rationale.
    fn_create_resample_t      create_resample      = nullptr;
    fn_destroy_resample_t     destroy_resample     = nullptr;
    fn_flush_resample_t       flush_resample       = nullptr;
    fn_xresample_t            xresample            = nullptr;
};

class WdspNative : public QObject {
    Q_OBJECT
    // Exposed to QML as a context property so the operator can
    // SEE the load status in the UI (Step 3a polish — log line in
    // the existing log panel; richer surfacing lands later).
    Q_PROPERTY(bool    loaded     READ isLoaded     NOTIFY loadedChanged)
    Q_PROPERTY(QString loadedFrom READ loadedFrom   NOTIFY loadedChanged)
    Q_PROPERTY(QString loadError  READ loadError    NOTIFY loadedChanged)

public:
    explicit WdspNative(QObject *parent = nullptr);
    ~WdspNative() override;

    bool    isLoaded()   const { return handle_ != nullptr; }
    QString loadedFrom() const { return loadedFrom_; }
    QString loadError()  const { return loadError_; }

    // Attempt to load wdsp.dll from `<lyra.exe directory>/_native/`.
    // Idempotent: subsequent calls after success are no-ops.  Safe
    // to call before main window construction.  Returns true on
    // success, false on failure (operator sees `loadError` for the
    // Windows error message).
    bool load();

    // Force-unload (testing / shutdown).  Generally we let the OS
    // do it at process exit.
    void unload();

    // Access the resolved function-pointer table.  Step 3c+ uses
    // this via `wdsp.api().OpenChannel(...)` etc.  All pointers
    // are nullptr until load() succeeds.
    const WdspApi &api() const { return api_; }

    // Step 3c-i: FFTW WISDOM plumbing.  WDSP plans every internal
    // FFT at FFTW_PATIENT (the most expensive planner tier).  With
    // no cached wisdom file, FFTW_PATIENT re-runs in-process on
    // every OpenChannel — multi-minute stall, with the UI frozen.
    // ensureWisdom() resolves that by:
    //
    //   (a) computing Lyra-C++-PRIVATE wisdom dir
    //       %APPDATA%\N8SDR\Lyra-cpp\fftw\  — note the `-cpp` suffix
    //       so we never share with Python Lyra (which uses .../Lyra/
    //       fftw/, leave it untouched per CLAUDE.md §15.26
    //       isolation-by-directory rule);
    //
    //   (b) if `wdspWisdom00` exists in that dir, calling
    //       api().WDSPwisdom(<dir>) IN-PROCESS — fast import,
    //       <100 ms typical;
    //
    //   (c) if it does not, building IN-PROCESS on a worker thread
    //       (the Thetis model, radio.cs:135) into a temp dir, then
    //       atomically publishing the result — NO self-spawned child
    //       (the old `lyra.exe --build-wisdom` pattern tripped
    //       antivirus SONAR: an unsigned exe launching a copy of
    //       itself).  WDSPwisdom()'s internal AllocConsole() is tamed
    //       (pre-owned hidden console + stdout restore) so nothing
    //       pops up on the `--windowed` build; a modal 'please wait'
    //       notice shows only if the build is actually slow.  See
    //       runWisdomCall().
    //
    // Returns true on success.  Status messages stream through
    // logWisdom() (lyra-log.txt + a dedicated wisdom.log).
    bool ensureWisdom();

    // Returns the path Lyra C++ uses for the wisdom cache.  Public
    // so test code / future Settings UI can read it.
    static QString wisdomDir();

    // Full path to the cached-plans file inside wisdomDir().
    static QString wisdomFilePath();

    // True when a built wisdom cache exists (next launch will import,
    // not rebuild).
    static bool wisdomExists();

    // Delete the cached plans so the NEXT launch rebuilds them from
    // scratch (used by the Settings "rebuild FFT optimization" button —
    // e.g. after a CPU change).  Returns true if the file is gone
    // afterwards (already-absent counts as success).
    static bool deleteWisdom();

signals:
    void loadedChanged();
    void logLine(QString line);

private:
    bool resolveSymbols();
    void emitLog(const QString &line);   // mirror logLine -> qInfo console
    // Wisdom-build diagnostics.  Unlike emitLog (qInfo, dropped by the
    // in-app LogBuffer unless the operator enabled verbose logging),
    // this logs at qWarning so the lines ALWAYS reach lyra-log.txt, and
    // also appends them to a dedicated wisdom.log next to the cache dir
    // -- so a field "rebuilds every launch" report is self-diagnosing
    // with zero setup.
    void logWisdom(const QString &line);

    // Run api_.WDSPwisdom(<callDir>/) on a worker thread (the Thetis
    // in-process model) with WDSP's AllocConsole tamed, keeping the GUI
    // responsive.  Returns WDSPwisdom's code (0=import, 1=rebuilt).  Used
    // by ensureWisdom() for both the cache import (fast path) and the
    // from-scratch build.  showModalImmediately: true for a known build
    // (the 'please wait' notice appears at once, since the caller has
    // deferred the main window); false for the import (notice appears
    // only if it unexpectedly rebuilds a rejected cache).
    int runWisdomCall(const QString &callDir, bool showModalImmediately = false);

    // WDSP impulse-cache lifecycle (reference radio.cs CreateDSP /
    // DestroyDSP parity).  init runs inside ensureWisdom() AFTER the
    // WDSPwisdom call + BEFORE any OpenChannel: init_impulse_cache(1)
    // (default-on, no UI toggle for now) then read_impulse_cache() --
    // UNLESS the FFT wisdom was just rebuilt, in which case the stale
    // impulse_cache.dat is deleted + the read skipped (the cached
    // impulses were hashed against the old plan world).  The matching
    // save_impulse_cache() + destroy_impulse_cache() run in unload()
    // (fires from ~WdspNative during app destruction, after every
    // aboutToQuit channel-close handler).  impulseCacheInited_ gates
    // the save so a never-inited / exports-absent load never touches it.
    void initImpulseCache(const QString &dir, bool wisdomRebuilt);
    bool impulseCacheInited_ = false;

    // We deliberately keep this as a `void*` so the header doesn't
    // drag windows.h through every translation unit that includes
    // it.  Cast to HMODULE in the cpp.
    void   *handle_ = nullptr;
    QString loadedFrom_;
    QString loadError_;
    WdspApi api_;
};

} // namespace lyra::dsp
