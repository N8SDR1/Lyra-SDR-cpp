// Lyra-cpp — wdspcalls.cpp
//
// Pointer storage + resolver for the WDSP call table.  See
// wdspcalls.h for the operator-approved seam rationale + the
// per-entry signature citations.

#include "wire/wdspcalls.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <QtGlobal>   // qWarning

namespace lyra::wire {

// ---- pointer storage (all start null; resolve_wdsp_calls fills) ----
void (*OpenChannel)(int, int, int, int, int, int, int, int,
                    double, double, double, double, int) = nullptr;
void (*CloseChannel)(int) = nullptr;
int  (*SetChannelState)(int, int, int) = nullptr;
void (*SetType)(int, int) = nullptr;
void (*SetInputBuffsize)(int, int) = nullptr;
void (*SetDSPBuffsize)(int, int) = nullptr;
void (*SetInputSamplerate)(int, int) = nullptr;
void (*SetDSPSamplerate)(int, int) = nullptr;
void (*SetOutputSamplerate)(int, int) = nullptr;
void (*fexchange0)(int, double*, double*, int*) = nullptr;
RESAMPLE (*create_resample)(int, int, double*, double*, int, int,
                            double, int, double) = nullptr;
void (*destroy_resample)(RESAMPLE) = nullptr;
void (*flush_resample)(RESAMPLE) = nullptr;
int  (*xresample)(RESAMPLE) = nullptr;
// VAC adaptive resampler — rmatchV (#158).  Opaque void* handle.
void* (*create_rmatchV)(int, int, int, int, int, double) = nullptr;
void (*destroy_rmatchV)(void*) = nullptr;
void (*xrmatchIN)(void*, double*) = nullptr;
void (*xrmatchOUT)(void*, double*) = nullptr;
void (*forceRMatchVar)(void*, int, double) = nullptr;
void (*getRMatchDiags)(void*, int*, int*, double*, int*, int*) = nullptr;
void (*resetRMatchDiags)(void*) = nullptr;
void (*setRMatchInsize)(void*, int) = nullptr;
void (*setRMatchOutsize)(void*, int) = nullptr;
void (*setRMatchNomInrate)(void*, int) = nullptr;
void (*setRMatchNomOutrate)(void*, int) = nullptr;
void (*setRMatchRingsize)(void*, int) = nullptr;
void (*setRMatchFeedbackGain)(void*, double) = nullptr;
void (*setRMatchSlewTime)(void*, double) = nullptr;
void (*setRMatchSlewTime1)(void*, double) = nullptr;
void (*setRMatchPropRingMin)(void*, int) = nullptr;
void (*setRMatchPropRingMax)(void*, int) = nullptr;
void (*setRMatchFFRingMin)(void*, int) = nullptr;
void (*setRMatchFFRingMax)(void*, int) = nullptr;
void (*setRMatchFFAlpha)(void*, double) = nullptr;
void (*XCreateAnalyzer)(int, int*, int, int, int, char*) = nullptr;
void (*DestroyAnalyzer)(int) = nullptr;
void (*Spectrum0)(int, int, int, int, double*) = nullptr;
EER  (*create_eer)(int, int, double*, double*, double*, int, double,
                   double, int, double, double, int) = nullptr;
void (*destroy_eer)(EER) = nullptr;
void (*xeer)(EER) = nullptr;
void (*pSetEERSize)(EER, int) = nullptr;
void (*pSetEERSamplerate)(EER, int) = nullptr;
void (*SetTXAPostGenRun)(int, int) = nullptr;
void (*SetTXAPostGenMode)(int, int) = nullptr;
void (*SetTXAPostGenToneMag)(int, double) = nullptr;
void (*SetTXAPostGenToneFreq)(int, double) = nullptr;
void (*SetTXAPostGenTTMag)(int, double, double) = nullptr;
void (*SetTXAPostGenTTFreq)(int, double, double) = nullptr;
void (*SetTXAMode)(int, int) = nullptr;
void (*SetTXABandpassFreqs)(int, double, double) = nullptr;
void (*SetTXAPanelGain1)(int, double) = nullptr;
void (*SetTXAALCDecay)(int, int) = nullptr;
void (*SetTXAALCMaxGain)(int, double) = nullptr;
void (*SetTXALevelerSt)(int, int) = nullptr;
void (*SetTXALevelerDecay)(int, int) = nullptr;
void (*SetTXALevelerTop)(int, double) = nullptr;
void (*SetTXAPHROTRun)(int, int) = nullptr;   // #109 phase rotator run
void (*SetTXACFIRRun)(int, int) = nullptr;    // TXA compensating FIR run (P2 DUC droop pre-correction)
void (*SetTXACTCSSRun)(int, int) = nullptr;   // FM sub-tone run (off for basic FM)
void (*SetTXAFMDeviation)(int, double) = nullptr;   // #107 FM peak deviation Hz
void (*SetTXAFMAFFreqs)(int, double, double) = nullptr;  // FM audio band edges (low,high) → modulator occupied-BW clamp
void (*SetTXAFMEmphPosition)(int, int) = nullptr;   // FM pre-emphasis chain position (1 = run the 6 dB/oct curve; off-position = bypass)
void (*SetTXACTCSSFreq)(int, double) = nullptr;     // #107 FM CTCSS sub-tone Hz
void (*SetTXAAMCarrierLevel)(int, double) = nullptr;   // AM/SAM carrier fraction 0..1
void (*pscc)(int, int, double*, double*) = nullptr;
void (*psccF)(int, int, float*, float*, float*, float*, int, int) = nullptr;
void (*SetPSRunCal)(int, int) = nullptr;
void (*SetPSMox)(int, int) = nullptr;
void (*GetPSInfo)(int, int*) = nullptr;
void (*SetPSReset)(int, int) = nullptr;
void (*SetPSMancal)(int, int) = nullptr;
void (*SetPSAutomode)(int, int) = nullptr;
void (*SetPSTurnon)(int, int) = nullptr;
void (*SetPSControl)(int, int, int, int, int) = nullptr;
void (*SetPSLoopDelay)(int, double) = nullptr;
void (*SetPSMoxDelay)(int, double) = nullptr;
void (*SetPSHWPeak)(int, double) = nullptr;
void (*GetPSHWPeak)(int, double*) = nullptr;
void (*GetPSMaxTX)(int, double*) = nullptr;
void (*SetPSPtol)(int, double) = nullptr;
void (*GetPSDisp)(int, double*, double*, double*, double*,
                  double*, double*, double*) = nullptr;
void (*SetPSFeedbackRate)(int, int) = nullptr;
void (*SetPSPinMode)(int, int) = nullptr;
void (*SetPSMapMode)(int, int) = nullptr;
void (*SetPSStabilize)(int, int) = nullptr;
void (*SetPSIntsAndSpi)(int, int, int) = nullptr;

// X-macro over every table entry: one line per symbol; the resolver
// below expands it.  Adding a symbol = one extern in the header, one
// storage definition above, one line here.
#define LYRA_WDSP_CALL_TABLE(X) \
    X(OpenChannel)          \
    X(CloseChannel)         \
    X(SetChannelState)      \
    X(SetType)              \
    X(SetInputBuffsize)     \
    X(SetDSPBuffsize)       \
    X(SetInputSamplerate)   \
    X(SetDSPSamplerate)     \
    X(SetOutputSamplerate)  \
    X(fexchange0)           \
    X(create_resample)      \
    X(destroy_resample)     \
    X(flush_resample)       \
    X(xresample)            \
    X(create_rmatchV)       \
    X(destroy_rmatchV)      \
    X(xrmatchIN)            \
    X(xrmatchOUT)           \
    X(forceRMatchVar)       \
    X(getRMatchDiags)       \
    X(resetRMatchDiags)     \
    X(setRMatchInsize)      \
    X(setRMatchOutsize)     \
    X(setRMatchNomInrate)   \
    X(setRMatchNomOutrate)  \
    X(setRMatchRingsize)    \
    X(setRMatchFeedbackGain)\
    X(setRMatchSlewTime)    \
    X(setRMatchSlewTime1)   \
    X(setRMatchPropRingMin) \
    X(setRMatchPropRingMax) \
    X(setRMatchFFRingMin)   \
    X(setRMatchFFRingMax)   \
    X(setRMatchFFAlpha)     \
    X(XCreateAnalyzer)      \
    X(DestroyAnalyzer)      \
    X(Spectrum0)            \
    X(create_eer)           \
    X(destroy_eer)          \
    X(xeer)                 \
    X(pSetEERSize)          \
    X(pSetEERSamplerate)    \
    X(SetTXAPostGenRun)     \
    X(SetTXAPostGenMode)    \
    X(SetTXAPostGenToneMag) \
    X(SetTXAPostGenToneFreq)\
    X(SetTXAPostGenTTMag)   \
    X(SetTXAPostGenTTFreq)  \
    X(SetTXAMode)           \
    X(SetTXABandpassFreqs)  \
    X(SetTXAPanelGain1)     \
    X(SetTXAALCDecay)       \
    X(SetTXAALCMaxGain)     \
    X(SetTXALevelerSt)      \
    X(SetTXALevelerDecay)   \
    X(SetTXALevelerTop)     \
    X(SetTXAPHROTRun)       \
    X(SetTXACFIRRun)        \
    X(SetTXACTCSSRun)       \
    X(SetTXAFMDeviation)    \
    X(SetTXAFMAFFreqs)      \
    X(SetTXAFMEmphPosition) \
    X(SetTXACTCSSFreq)      \
    X(SetTXAAMCarrierLevel) \
    X(pscc)                 \
    X(psccF)                \
    X(SetPSRunCal)          \
    X(SetPSMox)             \
    X(GetPSInfo)            \
    X(SetPSReset)           \
    X(SetPSMancal)          \
    X(SetPSAutomode)        \
    X(SetPSTurnon)          \
    X(SetPSControl)         \
    X(SetPSLoopDelay)       \
    X(SetPSMoxDelay)        \
    X(SetPSHWPeak)          \
    X(GetPSHWPeak)          \
    X(GetPSMaxTX)           \
    X(SetPSPtol)            \
    X(GetPSDisp)            \
    X(SetPSFeedbackRate)    \
    X(SetPSPinMode)         \
    X(SetPSMapMode)         \
    X(SetPSStabilize)       \
    X(SetPSIntsAndSpi)

int resolve_wdsp_calls()
{
    HMODULE h = GetModuleHandleW(L"wdsp.dll");
    if (h == nullptr) {
        qWarning("[wdspcalls] wdsp.dll is not loaded — "
                 "resolve_wdsp_calls() called before WdspNative::load()?");
        return -1;
    }

    int missing = 0;

#define LYRA_RESOLVE(name)                                                  \
    {                                                                       \
        FARPROC p = GetProcAddress(h, #name);                               \
        if (p == nullptr) {                                                 \
            qWarning("[wdspcalls] UNRESOLVED wdsp.dll export: %s", #name);  \
            ++missing;                                                      \
        }                                                                   \
        name = reinterpret_cast<decltype(name)>(                            \
            reinterpret_cast<void*>(p));                                    \
    }

    LYRA_WDSP_CALL_TABLE(LYRA_RESOLVE)

#undef LYRA_RESOLVE

    return missing;
}

}  // namespace lyra::wire
