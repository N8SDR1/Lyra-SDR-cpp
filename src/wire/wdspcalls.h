// Lyra-cpp — wdspcalls.h
//
// THE single operator-approved linkage seam for the ChannelMaster
// direct port (operator decision 2026-06-09, "function-pointer
// table" option):
//
//   The reference's ChannelMaster project STATICALLY LINKS the WDSP
//   library and calls its exports directly (OpenChannel, fexchange0,
//   xresample, pscc, ...).  Lyra-cpp loads wdsp.dll at runtime
//   (LoadLibrary in lyra::dsp::WdspNative), so static linking is
//   physically unavailable.  This file declares file-scope function
//   pointers carrying the WDSP exports' EXACT names, resolved once
//   from the already-loaded module.  Every ported ChannelMaster call
//   site therefore reads byte-identical to the reference source —
//   only this declaration-site plumbing differs.
//
// RULES FOR THIS TABLE (locked):
//   1. Every signature below is harvested VERBATIM from the WDSP
//      source headers / definition sites, cited per entry.  No
//      signature is ever guessed — a wrong calling-convention or
//      parameter type is a silent register-class bug on x64.
//   2. A symbol is added ONLY when ported code first calls it,
//      with its citation.  No speculative entries.
//   3. PureSignal is a committed feature (operator 2026-06-09):
//      the full PS entry-point family is included now so the v0.3
//      PS work lands on a ready table.  All PS exports verified
//      present in the bundled wdsp.dll via dumpbin 2026-06-09.
//
// API attribution: the names + signatures are the WDSP API,
// Copyright (C) 2014-2024 Warren Pratt, NR0V, GPL v2 or later
// (Source/wdsp/ in the openHPSDR Thetis 2.10.3.13 tree).  Lyra-cpp
// is GPL v3+; see NOTICE.md.

#pragma once

#include "wire/resample.h"   // RESAMPLE — the WDSP public resampler ABI typedef
#include "wire/cmcomm.h"     // EER — the WDSP public eer ABI typedef (verbatim at P2.a)

namespace lyra::wire {

// ---- channel lifecycle -------------------------------------------------
// wdsp/channel.h (decl lines cited in wdspcalls.cpp X-table):
//   PORT void OpenChannel (int channel, int in_size, int dsp_size,
//       int input_samplerate, int dsp_rate, int output_samplerate,
//       int type, int state, double tdelayup, double tslewup,
//       double tdelaydown, double tslewdown, int bfo);
//   PORT void CloseChannel (int channel);
//   PORT int  SetChannelState (int channel, int state, int dmode);
//   PORT void SetType (int channel, int type);
//   PORT void SetInputBuffsize (int channel, int in_size);
//   PORT void SetDSPBuffsize (int channel, int dsp_size);
//   PORT void SetInputSamplerate (int channel, int samplerate);
//   PORT void SetDSPSamplerate (int channel, int samplerate);
//   PORT void SetOutputSamplerate (int channel, int samplerate);
extern void (*OpenChannel)(int channel, int in_size, int dsp_size,
                           int input_samplerate, int dsp_rate,
                           int output_samplerate, int type, int state,
                           double tdelayup, double tslewup,
                           double tdelaydown, double tslewdown, int bfo);
extern void (*CloseChannel)(int channel);
extern int  (*SetChannelState)(int channel, int state, int dmode);
extern void (*SetType)(int channel, int type);
extern void (*SetInputBuffsize)(int channel, int in_size);
extern void (*SetDSPBuffsize)(int channel, int dsp_size);
extern void (*SetInputSamplerate)(int channel, int samplerate);
extern void (*SetDSPSamplerate)(int channel, int samplerate);
extern void (*SetOutputSamplerate)(int channel, int samplerate);

// ---- buffer exchange ---------------------------------------------------
// wdsp/iobuffs.h:90:
//   PORT void fexchange0 (int channel, double* in, double* out, int* error);
extern void (*fexchange0)(int channel, double* in, double* out, int* error);

// ---- resampler (aamix.c dependency) -------------------------------------
// wdsp/resample.h (dllexport set, complex-double version).  RESAMPLE
// is the PUBLIC struct typedef ported verbatim at wire/resample.h —
// the reference ChannelMaster pokes rsmp->run/->in/->out/->size
// directly, so the type is public ABI, not opaque (the earlier
// "private typedef" note here was wrong and is corrected):
//   RESAMPLE create_resample (int run, int size, double* in, double* out,
//       int in_rate, int out_rate, double fc, int ncoef, double gain);
//   void destroy_resample (RESAMPLE a);
//   void flush_resample (RESAMPLE a);       // resample.c:113-118
//   int  xresample (RESAMPLE a);
extern RESAMPLE (*create_resample)(int run, int size, double* in, double* out,
                                   int in_rate, int out_rate, double fc,
                                   int ncoef, double gain);
extern void (*destroy_resample)(RESAMPLE a);
extern void (*flush_resample)(RESAMPLE a);
extern int  (*xresample)(RESAMPLE a);

// ---- VAC adaptive resampler — rmatchV (#158 ivac.c dependency) ----------
// wdsp/rmatch.h dllexport set.  rmatchV is the two-clock (radio crystal
// vs PC soundcard) drift-correcting resampler ivac.c uses for both VAC
// directions: an internal ring + varsamp + a feed-forward/proportional
// control loop.  The RMATCH handle is OPAQUE — ivac.c only ever passes
// it as void* (unlike RESAMPLE, whose struct ChannelMaster pokes), so no
// struct typedef is ported; the handle is void*.  Confirmed exported by
// the bundled wdsp.dll (dumpbin /exports, 2026-06-14 Stage 0 link check).
//   void* create_rmatchV (in_size, out_size, nom_inrate, nom_outrate,
//                          ringsize, var);
//   void  xrmatchIN  (void* b, double* in);   // push producer side
//   void  xrmatchOUT (void* b, double* out);  // pull consumer side
extern void* (*create_rmatchV)(int in_size, int out_size, int nom_inrate,
                               int nom_outrate, int ringsize, double var);
extern void (*destroy_rmatchV)(void* ptr);
extern void (*xrmatchIN)(void* b, double* in);
extern void (*xrmatchOUT)(void* b, double* out);
extern void (*forceRMatchVar)(void* b, int force, double fvar);
extern void (*getRMatchDiags)(void* b, int* underflows, int* overflows,
                              double* var, int* ringsize, int* nring);
extern void (*resetRMatchDiags)(void* b);
extern void (*setRMatchInsize)(void* ptr, int insize);
extern void (*setRMatchOutsize)(void* ptr, int outsize);
extern void (*setRMatchNomInrate)(void* ptr, int nom_inrate);
extern void (*setRMatchNomOutrate)(void* ptr, int nom_outrate);
extern void (*setRMatchRingsize)(void* ptr, int ringsize);
extern void (*setRMatchFeedbackGain)(void* b, double feedback_gain);
extern void (*setRMatchSlewTime)(void* b, double slew_time);
extern void (*setRMatchSlewTime1)(void* b, double slew_time);
extern void (*setRMatchPropRingMin)(void* ptr, int prop_min);
extern void (*setRMatchPropRingMax)(void* ptr, int prop_max);
extern void (*setRMatchFFRingMin)(void* ptr, int ff_ringmin);
extern void (*setRMatchFFRingMax)(void* ptr, int ff_ringmax);
extern void (*setRMatchFFAlpha)(void* ptr, double ff_alpha);

// ---- display analyzer (create_xmtr / xcmaster dependency) ---------------
// wdsp/analyzer.h:175-184, :205-206:
//   void XCreateAnalyzer (int disp, int *success, int m_size, int m_LO,
//       int m_stitch, char *app_data_path);
//   void DestroyAnalyzer (int disp);
//   void Spectrum0 (int run, int disp, int ss, int LO, double* pbuff);
extern void (*XCreateAnalyzer)(int disp, int* success, int m_size,
                               int m_LO, int m_stitch, char* app_data_path);
extern void (*DestroyAnalyzer)(int disp);
extern void (*Spectrum0)(int run, int disp, int ss, int LO, double* pbuff);

// ---- eer (create_xmtr / xcmaster / SetXmtrChannelOutrate dependency) ----
// wdsp/eer.h:51-75 (dllexport set; struct verbatim at wire/cmcomm.h
// since P2.a).  All five verified present in the bundled wdsp.dll
// exports (PE export-table scan 2026-06-12).  The reference creates
// eer run=0 on HL2 (no EER hardware) — the object exists so the
// `peer->run` derefs in sendOutbound/sendProtocol1Samples are valid:
//   eer.h:51  EER  create_eer (int run, int size, double* in, double* out,
//                 double* outM, int rate, double mgain, double pgain,
//                 int rundelays, double mdelay, double pdelay, int amiq);
//   eer.h:53  void destroy_eer (EER a);
//   eer.h:57  void xeer (EER a);
//   eer.h:73  void pSetEERSize (EER a, int size);
//   eer.h:75  void pSetEERSamplerate (EER a, int rate);
extern EER  (*create_eer)(int run, int size, double* in, double* out,
                          double* outM, int rate, double mgain, double pgain,
                          int rundelays, double mdelay, double pdelay, int amiq);
extern void (*destroy_eer)(EER a);
extern void (*xeer)(EER a);
extern void (*pSetEERSize)(EER a, int size);
extern void (*pSetEERSamplerate)(EER a, int rate);

// ---- TXA PostGen / gen1 (TUN re-home consumer, P4 — see
// docs/architecture/P4_WIRE_LIVE_DESIGN.md §4) ------------------------------
// The output-side TXA tone generator the reference uses for the TUN
// carrier + the PS two-tone.  Pre-cdef audit per the §15.18
// discipline (2026-06-12): NO header declares these — they are
// PORT-exported definition sites in wdsp/gen.c only (the reference's
// C# side P/Invokes them), so the citations are the gen.c definition
// lines; exact casing + presence verified against the bundled
// wdsp.dll export table (PE scan, 533 exports); signatures harvested
// verbatim from the definition bodies.  TT (two-tone) pair included
// per table rule #3 (PureSignal is a committed feature; the PS
// calibration drive is the two-tone).
//   gen.c:784  void SetTXAPostGenRun (int channel, int run);
//   gen.c:792  void SetTXAPostGenMode (int channel, int mode);
//   gen.c:800  void SetTXAPostGenToneMag (int channel, double mag);
//   gen.c:808  void SetTXAPostGenToneFreq (int channel, double freq);
//   gen.c:817  void SetTXAPostGenTTMag (int channel, double mag1, double mag2);
//   gen.c:826  void SetTXAPostGenTTFreq (int channel, double freq1, double freq2);
extern void (*SetTXAPostGenRun)(int channel, int run);
extern void (*SetTXAPostGenMode)(int channel, int mode);
extern void (*SetTXAPostGenToneMag)(int channel, double mag);
extern void (*SetTXAPostGenToneFreq)(int channel, double freq);
extern void (*SetTXAPostGenTTMag)(int channel, double mag1, double mag2);
extern void (*SetTXAPostGenTTFreq)(int channel, double freq1, double freq2);

// ---- TXA operator-control setters (P4.b TxControl re-home consumer) ------
// The operator-facing TXA chain controls the FSM/Settings seam pushes
// onto the verbatim TX channel (chid(1,0)=1): mode + bandpass (the
// §15.23 discipline applies — pass SIGNED edges per mode; NEVER call
// SetTXABandpassRun, it toggles the compressor-only bp1 and kills the
// SSB path), mic gain (panel gain1), ALC ceiling/decay, leveler trio.
// Pre-cdef audit per the §15.18 discipline (2026-06-12): signatures
// harvested verbatim from the definition sites; exact casing + presence
// verified against the bundled wdsp.dll export table (PE scan — all 8
// PRESENT).
//   TXA.c:752        void SetTXAMode (int channel, int mode);
//   TXA.c:791        void SetTXABandpassFreqs (int channel, double f_low,
//                        double f_high);
//   patchpanel.c:209 void SetTXAPanelGain1 (int channel, double gain);
//   wcpAGC.c:586     void SetTXAALCDecay (int channel, int decay);
//   wcpAGC.c:604     void SetTXAALCMaxGain (int channel, double maxgain);
//   wcpAGC.c:613     void SetTXALevelerSt (int channel, int state);
//   wcpAGC.c:630     void SetTXALevelerDecay (int channel, int decay);
//   wcpAGC.c:648     void SetTXALevelerTop (int channel, double maxgain);
extern void (*SetTXAMode)(int channel, int mode);
extern void (*SetTXABandpassFreqs)(int channel, double f_low, double f_high);
extern void (*SetTXAPanelGain1)(int channel, double gain);
extern void (*SetTXAALCDecay)(int channel, int decay);
extern void (*SetTXAALCMaxGain)(int channel, double maxgain);
extern void (*SetTXALevelerSt)(int channel, int state);
extern void (*SetTXALevelerDecay)(int channel, int decay);
extern void (*SetTXALevelerTop)(int channel, double maxgain);
//   iir.c:665+       void SetTXAPHROTRun (int channel, int run);   // #109 phase rotator
extern void (*SetTXAPHROTRun)(int channel, int run);
//   cfir.c           void SetTXACFIRRun (int channel, int run);
//   TXA compensating FIR — pre-corrects the CIC interpolation droop of
//   the 192 kHz P2 DUC across the TX passband.  Run ON only for a P2
//   transmit path; the 48 kHz P1/HL2 path leaves it at the create-time
//   default (off).
extern void (*SetTXACFIRRun)(int channel, int run);
//   fmmod.c:178      void SetTXACTCSSRun (int channel, int run);
extern void (*SetTXACTCSSRun)(int channel, int run);
//   #107 FM operator knobs (fmmod.c definition sites; verified present
//   in the bundled wdsp.dll export table 2026-06-20).
//   fmmod.c:148      void SetTXAFMDeviation (int channel, double deviation);
extern void (*SetTXAFMDeviation)(int channel, double deviation);
//   fmmod.c:216      void SetTXAFMAFFreqs (int channel, double low, double high);
//   Sets the FM audio passband edges; the modulator's internal output
//   bandpass is recomputed to ±(deviation + high) = the occupied-channel
//   (Carson) RF clamp.  Pin it per FM entry instead of inheriting the
//   create-time default.
extern void (*SetTXAFMAFFreqs)(int channel, double low, double high);
//   emph.c:108       void SetTXAFMEmphPosition (int channel, int position);
//   Sets the pre-emphasis FIR's chain position; xemphp runs it only when
//   position matches the call site (0 or 1).  position=1 → the native
//   6 dB/oct (300–3000 Hz) comms curve runs; any off-position (e.g. 2) →
//   both call sites pass through = a TRUE bypass even though FM force-runs it.
extern void (*SetTXAFMEmphPosition)(int channel, int position);
//   fmmod.c:167      void SetTXACTCSSFreq (int channel, double freq);
extern void (*SetTXACTCSSFreq)(int channel, double freq);
//   ammod.c:103      void SetTXAAMCarrierLevel (int channel, double c_level);
extern void (*SetTXAAMCarrierLevel)(int channel, double c_level);

// ---- PureSignal (calcc.c exports; committed feature, v0.3 consumer) ------
// Signatures harvested from wdsp/calcc.c definition sites (PORT-
// prefixed; line numbers cited in wdspcalls.cpp).  All verified
// present in the bundled wdsp.dll exports (dumpbin 2026-06-09).
//   calcc.c:617   void pscc (int channel, int size, double* tx, double* rx);
//   calcc.c:840   void psccF (int channel, int size, float* Itxbuff,
//                     float* Qtxbuff, float* Irxbuff, float* Qrxbuff,
//                     int mox, int solidmox);
//   calcc.c:891   void SetPSRunCal (int channel, int run);
//   calcc.c:901   void SetPSMox (int channel, int mox);
//   calcc.c:914   void GetPSInfo (int channel, int* info);
//   calcc.c:924   void SetPSReset (int channel, int reset);
//   calcc.c:934   void SetPSMancal (int channel, int mancal);
//   calcc.c:942   void SetPSAutomode (int channel, int automode);
//   calcc.c:950   void SetPSTurnon (int channel, int turnon);
//   calcc.c:958   void SetPSControl (int channel, int reset, int mancal,
//                     int automode, int turnon);
//   calcc.c:971   void SetPSLoopDelay (int channel, double delay);
//   calcc.c:982   void SetPSMoxDelay (int channel, double delay);
//   calcc.c:1016  void SetPSHWPeak (int channel, double peak);
//   calcc.c:1026  void GetPSHWPeak (int channel, double* peak);
//   calcc.c:1034  void GetPSMaxTX (int channel, double* maxtx);
//   calcc.c:1042  void SetPSPtol (int channel, double ptol);
//   calcc.c:1050  void GetPSDisp (int channel, double* x, double* ym,
//                     double* yc, double* ys, double* cm, double* cc,
//                     double* cs);
//   calcc.c:1065  void SetPSFeedbackRate (int channel, int rate);
//   calcc.c:1094  void SetPSPinMode (int channel, int pin);
//   calcc.c:1102  void SetPSMapMode (int channel, int map);
//   calcc.c:1110  void SetPSStabilize (int channel, int stbl);
//   calcc.c:1132  void SetPSIntsAndSpi (int channel, int ints, int spi);
//
// NOTE: SetPSTXDelay is exported by the DLL (dumpbin ordinal 143)
// but its definition site was not located in calcc.c on the
// 2026-06-09 harvest pass — per table rule #1 it is NOT declared
// here until its signature is verified at first use.
extern void (*pscc)(int channel, int size, double* tx, double* rx);
extern void (*psccF)(int channel, int size, float* Itxbuff, float* Qtxbuff,
                     float* Irxbuff, float* Qrxbuff, int mox, int solidmox);
extern void (*SetPSRunCal)(int channel, int run);
extern void (*SetPSMox)(int channel, int mox);
extern void (*GetPSInfo)(int channel, int* info);
extern void (*SetPSReset)(int channel, int reset);
extern void (*SetPSMancal)(int channel, int mancal);
extern void (*SetPSAutomode)(int channel, int automode);
extern void (*SetPSTurnon)(int channel, int turnon);
extern void (*SetPSControl)(int channel, int reset, int mancal,
                            int automode, int turnon);
extern void (*SetPSLoopDelay)(int channel, double delay);
extern void (*SetPSMoxDelay)(int channel, double delay);
extern void (*SetPSHWPeak)(int channel, double peak);
extern void (*GetPSHWPeak)(int channel, double* peak);
extern void (*GetPSMaxTX)(int channel, double* maxtx);
extern void (*SetPSPtol)(int channel, double ptol);
extern void (*GetPSDisp)(int channel, double* x, double* ym, double* yc,
                         double* ys, double* cm, double* cc, double* cs);
extern void (*SetPSFeedbackRate)(int channel, int rate);
extern void (*SetPSPinMode)(int channel, int pin);
extern void (*SetPSMapMode)(int channel, int map);
extern void (*SetPSStabilize)(int channel, int stbl);
extern void (*SetPSIntsAndSpi)(int channel, int ints, int spi);

// ---- resolver ------------------------------------------------------------
// Resolves every pointer above from the already-loaded wdsp.dll
// module (GetModuleHandle by base name — lyra::dsp::WdspNative has
// LoadLibrary'd it before this runs; see main.cpp wire-in).
// Returns the number of UNRESOLVED symbols (0 = all good); each
// missing name is qWarning'd.  MUST be called after WdspNative::load
// succeeds and BEFORE create_cmaster().
int resolve_wdsp_calls();

}  // namespace lyra::wire
