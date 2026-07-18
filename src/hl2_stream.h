// Lyra — HPSDR Protocol 1 stream (EP6 receive + EP2 keepalive).
//
// Step 2a scope (shipped): open the stream, RX EP6 datagrams on a
// dedicated OS thread, verify Metis header + USB-frame sync, count
// datagrams + dropouts + framing errors.
//
// Step 2b scope (this commit): add the EP2 keepalive writer on its
// OWN dedicated OS thread.  HL2 gateware watchdog cuts EP6 after
// ~13 sec without host EP2 traffic — Step 2a surfaced this empirically
// on the operator's HL2+ bench (65 520 dg / ~13 sec then stream
// stops cold).  EP2 writer fires at 380 Hz (= 48 kHz audio sample
// rate / 126 audio samples per EP2 datagram) carrying a minimum-
// viable C&C config (192 kHz IQ, nddc=4, MOX off, duplex bit on)
// + zero audio/TX-IQ payload.  Keeps the gateware happy indefinitely.
//
// Locked architectural rule: the wire path runs on dedicated OS
// threads (one for RX, one for TX), each on a std::jthread, native
// WinSock2 socket shared between them (sendto + recvfrom are
// independently thread-safe at the OS level on separate directions
// of one socket).  No Qt event loop on the wire threads.  No GIL.
// No Python.  Anywhere.  Ever.
//
// References (read before coding — Lyra-native implementation,
// nothing copied):
//   * HL2 wiki Protocol.md — register map, EP2/EP6 layout
//   * HPSDR Protocol 1 spec (openHPSDR.org)
//   * CLAUDE.md §3.2 / §3.4 — operator-verified byte layouts
//   * CLAUDE.md §5 — original Python threading model (now C++23)
//   * CLAUDE.md §15.26 — Win32 HIGH_RESOLUTION timer pattern
//
// Wire reference summary:
//
//   Host → radio control (64-byte UDP datagram to radio:1024):
//     bytes [0..1] = 0xEF 0xFE (magic)
//     byte  [2]    = 0x04 (command)
//     byte  [3]    = command byte; 0x01 = start IQ, 0x00 = stop
//     bytes [4..63] = zero padding
//
//   Host → radio EP2 keepalive (1032-byte UDP datagram to radio:1024):
//     8-byte Metis header:
//       bytes [0..1] = 0xEF 0xFE (magic)
//       byte  [2]    = 0x01 (data frame)
//       byte  [3]    = 0x02 (endpoint = EP2 = host → radio)
//       bytes [4..7] = sequence number (BIG-endian uint32)
//     USB frame 1 (512 bytes at offset 8):
//       bytes [0..2] = 0x7F 0x7F 0x7F (sync)
//       byte  [3]    = C0 = frame address << 1 | MOX bit
//       bytes [4..7] = C1..C4 (config registers; frame-0 carries
//                      sample rate, OC pins, nddc, duplex bit)
//       bytes [8..511] = 504 bytes = 63 LRIQ tuples × 8 bytes
//                       (L audio + R audio + TX I + TX Q, all
//                        16-bit signed BE; ZERO during RX-only)
//     USB frame 2 (512 bytes at offset 520): same layout
//
//   Radio → host EP6 receive (1032-byte UDP datagram from radio:1024):
//     same Metis layout, byte [3] = 0x06; each USB frame's 504-byte
//     payload = 19 sample slots × 26 bytes (DDC0-3 I/Q + mic).
//     Parsing lands in Step 2c — Step 2b only verifies integrity.
//
// Frame-0 C1..C4 we send on every keepalive (matches the post-START
// gateware default the operator's HL2+ runs at — no state change):
//   C1 = 0x02 — sample rate bits [1:0] = 10 = 192 kHz per DDC
//   C2 = 0x00 — no open-collector outputs, no 10MHz ref override
//   C3 = 0x00 — no random, no dither, no preamp adjust
//   C4 = 0x1C — nddc=4 (bits [6:3] = 0011 = 0x18) | duplex bit
//               (bit 2 = 0x04) per CLAUDE.md §3.2 main-loop emission

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <stop_token>
#include <vector>

// Step 14 Stage 2b2 — Ep6RecvThread is the live EP6 recv path on
// HL2Stream's behalf, retiring the OLD rxWorker_ jthread.  Member
// `ep6Thread_` is declared in the private block below; the include
// is at namespace-pulling distance so the destructor + getter +
// open()/close() wiring sites all compile without forward-declaring.
#include "wire/Ep6RecvThread.h"
// Stage 2b2: Router.h needed for the inline setIqSink shim's
// register_sink() + router_instance(0) calls (the post-rxWorker_
// IQ-dispatch path).
#include "wire/Router.h"
#include "oc/OcControl.h"   // #199 — editable per-band OC table + emit choke
// #91 VOX — pure mic-RMS gate (hardware-free decision core).
#include "tx/VoxDetector.h"
#include <memory>

// #105 CW-3a — host CW keyer (CWX); the full type is pulled in the .cpp.
namespace lyra::tx { class CwKeyer; }

namespace lyra::ipc {

// Platform socket handle, opaque to consumers of this header (we do
// NOT drag winsock2.h through here).  On Windows the platform
// SOCKET type is UINT_PTR which is quintptr.  On POSIX it would be
// `int` — the implementation casts internally.
using SocketHandle = quintptr;
inline constexpr SocketHandle kInvalidSocket = ~SocketHandle{0};

class HL2Stream : public QObject {
    Q_OBJECT
    // RX direction (Step 2a)
    Q_PROPERTY(bool    running              READ isRunning            NOTIFY runningChanged)
    Q_PROPERTY(QString targetIp             READ targetIp             NOTIFY runningChanged)
    Q_PROPERTY(double  datagramsPerSec      READ datagramsPerSec      NOTIFY statsChanged)
    Q_PROPERTY(qint64  totalDatagrams       READ totalDatagrams       NOTIFY statsChanged)
    Q_PROPERTY(qint64  seqErrors            READ seqErrors            NOTIFY statsChanged)
    Q_PROPERTY(qint64  framingErrors        READ framingErrors        NOTIFY statsChanged)
    // TX direction (Step 2b — EP2 keepalive)
    Q_PROPERTY(double  txDatagramsPerSec    READ txDatagramsPerSec    NOTIFY statsChanged)
    Q_PROPERTY(qint64  txTotalDatagrams     READ txTotalDatagrams     NOTIFY statsChanged)
    Q_PROPERTY(qint64  txSendErrors         READ txSendErrors         NOTIFY statsChanged)
    // Stage 2b2: rx1DbFs Q_PROPERTY retired (strict-reference rule;
    // WDSP audioDbFs is the reference-faithful S-meter path).
    // RX1 (DDC0) receive frequency, Hz — tuning from C++ (Step 4)
    Q_PROPERTY(quint32 rx1FreqHz            READ rx1FreqHz            NOTIFY rx1FreqChanged)
    // Frequency-calibration factor — live-bindable for the calibration UI.
    Q_PROPERTY(double  freqCorrection       READ freqCorrection       NOTIFY freqCorrectionChanged)
    // SPLIT — TX on VFO B while RX stays on VFO A (same-band cross-freq
    // TX; also the engine repeater offsets ride on).  PureSignal-SAFE BY
    // CONSTRUCTION: the split TX freq drives the SINGLE txFreqHz_ ->
    // set_tx_freq (TX NCO + the DDC2/DDC3 PS-feedback registers), exactly
    // as the reference derives PS feedback from tx[0].frequency
    // (networkproto1.c cases 5/6 = tx freq on HL2).  No parallel TX-freq
    // path — so PS, whenever it lands, samples the split VFO unchanged.
    Q_PROPERTY(bool    splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY splitEnabledChanged)
    Q_PROPERTY(quint32 vfoBHz       READ vfoBHz       WRITE setVfoBHz       NOTIFY vfoBHzChanged)
    // RIT (RX incremental tuning) + XIT (TX incremental tuning) — signed
    // Hz offsets folded into the RX-NCO / TX-NCO writes respectively.
    Q_PROPERTY(bool    ritEnabled  READ ritEnabled  WRITE setRitEnabled  NOTIFY ritChanged)
    Q_PROPERTY(int     ritOffsetHz READ ritOffsetHz WRITE setRitOffsetHz NOTIFY ritChanged)
    Q_PROPERTY(bool    xitEnabled  READ xitEnabled  WRITE setXitEnabled  NOTIFY xitChanged)
    Q_PROPERTY(int     xitOffsetHz READ xitOffsetHz WRITE setXitOffsetHz NOTIFY xitChanged)
    // #174 CTUNE (center-tune lock).  ctuneCenterHz = the locked DDC centre
    // when engaged (0 = off); ctuneEnabled is the convenience read (engage
    // snaps the centre to the current dial).  See docs/architecture/ctune_design.md.
    Q_PROPERTY(bool    ctuneEnabled  READ ctuneEnabled  NOTIFY ctuneChanged)
    Q_PROPERTY(quint32 ctuneCenterHz READ ctuneCenterHz NOTIFY ctuneChanged)
    // RX1 LNA gain (AD9866 PGA), dB.  Range −12…+48; sent on the C&C
    // 0x14 register (C4 = 0x40 | ((dB+12)&0x3F)), rotated into the EP2
    // C&C cadence at ~20 Hz.  Persisted.
    Q_PROPERTY(int lnaGainDb READ lnaGainDb WRITE setLnaGainDb NOTIFY lnaGainChanged)
    // Auto-LNA — overload-triggered gain protection (standard HF SDR pattern).
    // When on, sustained ADC overload backs the LNA off 3 dB; when the
    // band is clear it creeps gain back up 1 dB per hold interval,
    // riding the overload edge.  Roams the full −12…+48 range
    // independently of the manual set point; auto adjustments are NOT
    // persisted (the manual slider value remains the stored set point,
    // restored when Auto is turned off).  Fresh-install default ON
    // (matches the operator's working-station config).
    Q_PROPERTY(bool autoLna       READ autoLna        WRITE setAutoLna        NOTIFY autoLnaChanged)
    Q_PROPERTY(bool autoLnaUndo   READ autoLnaUndo    WRITE setAutoLnaUndo    NOTIFY autoLnaChanged)
    Q_PROPERTY(int  autoLnaHoldSec READ autoLnaHoldSec WRITE setAutoLnaHoldSec NOTIFY autoLnaChanged)
    // Live ADC-overload indicator — true when the HL2 gateware reported
    // ADC clipping in the last ~400 ms window (EP6 status C1 bit 0).
    Q_PROPERTY(bool adcOverload   READ adcOverload    NOTIFY adcOverloadChanged)
    // External filter board (N2ADR): when enabled, the per-band OC
    // pattern is driven on frame-0 C2 so the board's RX band-pass +
    // 3 MHz HPF relays follow the band (front-end protection).  ocBits
    // is the live 7-bit J16 pattern (readout).
    Q_PROPERTY(bool filterBoardEnabled READ filterBoardEnabled
               WRITE setFilterBoardEnabled NOTIFY filterBoardChanged)
    Q_PROPERTY(int  ocBits             READ ocBits NOTIFY ocBitsChanged)
    // TX-0a — HL2 telemetry decoded from the EP6 status rotation
    // (C0&0xF8 address field).  RF-SAFE: pure read of the radio→host
    // stream, zero effect on what we transmit.  Temp + supply are
    // RX-time-meaningful; PA current/volts + fwd/rev power matter
    // during TX.  ⚠ The AINx→slot map + the watt/amp conversions are
    // gateware-rev-specific on HL2+ — these follow the documented HL2
    // rotation and MUST be bench-verified on the operator's unit
    // (set env LYRA_TELEM_DEBUG=1 to log the raw (addr,C1..C4)
    // rotation).  NaN getter = no telemetry for that slot yet.
    Q_PROPERTY(double hl2TempC   READ hl2TempC   NOTIFY statsChanged)
    Q_PROPERTY(double hl2SupplyV READ hl2SupplyV NOTIFY statsChanged)
    Q_PROPERTY(double paCurrentA READ paCurrentA NOTIFY statsChanged)
    Q_PROPERTY(double fwdPowerW  READ fwdPowerW  NOTIFY statsChanged)
    Q_PROPERTY(double revPowerW  READ revPowerW  NOTIFY statsChanged)
    // TX-0c-fsm — wire-level MOX state (true once the keydown TR-delay
    // sequence completes and the C0 bit-0 has settled on the wire;
    // cleared at the END of the keyup sequence).  This is the
    // operator-visible "on the air" truth — drive the red TX indicator
    // off this, NOT off the toggle button's checked state, so the LED
    // tracks the actual radio state through the TR-delay window.
    Q_PROPERTY(bool moxActive READ moxActive NOTIFY moxActiveChanged)
    // #105 — "transmitting for UI purposes" = wire MOX OR CW keying.  QSK CW
    // keys the PA at the gateware without raising wire MOX, so display
    // consumers (panadapter rescale + analyzer swap, red-on-air) bind this
    // instead of moxActive so they flip during CW too.  NOT a wire signal.
    Q_PROPERTY(bool txDisplayActive READ txDisplayActive
               NOTIFY txDisplayActiveChanged)
    // Amp watts-cap live status, for the TX-panel CAP chip.  Recomputed on
    // the ACTIVE TX band inside applyTxPower_ (the one chokepoint every
    // drive / PA-gain / band / cap change routes through).  0 = cap off, or
    // on but not actively limiting → chip hidden.  1 = cap ON and holding a
    // CALIBRATED band at the set watts.  2 = cap ON but this band is NOT
    // calibrated, so TX is clamped to the conservative ~30 % fallback (the
    // "cap set to 6 W but the radio only makes 3 W" trap Pierre HS0ZRT hit).
    // capLimitW is the set cap in watts (for the chip's "CAP nW" text).
    Q_PROPERTY(int    capStatus READ capStatus NOTIFY capStatusChanged)
    Q_PROPERTY(double capLimitW READ maxOutputW NOTIFY maxOutputWChanged)
    // TX-0c-pa-debug — host-side TX safety timeout.  Auto-clears MOX
    // (via requestMox(false)) if the radio stays keyed continuously
    // past txTimeoutSec seconds.  Persisted (tx/timeoutSeconds in
    // QSettings).  Operator can bypass via txTimeoutBypass — typically
    // for long-form AM ragchews / slow CW beacons where the ~10-min
    // default is too short.  Default is 600 s (10 min); range 60..1200
    // (1..20 min) enforced by the setter.
    Q_PROPERTY(int  txTimeoutSec    READ txTimeoutSec
               WRITE setTxTimeoutSec NOTIFY txTimeoutSecChanged)
    Q_PROPERTY(bool txTimeoutBypass READ txTimeoutBypass
               WRITE setTxTimeoutBypass NOTIFY txTimeoutBypassChanged)
    // Task #36 — Hardware PTT input forwarder enable.  When TRUE, an
    // edge in EP6 status C0 bit 0 (`ptt_in`, the HL2's hardware PTT-
    // input pin) is forwarded to requestMox() so a foot-switch / hand-
    // mic PTT keys the rig.  Default FALSE — per project-memory §10
    // Q#1, the N8SDR HL2+/AK4951 unit carries a non-zero ptt_in at
    // RX rest, so an always-on forwarder mis-reads it as a press and
    // produces a phantom-TX surge.  Operator MUST bench-verify ptt_in
    // is a clean 0 at RX rest on their specific unit BEFORE enabling.
    // The forwarder is gated entirely on this atomic — when false the
    // EP6 RX worker just decodes the bit into a local + discards it,
    // wire-identical to the pre-Task-#36 build.  Setter is thread-safe
    // (atomic store); Settings UI persists via Prefs.hwPttEnabled.
    Q_PROPERTY(bool hwPttEnabled READ hwPttEnabled
               WRITE setHwPttEnabled NOTIFY hwPttEnabledChanged)
    // TX-0c-pa-debug — operator-gated PA-enable.  When true, frame-10
    // C2 bit 3 (0x08) goes on the wire — gateware PA-enable, active-
    // high.  Combined with the wire MOX bit + non-zero TX I/Q drive,
    // this would put RF on the antenna.  At v0.2.0 there's still no
    // SSB modulator, so TX I/Q stays zero and the operator sees PA
    // bias current rise on keydown (~0.2 A idle bias on N8SDR's HL2+)
    // with ~0 W on the dummy load — the safe first-RF bench gate.
    // Persisted: tx/paEnabled.  Defensive cleared on stream open/close.
    Q_PROPERTY(bool paEnabled READ paEnabled WRITE setPaEnabled
               NOTIFY paEnabledChanged)
    // Task #39 — HL2 hardware +20 dB Mic Boost via the codec's
    // analog PGA.  Lands C0 0x12 C2 bit 0 (the reference's
    // standard SetMicBoost bit, networkproto1.c:748-751 +
    // network.h:220-221 mic_boost : 1).  Single bit on the
    // wire — hardware is 2-state (off / +20 dB); intermediate
    // levels come from the operator Mic Gain slider
    // (Task #32) on top of this hardware boost.  Persisted:
    // tx/micBoost.  Not defensively cleared on open/close —
    // this is the operator's voice-chain calibration, not a
    // safety gate.  No RF behavior; takes effect only while
    // the codec mic is the active TX source.
    Q_PROPERTY(bool micBoost READ micBoost WRITE setMicBoost
               NOTIFY micBoostChanged)
    // HL2 "Band Volts" output (MI0BOT / Ramdor gateware feature): C0=0x00
    // frame C3 bit 3 (the ADC "dither" bit) → gateware band_volts_enabled →
    // per-band analog voltage on the fan-PWM pin for amps / tuners / antenna
    // switches that band-follow off a band voltage.  Repurposes the fan pin
    // while on → operator opt-in, default OFF.  Persisted: hw/bandVolts.
    Q_PROPERTY(bool bandVoltsOutput READ bandVoltsOutput WRITE setBandVoltsOutput
               NOTIFY bandVoltsOutputChanged)
    // TX-0c-pa-drive — operator-tunable drive DAC level.  Maps to
    // frame-10 C1; the gateware uses the top 4 bits → 16 coarse steps,
    // so wire values 0/16/32/.../255 are the meaningful endpoints (the
    // bottom 4 bits are ignored).  Operator-facing UI uses 0..100 %;
    // raw 0..255 is the on-wire value (UI converts via
    // c1 = round(255 * pct/100), matching the verified flat map —
    // per-band calibration is a later polish item, not first-RF).
    // At drive=0 the wire is byte-identical to TX-0c-pa-debug B-pa
    // regardless of PA enable / MOX: zero amplitude on the DAC = no
    // carrier.  Persisted: tx/driveLevel.  NOT defensively cleared on
    // stream open/close — PA enable is the safety gate (cleared every
    // open/close); drive is the volume knob (carries the operator's
    // intentional set point across stop/restart, harmless without PA).
    Q_PROPERTY(int  txDriveLevel READ txDriveLevel WRITE setTxDriveLevel
               NOTIFY txDriveLevelChanged)
    // TX-0c-tune — operator-armed tune-carrier generator.  When TRUE
    // *and* the wire MOX bit is high, the EP2 writer fills each LRIQ
    // tuple's TX-I bytes (offsets 4..5) with a constant ~0.95-full-
    // scale value and TX-Q bytes (offsets 6..7) with zero.  DC
    // injection in I produces a pure carrier at LO exactly (no audio
    // offset, no sideband), which lands the on-air carrier at the
    // operator's dial freq — the universal HF-rig TUN convention
    // (every commercial HF rig).  This is the host-streamed
    // carrier the HL2+ ak4951v4 gateware requires for any RF (the
    // gateware has NO internal tune carrier, so bias + MOX + drive
    // without a non-zero TX I/Q stream = zero RF).  Drive % scales
    // the on-air power; this just provides the carrier for it to
    // scale.  Auto-clears on every moxActiveChanged(false) edge so a
    // stray MOX click after a tune session can't accidentally re-emit
    // the carrier.  NOT persisted (always starts off — TUN is an
    // explicit operator gesture, not a configured state).
    Q_PROPERTY(bool tuneEnabled READ tuneEnabled WRITE setTuneEnabled
               NOTIFY tuneEnabledChanged)
    // TX-1 component 6 — SSB modulator I/Q injection.  When TRUE
    // *and* the wire MOX bit is high, the EP2 writer pulls 126
    // complex<float> samples per datagram from the registered TX
    // I/Q source (typically TxDspWorker), multiplies by the cos²
    // fade coef, quantizes via the reference-faithful symmetric
    // round-to-nearest, and packs them into the TX-I/TX-Q bytes
    // of each LRIQ tuple.  TUN takes priority over SSB (if both
    // are armed simultaneously, TUN carrier wins — operator gesture
    // semantics).  When FALSE, the SSB branch is INACTIVE — even
    // with MOX keyed, no SSB I/Q lands on the wire (the mandatory
    // zero-fill branch runs instead, mirroring the verified
    // reference's `!XmitBit ⇒ zero outIQbufp` posture (see
    // docs/architecture/tx1_ssb_design.md §5.7).
    //
    // Defaults FALSE.  Set TRUE by the FSM at keydown (after the
    // WDSP TXA channel is started + the rf_delay window has
    // elapsed) and cleared FALSE at keyup (before the wire MOX
    // bit clears, per §5.7 keyup ordering invariant).
    //
    // NOT persisted — TX intent doesn't survive a stream stop /
    // restart cycle (safety: come-up-not-keyed posture).
    Q_PROPERTY(bool injectTxIq READ injectTxIq WRITE setInjectTxIq
               NOTIFY injectTxIqChanged)
    // TX-1 component 6 — EP2-side underrun counter.  Increments
    // when MOX + injectTxIq are BOTH true (so the EP2 packer
    // expected SSB I/Q on this datagram) but tryConsumeTxIq
    // returned false (the producer hadn't signalled dataReady in
    // time — usually because WDSP TXA hadn't accumulated 126
    // samples yet at keydown, OR the producer thread was
    // momentarily starved).  The EP2 packer falls through to the
    // mandatory zero-fill on underrun (silent slot — safe but
    // would be audible as a brief gap on actual SSB voice).
    // Bench instrument; should stay ≈ 0 in steady state once the
    // FSM keydown is wired.
    Q_PROPERTY(qint64 txIqUnderruns READ txIqUnderruns NOTIFY statsChanged)
    // TX-1 component 5a — TR-sequencing + cos² envelope durations.
    //
    // ⚠ HOT-SWITCH PROTECTION (operator-mandated, 2026-05-31):
    // These values are the load-bearing safety profile for external
    // solid-state HF linear amplifiers.  Defaults come from the
    // operator's bench-validated working-station config (HL2+ +
    // 1 kW SS linear); reducing them without knowing the amp's T/R
    // relay settle spec can destroy the PA via hot-switching.
    //
    //   moxDelayMs       — operator-PTT → wire-MOX-bit hot     (default 15)
    //   rfDelayMs        — wire-MOX hot → "RF settled" emit    (default 50)
    //   spaceMoxDelayMs  — keyup re-key window before MOX-off  (default 13)
    //   pttOutDelayMs    — MOX-off → final cleanup             (default 5)
    //   txStopDelayMs    — TX-stop → wire-MOX-clear in-flight  (default 10)
    //                      Component 7 (reference-faithful match to
    //                      the verified reference's keyup `mox_delay`):
    //                      time between TX-DSP-channel-stop (blocking
    //                      WDSP internal flush) and the wire MOX bit
    //                      clearing.  Lets UDP datagrams already-sent
    //                      or in-OS-buffer (with MOX=1 + non-zero
    //                      samples) actually reach + be processed by
    //                      the HL2 BEFORE we flip the wire state, so
    //                      the gateware processes them as the keyed
    //                      samples they were when sent.
    //
    // Persisted under tx/trSeq/<key> in QSettings.  Live-apply (no
    // restart): a value change while the radio is keyed takes effect
    // on the NEXT MOX edge.
    Q_PROPERTY(int moxDelayMs      READ moxDelayMs
               WRITE setMoxDelayMs      NOTIFY moxDelayMsChanged)
    Q_PROPERTY(int rfDelayMs       READ rfDelayMs
               WRITE setRfDelayMs       NOTIFY rfDelayMsChanged)
    Q_PROPERTY(int spaceMoxDelayMs READ spaceMoxDelayMs
               WRITE setSpaceMoxDelayMs NOTIFY spaceMoxDelayMsChanged)
    Q_PROPERTY(int pttOutDelayMs   READ pttOutDelayMs
               WRITE setPttOutDelayMs   NOTIFY pttOutDelayMsChanged)
    Q_PROPERTY(int txStopDelayMs   READ txStopDelayMs
               WRITE setTxStopDelayMs   NOTIFY txStopDelayMsChanged)

    // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
    //
    // micGainDb       — `SetTXAPanelGain1` (TXA chain stage #3, before
    //                   phrot/EQ/leveler/CFCOMP/bandpass/compressor/
    //                   OSCtrl/ALC).  The only operator-tunable software
    //                   gain stage in the WDSP TXA chain.  Range
    //                   [-20, +40] dB.  Default 0 dB = WDSP linear
    //                   unity = no change vs WDSP create-time defaults
    //                   (matches the lyra-cpp ship-no-setters posture
    //                   in TxChannel::open()).  Operator dials up via
    //                   the TxPanel slider for typical ESSB headroom.
    //
    // alcMaxGainLinear — `SetTXAALCMaxGain` (always-on ALC ceiling).
    //                   ⚠ LOAD-BEARING + ⚠ UNIT-CRITICAL: the WDSP
    //                   `max_gain` parameter is a LINEAR amplitude
    //                   factor, NOT dB.  On a continuous tone the
    //                   wcpagc mode-5 ALC settles at `output =
    //                   max_gain * envelope` — so a 2× difference
    //                   in this value is a 6 dB difference in
    //                   continuous-tone output power.  Default 3.0
    //                   LINEAR (= 3.0× amplitude = +9.54 dB
    //                   amplification headroom) matches the
    //                   verified reference's Setup spinner default
    //                   (integer 0..120 incr 1, default 3, passed
    //                   straight through to WDSP).  Earlier Lyra
    //                   shipped this as a dB value and called
    //                   `dbToLin(3.0) = 1.413` — capping the
    //                   ceiling at 47% of the reference's value
    //                   and producing a 5-6 dB power deficit on
    //                   continuous mic-input tones (the task #79
    //                   whistle-vs-reference RF gap; see
    //                   CLAUDE.md §15.27).  Range [0, 120]
    //                   LINEAR; operator-tunable in Settings →
    //                   TX → "ALC Max Gain" integer spinner.
    //
    // Persisted under tx/micGainDb + tx/alcMaxGainLinear in
    // QSettings.  The old `tx/alcMaxGainDb` key is intentionally
    // abandoned on upgrade — operator silently inherits the new
    // reference-faithful default (3.0 LINEAR) on first launch
    // after the §15.27 fix, which is the desired correction.
    // Live-apply: setter forwards to the registered TxControl
    // callback (which calls TxDspWorker pass-through → TxChannel WDSP
    // setter) so a slider move takes effect on the next ~2.6 ms
    // fexchange0 block (the WDSP channel mutex serialises the setter
    // against any in-flight process() call).  registerTxControl()
    // also pushes the autoloaded values ONCE at registration time so
    // the freshly-opened WDSP channel doesn't sit at the load-bearing
    // ALC = 0 dB trap waiting for the operator to nudge the slider.
    Q_PROPERTY(double micGainDb        READ micGainDb
               WRITE setMicGainDb        NOTIFY micGainDbChanged)
    Q_PROPERTY(double alcMaxGainLinear READ alcMaxGainLinear
               WRITE setAlcMaxGainLinear NOTIFY alcMaxGainLinearChanged)
    // §15.27 Commit B — ALC decay (operator-tunable wcpagc release tau).
    // Integer ms, range 1..50, default 10 — matches verified reference UI.
    Q_PROPERTY(int    alcDecayMs       READ alcDecayMs
               WRITE setAlcDecayMs       NOTIFY alcDecayMsChanged)
    // §15.27 Commit B — Leveler trio (pre-ALC amplifier stage).
    // Lyra default OFF (operator preference; reference UI ships ON).
    // Operator opt-in: tick the Enable checkbox in Settings → TX → Leveler.
    Q_PROPERTY(bool   levelerOn               READ levelerOn
               WRITE setLevelerOn               NOTIFY levelerOnChanged)
    Q_PROPERTY(double levelerMaxGainLinear    READ levelerMaxGainLinear
               WRITE setLevelerMaxGainLinear    NOTIFY levelerMaxGainLinearChanged)
    Q_PROPERTY(int    levelerDecayMs          READ levelerDecayMs
               WRITE setLevelerDecayMs          NOTIFY levelerDecayMsChanged)
    // #109 — PHROT (phase rotator) enable.  WDSP TXA speech processor
    // that symmetrizes asymmetric voice waveforms → lower peak-to-average
    // → more average talk power.  Operator on/off (mirrors the reference
    // Setup PHROT checkbox).  Default ON = WDSP/reference posture.  The
    // effective WDSP run is auto-gated OFF in digital modes (DIGU/DIGL —
    // phase rotation distorts FT8/RTTY/etc.); the toggle is the operator's
    // voice-mode intent (see applyPhrotRun).
    Q_PROPERTY(bool   phrotEnabled            READ phrotEnabled
               WRITE setPhrotEnabled            NOTIFY phrotEnabledChanged)

    // §15.31 — ATT-on-TX operator surface (RX-front-end protection on
    // key-down).  When enabled, the keydown FSM forces the HL2 step
    // attenuator to attOnTxDb so the RX ADC isn't blinded by TX
    // coupling (the XmitBit-gated case-11 wire path drives the AD9866
    // rx_gain to minimum during TX); keyup restores the operator's
    // pre-key value.  Default ENABLED / 31 dB = the verified reference's
    // HL2 working posture (Setup → General → Ant/Filters → "ATT on Tx"
    // ✓ / "ATT: 31").  Disabling removes RX-ADC protection during TX —
    // operator opt-out, default ON; the PS-A-conditional Force/Auto
    // sub-options are deferred to the v0.3 PureSignal dialog.
    Q_PROPERTY(bool attOnTxEnabled READ attOnTxEnabled
               WRITE setAttOnTxEnabled NOTIFY attOnTxEnabledChanged)
    Q_PROPERTY(int  attOnTxDb       READ attOnTxDb
               WRITE setAttOnTxDb       NOTIFY attOnTxDbChanged)
    // #94 — External TX Inhibit.  Operator hard lockout: blocks ALL keying
    // (MOX / HW-PTT / CW / TUN / TCI / Auto-ID) at the requestMox funnel, to
    // protect external gear (a 2nd SDR / scope) sharing the setup.  Engaging
    // it while keyed force-unkeys.  Persisted (fail-safe — stays locked across
    // restarts until consciously cleared); a visible indicator shows it's on.
    Q_PROPERTY(bool txInhibit READ txInhibit WRITE setTxInhibit
               NOTIFY txInhibitChanged)
    // #91 — VOX (voice-operated TX).  A pure mic-RMS gate (VoxDetector)
    // fed the EP6 mic level polled off the same 50 ms tick as HW-PTT;
    // keys through requestMoxFromVox with the "never override a manual /
    // foot-switch key" safety.  Default OFF.  voxKeying reflects whether
    // THIS gate currently holds the key (for a front-panel lamp state).
    Q_PROPERTY(bool voxEnabled READ voxEnabled WRITE setVoxEnabled
               NOTIFY voxEnabledChanged)
    Q_PROPERTY(bool voxKeying  READ voxKeying  NOTIFY voxKeyingChanged)
    // Live mic level (dBFS) the VOX gate sees — a reference for setting
    // the threshold ("speak, watch the peaks, set threshold a bit below").
    // Updated every VOX poll (~50 ms) whenever the stream is open, even
    // with VOX disabled, so it works while you're dialling it in.
    Q_PROPERTY(double voxMicDbfs READ voxMicDbfs NOTIFY voxMicDbfsChanged)
    // #169 — SWR protection (auto-cut TX on sustained high reflected
    // power).  A stream-side evaluator (off the GUI loop, beside the
    // host TX-safety timer + ATT-on-TX) samples fwd/rev power on a 50 ms
    // tick while keyed; once past key-down blanking AND above the power
    // floors AND sustained over the limit for the dwell window it CUTS
    // TX through the requestMox(false) funnel and LATCHES until the next
    // key-down (manual re-arm).  The SWR ratio is calibration-free
    // (rho=sqrt(rev/fwd)); the power floors gate out the low-power
    // regime where the ratio is just noise (the false-trigger guards).
    // swrProtectTripped / swrProtectReason are read-only lamp state for
    // the TxPanel "PROT" indicator.  Default ENABLED / 5:1.  Phase 1 =
    // Cut + Fold (both shipped; see swrProtectAction below).  The #170b
    // over-power trip is WON'T-DO: an external amp has no telemetry path
    // into Lyra (we only see the HL2 ~5 W exciter), so a measured-PO trip
    // can't protect it -- the #170a drive cap is the real protection.
    Q_PROPERTY(bool   swrProtectEnabled    READ swrProtectEnabled
               WRITE setSwrProtectEnabled    NOTIFY swrProtectEnabledChanged)
    Q_PROPERTY(double swrProtectLimit      READ swrProtectLimit
               WRITE setSwrProtectLimit      NOTIFY swrProtectLimitChanged)
    Q_PROPERTY(bool   swrProtectDuringTune READ swrProtectDuringTune
               WRITE setSwrProtectDuringTune NOTIFY swrProtectDuringTuneChanged)
    Q_PROPERTY(bool   swrProtectTripped    READ swrProtectTripped
               NOTIFY swrProtectTrippedChanged)
    Q_PROPERTY(QString swrProtectReason    READ swrProtectReason
               NOTIFY swrProtectReasonChanged)
    // #169 Phase 1b — action on a sustained over-limit: 0 = Cut (unkey),
    // 1 = Fold (monotone x0.5 drive step-down toward the floor, then
    // escalate to Cut if still over at the floor).  foldMinDrivePct is
    // the fold floor (% of full drive); below it fold escalates to Cut.
    // Both actions share the same §5 false-trigger guards; Fold reduces
    // the APPLIED drive without overwriting the operator's stored set
    // point (no-persist, like Auto-LNA), and the operator's drive is
    // restored on the next key-down (manual re-arm, never auto-recover).
    Q_PROPERTY(int    swrProtectAction     READ swrProtectAction
               WRITE setSwrProtectAction     NOTIFY swrProtectActionChanged)
    Q_PROPERTY(int    foldMinDrivePct      READ foldMinDrivePct
               WRITE setFoldMinDrivePct      NOTIFY foldMinDrivePctChanged)
    // #170a — Max TX drive cap (the primary, always-safe over-power
    // guard for low-drive amps).  A hard ceiling on TX drive %: every
    // drive write (operator slider, TUN drive, BandMemory restore, TCI
    // DRIVE) funnels through setTxDriveLevel, which clamps to this — a
    // pure preventive clamp with ZERO false-trigger risk (it never trips,
    // it just won't exceed the ceiling; needs no telemetry, no cal).
    // 100 % = no cap (the default).
    Q_PROPERTY(int    maxDrivePct          READ maxDrivePct
               WRITE setMaxDrivePct          NOTIFY maxDrivePctChanged)
    // #105 CW-1a — CW keyer config (operator surface).  Plumbed into
    // prn->cw (the composer cases 12/13/14 already consume it); INERT
    // until the keying commit sets cw_enable.  Defaults per
    // cw_tx_design.md §7.
    Q_PROPERTY(int  cwKeyerSpeedWpm  READ cwKeyerSpeedWpm
               WRITE setCwKeyerSpeedWpm  NOTIFY cwKeyerSpeedWpmChanged)
    Q_PROPERTY(int  cwKeyerWeight    READ cwKeyerWeight
               WRITE setCwKeyerWeight    NOTIFY cwKeyerWeightChanged)
    Q_PROPERTY(bool cwIambic         READ cwIambic
               WRITE setCwIambic         NOTIFY cwIambicChanged)
    Q_PROPERTY(bool cwModeB          READ cwModeB
               WRITE setCwModeB          NOTIFY cwModeBChanged)
    Q_PROPERTY(bool cwRevPaddle      READ cwRevPaddle
               WRITE setCwRevPaddle      NOTIFY cwRevPaddleChanged)
    Q_PROPERTY(bool cwStrictSpacing  READ cwStrictSpacing
               WRITE setCwStrictSpacing  NOTIFY cwStrictSpacingChanged)
    Q_PROPERTY(int  cwBreakInMode    READ cwBreakInMode
               WRITE setCwBreakInMode    NOTIFY cwBreakInModeChanged)
    Q_PROPERTY(int  cwHangDelayMs    READ cwHangDelayMs
               WRITE setCwHangDelayMs    NOTIFY cwHangDelayMsChanged)
    Q_PROPERTY(bool cwSidetoneOn     READ cwSidetoneOn
               WRITE setCwSidetoneOn     NOTIFY cwSidetoneOnChanged)
    Q_PROPERTY(int  cwSidetoneLevel  READ cwSidetoneLevel
               WRITE setCwSidetoneLevel  NOTIFY cwSidetoneLevelChanged)

    // #93/#106 — AM/SAM carrier level, operator-facing as a percent
    // (0..100 %, default 50 = WDSP's 0.5 default = standard AM).  Higher =
    // more carrier power / less sideband; 0 → suppressed-carrier (DSB-like).
    Q_PROPERTY(double amCarrierPct READ amCarrierPct
               WRITE setAmCarrierPct NOTIFY amCarrierPctChanged)
    // #107 — FM operator knobs (bite only in FM; WDSP TXA fmmod stage).
    // Deviation = FM peak deviation in Hz (default 5000 = WDSP create-time
    // default = ±5 kHz wide; 2500 = narrow).  CTCSS = sub-audible repeater
    // access tone: enable + tone Hz from the standard 67.0..254.1 table.
    // CTCSS run is mode-gated (FM only) via applyCtcssRun().
    Q_PROPERTY(double fmDeviationHz READ fmDeviationHz
               WRITE setFmDeviationHz NOTIFY fmDeviationHzChanged)
    Q_PROPERTY(bool   ctcssEnabled  READ ctcssEnabled
               WRITE setCtcssEnabled  NOTIFY ctcssEnabledChanged)
    Q_PROPERTY(double ctcssToneHz   READ ctcssToneHz
               WRITE setCtcssToneHz   NOTIFY ctcssToneHzChanged)
    // FM pre-emphasis selector: 0 = Off (flat — true bypass, for digital/data
    // + warm HF), 1 = Comm (the native 6 dB/oct 300–3000 Hz communications
    // curve; voice default).  50/75 µs broadcast curves are a later option.
    Q_PROPERTY(int    fmEmphasisMode READ fmEmphasisMode
               WRITE setFmEmphasisMode NOTIFY fmEmphasisModeChanged)

public:
    explicit HL2Stream(QObject *parent = nullptr);
    ~HL2Stream() override;

    // TX-1 component 6 — type alias for the TX I/Q source callback
    // (full contract documented at registerTxIqSource below in the
    // public-slots section).  Declared here at the top of public:
    // because Qt MOC doesn't allow `using` declarations in
    // `public slots:` sections.
    //
    // The callback runs on the EP2 wire writer thread, ONCE per
    // datagram, with a 126-element complex<float> output buffer the
    // callback should fill (samples in [-1, +1)).  Returns true if
    // it filled the buffer, false if no data was ready (the EP2
    // packer then zero-fills + increments the underrun counter).
    // MUST be non-blocking — the EP2 writer is on a hard ~2.6 ms
    // timer cadence and cannot afford to block.
    //
    // Typical wire-up: main.cpp calls registerTxIqSource with a
    // lambda that delegates to TxDspWorker::tryConsumeTxIq.
    // Caller-owned lifetime: caller must clear the source
    // (registerTxIqSource({})) BEFORE the underlying object goes
    // away.  Passing {} (empty std::function) clears the source
    // (the EP2 packer then falls through to zero-fill).
    using TxIqSource = std::function<bool(std::complex<float> *)>;

    // TX-1 component 7 — TX channel lifecycle callbacks.  The FSM
    // calls these at keydown (start) / keyup (stop) to bring the
    // WDSP TXA channel in/out of run-state.  Matches the verified
    // reference's `WDSP.SetChannelState(id(1,0), 1, 0)` (start,
    // non-blocking) and `WDSP.SetChannelState(id(1,0), 0, 1)`
    // (stop, BLOCKING flush up to 100 ms for WDSP internal drain).
    //
    // start: non-blocking; arms WDSP DSP thread for the channel.
    // stop:  BLOCKING; waits up to ~100 ms for WDSP internal flush
    //        (the ALC + bandpass + other stateful stages drain).
    //        Reference also blocks (Thread.Sleep) — accept the
    //        Qt-main-thread block during the keyup chain.
    //
    // Typical wire-up: main.cpp registers TxDspWorker's start/stop
    // pass-throughs.  Caller-owned lifetime: clear via {} before
    // the underlying object goes away.  Empty std::function = no-op.
    struct TxControl {
        std::function<void()> start;
        std::function<void()> stop;
        std::function<void(bool)> setInjectTxIq;
        // TX-1 component 8a-tx-mode — operator USB/LSB mode propagated
        // to the WDSP TXA channel.  Slaved to the operator's RX mode
        // selection (WdspEngine::modeChanged) — TX always uses the
        // same sideband as RX, which matches the reference posture and
        // the operator's "I picked USB" expectation.  Argument is the
        // WDSP TXA mode integer: 0 = LSB, 1 = USB (matches the
        // kTxaModeLSB/USB constants in tx_channel.cpp).  Non-SSB modes
        // (AM/FM/CW/DIG) translate to closest SSB sideband at the call
        // site (CWU/DIGU → USB, CWL/DIGL → LSB) — TX-1 is SSB-only so
        // operator shouldn't be keying in those modes anyway.
        std::function<void(int)> setMode;
        // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
        // Called by the Q_PROPERTY setters (operator slider/spin-box
        // moves) AND once on registerTxControl() with the autoloaded
        // persisted values so the freshly-opened TX channel doesn't
        // sit at the load-bearing WDSP defaults (especially the ALC
        // max-gain = 0 dB trap that pins the output chain).  dB
        // forwarded at the public boundary; TxChannel does dB→linear
        // before the SetTXA* call.  Empty std::function = no-op
        // (registration order: TxControl is registered AFTER
        // TxChannel::open() succeeds; if open() failed, the callbacks
        // never registered, and the Q_PROPERTY setters just persist
        // + emit without forwarding — operator state lives, channel
        // bringup applies on next radio start).
        std::function<void(double)> setMicGainDb;
        std::function<void(double)> setAlcMaxGainLinear;
        // §15.27 Commit B — ALC decay + full Leveler operator surface.
        // ALC decay: WDSP `SetTXAALCDecay` exponential-curve tau in ms.
        // Leveler: pre-ALC amplifier stage, OFF by default in Lyra (per
        // operator preference for PS quality), but operator-tunable
        // to match the verified reference's working profile (typical
        // ON / max_gain=15 LINEAR / decay=100 ms).
        std::function<void(int)>         setAlcDecayMs;
        std::function<void(bool,double)> setLevelerOn;        // (enabled, topLinear)
        std::function<void(int)>         setLevelerDecayMs;
        // #93/#106 — AM/SAM carrier fraction (0..1) → SetTXAAMCarrierLevel.
        std::function<void(double)>      setAmCarrierLevel;
        // TX-1 component 8c — operator TX bandpass.  Receives (low Hz,
        // high Hz); TxChannel internally sign-codes per the current
        // WDSP mode (USB pass-through, LSB negate-and-swap).  Called
        // by HL2Stream::setTxBwHz on every Prefs.txBandwidth change
        // AND once on registerTxControl() with the autoloaded
        // persisted value so the freshly-opened TX channel doesn't
        // sit at TxChannel::open()'s create-time (200, 3100)
        // default.  Low edge is currently fixed at 200 Hz (the
        // TxChannel SSB default; a separate operator Low spinbox is
        // a future enhancement per design doc §9.2).
        std::function<void(double, double)> setBandpass;
        // P4.b TUN re-home — drive the WDSP TXA output-side tone
        // generator (postgen / gen1) for the tune carrier.  on=true sets
        // up + runs the single tone (SetTXAPostGenMode/ToneFreq/ToneMag/
        // Run); on=false stops it.  The Thetis TUN mechanism (console.cs:
        // 30787-30801) — replaces the retired legacy DC-injection that
        // died with the EP2 packer.  Empty = no-op (pre-registration).
        std::function<void(bool)> setTune;
        // #109 — PHROT (phase rotator) run.  Symmetrizes asymmetric
        // speech to lower peak-to-average ratio (more average talk power
        // for the same ALC ceiling).  Operator on/off, mirrors the
        // reference's Setup PHROT-enable checkbox.  Empty = no-op.
        std::function<void(bool)> setPhrotRun;
        // #107 — FM operator knobs.  setFmDeviation(Hz) → SetTXAFMDeviation;
        // setCtcssFreq(Hz) → SetTXACTCSSFreq; setCtcssRun(on) →
        // SetTXACTCSSRun.  Run is mode-gated (FM only) by applyCtcssRun().
        std::function<void(int)>    setFmEmphasis;   // pre-emph: 0=Off, 1=Comm
        std::function<void(double)> setFmDeviation;
        std::function<void(double)> setCtcssFreq;
        std::function<void(bool)>   setCtcssRun;
    };

    bool    isRunning()         const { return running_.load(std::memory_order_acquire); }
    QString targetIp()          const { return targetIp_; }
    double  datagramsPerSec()   const { return dgPerSec_; }
    // Stage 2b2: counter accessors route to the Ep6RecvThread
    // free-function facades (ep6_total_datagrams / ep6_seq_errors
    // / ep6_framing_errors).  HL2Stream no longer holds these
    // atomics — the EP6 recv path owns them inside its TU.
    qint64  totalDatagrams()    const {
        return lyra::wire::ep6_total_datagrams();
    }
    // seqErrors: count of EP6 datagrams whose sequence number was
    // anything other than (previous+1).  Incremented by 1 per event
    // — NOT summed by the gap magnitude — so a one-shot sequence
    // counter reset (which the HL2 gateware can do transiently)
    // shows up as "1 seq err" instead of nonsense.  Matches the
    // reference-implementation diagnostic posture; see CLAUDE.md
    // §15.x / FEATURES.md for the rationale.
    qint64  seqErrors()         const { return lyra::wire::ep6_seq_errors(); }
    qint64  framingErrors()     const { return lyra::wire::ep6_framing_errors(); }
    double  txDatagramsPerSec() const { return txDgPerSec_; }
    qint64  txTotalDatagrams()  const { return txTotalDg_.load(std::memory_order_relaxed); }
    qint64  txSendErrors()      const { return txSendErrors_.load(std::memory_order_relaxed); }
    // Stage 2b2: rx1DbFs / micDbFs accessors retired (strict-
    // reference rule; WDSP audioDbFs is the reference-faithful
    // S-meter, Q6.5 bench instrument no longer needed once
    // Hl2Ep6MicSource ships the live AK4951 mic path).
    quint32 rx1FreqHz()         const { return rx1FreqHz_.load(std::memory_order_relaxed); }
    bool    splitEnabled()      const { return splitEnabled_.load(std::memory_order_relaxed); }
    quint32 vfoBHz()            const { return vfoBHz_.load(std::memory_order_relaxed); }
    // Effective TX carrier the wire NCO is set to (0x02/0x08/0x0a).  Tracks
    // vfoBHz_ under SPLIT, else rx1FreqHz_ — so an out-of-band TX check reads
    // the actual transmit frequency, including split operation.
    quint32 txFreqHz()          const { return txFreqHz_.load(std::memory_order_relaxed); }
    bool    ritEnabled()        const { return ritEnabled_.load(std::memory_order_relaxed); }
    int     ritOffsetHz()       const { return ritOffsetHz_.load(std::memory_order_relaxed); }
    bool    xitEnabled()        const { return xitEnabled_.load(std::memory_order_relaxed); }
    int     xitOffsetHz()       const { return xitOffsetHz_.load(std::memory_order_relaxed); }
    bool    ctuneEnabled()      const { return ctuneCenterHz_.load(std::memory_order_relaxed) != 0; }
    quint32 ctuneCenterHz()     const { return ctuneCenterHz_.load(std::memory_order_relaxed); }
    int     lnaGainDb()         const { return lnaGainDb_.load(std::memory_order_relaxed); }
    bool    autoLna()           const { return autoLnaEnabled_; }
    bool    autoLnaUndo()       const { return autoLnaUndo_; }
    int     autoLnaHoldSec()    const { return autoLnaHoldSec_; }
    bool    adcOverload()       const { return adcOverload_; }
    // Operator gain bounds = the AD9866 LNA hardware range in full-range
    // mode (HL2 wiki: code 0…60 → −12…+48 dB; frame-11 C4 bit 6 = the
    // full-range enable, which Lyra sets).  +48 is the true ceiling
    // (old Lyra software-capped at +31 on an "above this is IMD" call —
    // now the operator's choice, with Auto-LNA managing the edge).
    static constexpr int kLnaMinDb = -12;
    static constexpr int kLnaMaxDb =  48;
    // TX safety timeout operator-facing range (seconds) — exposed so the
    // Settings UI builds its SpinBox against the canonical bounds.
    static constexpr int kTxTimeoutDefaultSec = 600;   // 10 min default
    static constexpr int kTxTimeoutMinSec     = 60;    //  1 min floor
    static constexpr int kTxTimeoutMaxSec     = 1200;  // 20 min ceiling
    // #169 — SWR-protection defaults + the false-trigger-guard knobs.
    // The blank/dwell/floor knobs are QSettings-tunable (no UI in Phase
    // 1) so they can be tuned on the bench without a rebuild.
    static constexpr double kSwrProtectDefaultLimit = 5.0;   // 5:1 default
    static constexpr double kSwrProtectMinLimit     = 1.5;   // clamp floor
    static constexpr double kSwrProtectMaxLimit     = 10.0;  // clamp ceiling
    static constexpr int    kSwrEvalIntervalMs      = 50;    // 20 Hz tick
    static constexpr int    kSwrBlankMsDefault      = 200;   // key-down blank
    static constexpr int    kSwrDwellMsDefault      = 200;   // sustained dwell
    static constexpr double kSwrFwdFloorWDefault    = 1.0;   // provisional-W
    static constexpr double kSwrRevFloorWDefault    = 0.3;   // provisional-W
    static constexpr int    kFoldMinDrivePctDefault = 10;    // fold floor (%)
    static constexpr int    kMaxDrivePctDefault     = 100;   // no cap default

    // P4.b TUN — Thetis tune zero-beat offset (console.cs cw_pitch=600).
    // The tune carrier is the WDSP postgen tone at ±kTuneCwPitchHz AND a
    // TX-DDS offset of ∓kTuneCwPitchHz applied ONLY while tuning, which
    // cancel so the on-air carrier lands at the dial (zero-beat) — a
    // co-channel SSB listener hears nothing.  gen1 (main.cpp setTune)
    // and the DDS offset (txDdsHzForTune) MUST use this same value so
    // they cancel.  Public so the main.cpp postgen lambda shares it.
    static constexpr int kTuneCwPitchHz = 600;
    bool    filterBoardEnabled() const { return filterBoardEnabled_; }
    int     ocBits()             const { return ocPattern_; }
    // #199 Stage 4 — the editable OC table for the Settings "Filters / BCD"
    // tab.  The tab mutates it via OcControl's setters, then calls
    // applyOcEdit() to persist (oc/*) + re-emit the current-band pattern.
    lyra::oc::OcControl *ocControl() { return &oc_; }
    void applyOcEdit();
    // TX-0a telemetry getters — convert the raw 12-bit EP6 ADC slots
    // (written by the RX worker) on the main thread.  Sentinel raw < 0
    // → NaN ("no telemetry yet").  Formulas per the documented HL2
    // rotation; bench-verify (see the Q_PROPERTY note).
    double  hl2TempC()   const;
    double  hl2SupplyV() const;
    double  paCurrentA() const;
    double  fwdPowerW()  const;
    // Forward power in watts AFTER the per-band PWR-meter trim — the single
    // calibrated watts source used by BOTH the displayed PWR meter and the
    // watts-cap servo, so "5 W cap" == "5 W on the meter".  (fwdPowerW stays
    // RAW for the SWR ratio + CW-keying detect, where a trim would be wrong.)
    double  fwdPowerCalW() const;
    double  revPowerW()  const;
    // TX-0c-fsm — true while the radio is wire-level keyed (post-keydown
    // settle, pre-keyup-clear).  Read by the UI red-on-air indicator.
    bool    moxActive()  const { return moxActive_; }
    // #105 — CW message-level keyed state for UI consumers (the meter
    // flip).  QSK CW keys the PA at the gateware and deliberately leaves
    // the wire MOX bit (moxActive_) low, so the UI needs a separate signal
    // to reflect "the radio is transmitting CW right now".  True from just
    // before the first element of a CWX message until after the cwx_ptt
    // drop.  Does NOT track per-element keying; does NOT assert wire MOX.
    bool    cwKeyingActive() const { return cwKeyingActive_; }
    // #105 — moxActive() || cwKeyingActive() (cached).  The UI display-state
    // truth: red-on-air + panadapter TX rescale follow this so they flip for
    // QSK CW; the wire MOX bit (moxActive) is unchanged.
    bool    txDisplayActive() const { return txDisplayActive_; }
    // TX-0c-pa-debug — operator-tunable safety timeout (seconds) +
    // bypass.  Setters clamp + persist + emit changes; the FSM-side
    // keydown/keyup hooks arm/cancel the QTimer.
    int     txTimeoutSec()    const { return txTimeoutSec_; }
    bool    txTimeoutBypass() const { return txTimeoutBypass_; }
    // Task #36 — HW PTT forwarder opt-in (Q_PROPERTY getter).  Reads
    // the atomic the RX worker thread polls each EP6 datagram.
    bool    hwPttEnabled()    const {
        return hwPttEnabled_.load(std::memory_order_relaxed);
    }
    // TX-0c-pa-debug — operator-gated PA-enable mirror (Q_PROPERTY
    // getter).  Reads the wire atomic.
    bool    paEnabled() const { return paOn_.load(std::memory_order_relaxed); }
    bool    micBoost()  const { return micBoost_.load(std::memory_order_relaxed); }
    bool    bandVoltsOutput() const { return bandVolts_.load(std::memory_order_relaxed); }
    // TX-0c-pa-drive — drive DAC level (Q_PROPERTY getter).  Raw 0..255
    // wire value; UI converts to/from 0..100 %.  Reads the wire atomic.
    int     txDriveLevel() const { return txDriveLevel_.load(std::memory_order_relaxed); }
    // TX power model Stage 3 — per-band "PA Gain By Band" (Thetis port).
    // gbb is a per-band multiplier in the RadioVolume formula (default
    // 100 = neutral).  The operator measures each band into a dummy load
    // and nudges its number so the dial reads true watts.  idx is the
    // amateurBands() index (0=160m … 10=6m); out-of-range returns the
    // default.  setPaGainForBand persists + re-applies live if idx is the
    // active TX band.  Called from the PA Gain Settings tab.
    double  paGainForBand(int idx) const;
    void    setPaGainForBand(int idx, double gain);
    // TX power model Stage 3b — per-band measured Full Output (W) at full
    // drive (0 = not measured) + the single watts Max cap.  Together they
    // give the PREDICTIVE drive ceiling per band: driveCeil% =
    // 100·sqrt(capW/fullW[band]) (power∝drive², conservative — the real
    // curve is steeper so actual output stays under the cap).  Applied at
    // apply-time inside applyTxPower_, so the operator's drive setpoint is
    // preserved and moving to a less-restrictive band restores power.
    double  fullOutputForBand(int idx) const;
    void    setFullOutputForBand(int idx, double watts);
    // Per-band PWR-meter trim (1.0 = raw formula).  Corrects displayed +
    // cap-servo watts to an external watt-meter; see fwdPowerCalW().
    // Persisted meter/pwrTrim/<idx>.
    double  pwrTrimForBand(int idx) const;
    void    setPwrTrimForBand(int idx, double scale);
    double  maxOutputW() const { return maxOutputW_.load(std::memory_order_relaxed); }
    void    setMaxOutputW(double watts);
    // Cap ARM gate (2026-07-03).  The watts Max cap does NOT limit until it is
    // armed — the operator arms it explicitly AFTER calibrating Full Output.
    // This kills the "cap on but uncalibrated → silent ~30 % fallback clamp →
    // mystery-low power" trap: with no arm, wattsDriveCeilingRaw_ returns
    // no-clamp regardless of the cap value.  Auto-clears if the cap value goes
    // to 0.  QSettings tx/capArmed.
    bool    capArmed() const { return capArmed_.load(std::memory_order_relaxed); }
    Q_INVOKABLE void setCapArmed(bool on);
    // Per-band raw-watts capture, latched while a carrier is live (TUN/MOX) so
    // "Calibrate this band" can compute the trim off a held sample without a
    // second key ("capture while you tune").  capturedRawWForBand = smoothed
    // un-trimmed formula watts (0 = no sample this session); capturedDrive =
    // the drive level (0..255) that sample was taken at (>=255 = full drive,
    // the condition for the same reading to also set Full Output).
    double  capturedRawWForBand(int idx) const;
    int     capturedDriveForBand(int idx) const;
    // True when the TX mode is CW (WDSP CWL=3 / CWU=4).  CW does not produce a
    // steady tune carrier, so the PWR calibration refuses in CW.
    bool    txModeIsCw() const {
        const int m = txMode_.load(std::memory_order_relaxed);
        return m == 3 || m == 4;
    }
    // Amp-cap live indicator status (see the capStatus Q_PROPERTY): 0 hidden,
    // 1 holding a calibrated band, 2 uncalibrated → ~30 % fallback clamp.
    int     capStatus() const { return capStatus_.load(std::memory_order_relaxed); }
    // Stage B — has this band been auto-tuned (TUN servo locked) for the
    // CURRENT cap?  For the PA Gain tab's per-band "tuned" indicator.
    bool    capTunedForBand(int idx) const;
    // Reset one band's PA-Gain calibration (PA Gain tab per-row Clear): the
    // caller resets the PA-Gain + Full-Output spin boxes; this clears the
    // TUN-learned cap ceiling those spins don't own, so the cap re-learns
    // cleanly instead of holding a stale locked ceiling.
    void    clearCapLearnForBand(int idx);
    // TX-0c-tune — tune-tone armed (Q_PROPERTY getter).  Reads the
    // wire atomic.  True means "emit a 1 kHz complex tone in TX I/Q
    // whenever MOX is active"; auto-clears on the next MOX-off edge.
    bool    tuneEnabled() const { return tuneEnabled_.load(std::memory_order_relaxed); }
    // TX-1 component 6 — SSB modulator I/Q injection gate (Q_PROPERTY
    // getter).  See the Q_PROPERTY decl above for the full contract.
    bool    injectTxIq() const { return injectTxIq_.load(std::memory_order_relaxed); }
    // TX-1 component 6 — EP2-side underrun count (Q_PROPERTY getter).
    qint64  txIqUnderruns() const { return txIqUnderruns_.load(std::memory_order_relaxed); }
    // TX-1 component 5a — TR-sequencing + cos² fade durations.
    // Getters read the live operator-tuned values (or defaults if
    // unset).  See Q_PROPERTY block above for the safety rationale.
    int     moxDelayMs()      const { return moxDelayMs_;      }
    int     rfDelayMs()       const { return rfDelayMs_;       }
    int     spaceMoxDelayMs() const { return spaceMoxDelayMs_; }
    int     pttOutDelayMs()   const { return pttOutDelayMs_;   }
    int     txStopDelayMs()   const { return txStopDelayMs_;    }
    // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
    // Getters read the live operator-tuned values (or defaults if
    // unset).  See Q_PROPERTY block above for the safety rationale.
    double  micGainDb()             const { return micGainDb_;             }
    double  alcMaxGainLinear()      const { return alcMaxGainLinear_;      }
    int     alcDecayMs()            const { return alcDecayMs_;            }
    bool    levelerOn()             const { return levelerOn_;             }
    double  levelerMaxGainLinear()  const { return levelerMaxGainLinear_;  }
    int     levelerDecayMs()        const { return levelerDecayMs_;        }
    bool    phrotEnabled()          const { return phrotEnabled_;          }
    // §15.31 — ATT-on-TX operator surface getters.
    bool    attOnTxEnabled()        const { return attOnTxEnabled_;        }
    bool    txInhibit()             const { return txInhibit_;             }
    int     attOnTxDb()             const { return attOnTxDb_;             }
    // #201 — transient convert lockout: the offline MP4 converter blocks
    // keying while it runs (separate from the operator's persisted
    // txInhibit_).  Not persisted, not operator-facing.
    bool    convertLockout()        const { return convertLockout_;        }
    void    setConvertLockout(bool on) { convertLockout_ = on; }
    // #91 — VOX getters/setters.  Params echo VoxDetector::Params; the
    // detector + safety live on the Qt main thread (onVoxPoll sole
    // owner).  setVoxRxRmsLin is the anti-VOX RX-audio level feed,
    // pushed from WdspEngine::dispatchAudioFrame (atomic; audio thread).
    bool    voxEnabled()            const { return voxEnabled_;            }
    bool    voxKeying()             const { return voxKeying_;             }
    double  voxMicDbfs()            const { return voxMicDbfs_;            }
    double  voxRxDbfs()             const { return voxRxDbfs_;             }
    double  voxThresholdDbfs()      const { return voxThresholdDbfs_;      }
    double  voxAntiVoxDbfs()        const { return voxAntiVoxDbfs_;        }
    int     voxOpenMs()             const { return voxOpenMs_;             }
    int     voxHangMs()             const { return voxHangMs_;             }
    bool    voxAntiVoxOn()          const { return voxAntiVoxOn_;          }
    // Q_INVOKABLE so the front-panel VOX button can call it from QML (the
    // other VOX setters below are driven from C++ by the Settings dialog and
    // don't need it).  Without this, Stream.setVoxEnabled(...) silently threw
    // in QML → the front-panel toggle did nothing; only Settings worked.
    Q_INVOKABLE void setVoxEnabled(bool on);
    void    setVoxThresholdDbfs(double dbfs);
    void    setVoxAntiVoxDbfs(double dbfs);
    void    setVoxOpenMs(int ms);
    void    setVoxHangMs(int ms);
    void    setVoxAntiVoxOn(bool on);
    void    setVoxRxRmsLin(double rms) noexcept {
        voxRxRmsLin_.store(rms, std::memory_order_relaxed);
    }
    // Anti-VOX RX-audio level source.  Pulled on the Qt main thread in
    // onVoxPoll (50 ms) — dangle-proof: the poll only runs while the
    // stream is open, so the provider (which reads WdspEngine's atomic)
    // is never invoked after teardown.  main.cpp wires it to the engine.
    void    setVoxRxRmsProvider(std::function<double()> p) {
        voxRxRmsProvider_ = std::move(p);
    }
    // #169 — SWR-protection getters.
    bool    swrProtectEnabled()     const { return swrProtectEnabled_;     }
    double  swrProtectLimit()       const { return swrProtectLimit_;       }
    bool    swrProtectDuringTune()  const { return swrProtectDuringTune_;  }
    bool    swrProtectTripped()     const { return swrProtectTripped_;     }
    QString swrProtectReason()      const { return swrProtectReason_;      }
    int     swrProtectAction()      const { return swrProtectAction_;      }
    int     foldMinDrivePct()       const { return foldMinDrivePct_;       }
    int     maxDrivePct()           const { return maxDrivePct_;           }
    // #105 CW-1a — CW keyer config getters.
    int     cwKeyerSpeedWpm()       const { return cwKeyerSpeedWpm_;       }
    int     cwKeyerWeight()         const { return cwKeyerWeight_;         }
    bool    cwIambic()              const { return cwIambic_;              }
    bool    cwModeB()               const { return cwModeB_;               }
    bool    cwRevPaddle()           const { return cwRevPaddle_;           }
    bool    cwStrictSpacing()       const { return cwStrictSpacing_;       }
    int     cwBreakInMode()        const { return cwBreakInMode_;        }
    int     cwHangDelayMs()         const { return cwHangDelayMs_;         }
    bool    cwSidetoneOn()          const { return cwSidetoneOn_;          }
    int     cwSidetoneLevel()       const { return cwSidetoneLevel_;       }
    // #93/#106 — AM/SAM carrier level (percent).
    double  amCarrierPct()          const { return amCarrierPct_;          }
    // #107 — FM operator knobs.
    double  fmDeviationHz()         const { return fmDeviationHz_;         }
    bool    ctcssEnabled()          const { return ctcssEnabled_;          }
    double  ctcssToneHz()           const { return ctcssToneHz_;           }
    int     fmEmphasisMode()        const { return fmEmphasisMode_;        }

    // Step 3d: register a sink for DDC0 baseband IQ.  Called ONCE per
    // EP6 datagram from the RX worker thread with interleaved
    // (I,Q,…) doubles in [-1,1) (38 frames/datagram at 192 kHz nddc=4).
    // The sink runs SYNCHRONOUSLY on the RX worker thread — it is a
    // plain std::function, NOT a Qt signal, so there is no cross-thread
    // queueing on the hot path (the DSP engine consumes it inline,
    // mirroring how the working C-source reference's wire-thread pump
    // calls fexchange0).  Must be
    // set before open() spawns the worker; not changed while running.
    // Stage 2b2-fix-v3: setIqSink shim retired.  IQ-sink wiring
    // moved to main.cpp which calls `lyra::wire::register_sink(
    // router_instance(0), 0, 0, 0, …)` directly — the reference-
    // shaped LoadRouterAll-flattened registration.  No HL2Stream
    // surface; the EP6 → Router → consumer dispatch chain is now
    // exactly what `router.c` does (no Lyra-native arg-reorder
    // wrapper, no per-stream member function).

    // Stage 2b2c: `setMicConsumer` retired.  The mic-feed path is now
    // reference-faithful end-to-end — mic_source.cpp registers its
    // consumer on `ep6Thread().set_mic_sink(...)` directly, matching
    // the reference's `Inbound(inid(1,0), n_samples, double*)` shape
    // (decimated mic doubles interleaved as `{I=mic, Q=0.0}` per
    // sample).  No float bridge, no Q6.5 RMS bench instrument.

    // (§7) Step 5 RX-audio-out (pushAudio / setInjectAudio) retired — RX
    // audio reaches the AK4951 jack through WdspEngine (dispatchAudioFrame
    // → OutBound(0) → the verbatim EP2 writer sendProtocol1Samples).

public slots:
    // Open the stream to the radio at `ip`.  Creates one native UDP
    // socket, spawns the EP6 RX std::jthread + the EP2 TX std::jthread
    // (both share the socket), the TX thread sends START on entry.
    // Safe to call when already running (logs + ignores).
    void open(const QString &ip);

    // Request stop on both worker threads, join them, send STOP
    // from the main thread (best-effort), close the socket.  Safe
    // to call when already stopped (no-op).
    void close();

    // Set the DDC0 (RX1) receive frequency in Hz.  Stored in an atomic
    // the EP2 writer reads each send; takes effect on the next
    // keepalive (~2.6 ms) via the addr-2 C&C frame.  The duplex bit
    // (frame-0 C4=0x1C, sent every datagram) is what lets the gateware
    // apply post-priming RX-freq updates (CLAUDE.md §3.2).  Thread-safe.
    void setRx1FreqHz(quint32 hz);

    // Set the RX1 LNA gain (−12…+48 dB).  Stored in an atomic the EP2
    // writer encodes into the 0x14 C&C register; persisted.  Thread-safe.
    void setLnaGainDb(int db);

    // Frequency calibration — the global crystal/TCXO ppm trim applied to
    // every RX/TX freq before it reaches the wire (wire::set_freq_correction).
    // 1.0 = none (default; fresh install / Reset tunes exactly as today).
    // Clamped to a sane ±1000 ppm; NaN → 1.0.  Persisted (cal/freqCorrection),
    // and re-pushes RX+TX so a change takes effect immediately (mirror of the
    // reference's FreqCorrectionChanged re-applying to all VFOs).  Mirrors the
    // setRx1FreqHz threading contract.
    void setFreqCorrection(double factor);
    double freqCorrection() const;

    // Auto-LNA controls (main thread).  setAutoLna(false) restores the
    // operator's persisted manual gain; setAutoLna(true) hands the LNA
    // to the overload-protection loop.  Undo + hold time are persisted.
    void setAutoLna(bool on);
    void setAutoLnaUndo(bool on);
    void setAutoLnaHoldSec(int sec);

    // Set the IQ sample rate (96 / 192 / 384 kHz).  Updates the frame-0
    // C1 speed bits the EP2 writer sends each datagram; takes effect on
    // the next send.  The DSP-side rate switch (WDSP channel reopen) is
    // driven separately via WdspEngine::setSampleRate.  Thread-safe
    // (atomic).  Ignores 48 k (EP2 cadence, like old Lyra).
    void setSampleRate(int hz);

    // Enable/disable the external N2ADR filter board.  When on, the
    // per-band OC pattern is driven on frame-0 C2 and re-applied on every
    // band change; when off, C2 OC pins are cleared (0).  Persisted to
    // QSettings (hw/filterBoard).  Thread-safe (atomic C2; readout on the
    // main thread).
    void setFilterBoardEnabled(bool on);

    // ---- TX-state C&C registers (TX-0b foundation) -----------------
    // Operator / PTT-FSM RF-transmit state.  Each setter clamps + stores
    // an atomic holding the HL2+ byte encoding for its TX C&C frame,
    // verified against the HL2 wire-protocol C reference's HL2
    // main-loop emitter (WriteMainLoop_HL2) + the ak4951v4
    // gateware decode (control.v).  See
    // docs/architecture/tx1_ssb_design.md for the C-reference
    // file:line cites (provenance home per the no-attribution rule).
    // These are the SINGLE source of truth for the byte layout; a later
    // TX phase wires them into the EP2 C&C round-robin under MOX gating.
    //
    // INERT until then: the datagram emission loop reads NONE of them,
    // so at MOX=0 / PA-off the wire is byte-identical to RX.  All
    // thread-safe (lock-free atomics on x86_64).
    //
    //   setMox(on)            RF transmit active -> C0 bit 0
    //                         (C-reference: C0=XmitBit)
    //   setTxFreqHz(hz)       TX NCO -> C0 0x02/0x08/0x0a (BE, all three
    //                         carry tx[0].frequency: C-reference cases
    //                         1/5/6)
    //   setTxDriveLevel(0..255)  drive DAC; C0 0x12 C1 (C-reference;
    //                            gateware uses top 4 bits -> 16 steps)
    //   setTxStepAttnDb(0..31)   C0 0x1C C3 = (31 - db) & 0x1F  (HL2's
    //                            inverted range; C-reference uses the
    //                            same SetTxAttenData(31-x) form)
    //   setPaEnabled(on)      onboard PA -> C0 0x12 C2 bit 3 (0x08),
    //                         active-high (C-reference ApolloTuner
    //                         =0x8 netInterface.c:581; control.v:213
    //                         pa_enable<=cmd_data[19]).  NB: C2 bit 7
    //                         (0x80, VNA) must stay clear or the PA won't
    //                         key (control.v:359); C2 bit 2 (0x04) is a
    //                         SEPARATE full-duplex/T-R control, NOT a
    //                         PA-off flag.
    void setMox(bool on);
    void setTxFreqHz(quint32 hz);
    // SPLIT (PS-safe — drives only txFreqHz_/set_tx_freq; see the
    // splitEnabled Q_PROPERTY doc).  setSplitEnabled flips the TX-freq
    // source between VFO A (simplex) and VFO B (split); setVfoBHz tunes
    // the split TX VFO and re-pushes the TX NCO when split is on.
    void setSplitEnabled(bool on);
    void setVfoBHz(quint32 hz);
    // RIT/XIT — RIT offsets only the RX DDC NCO (via pushEffectiveRxFreq);
    // XIT offsets only the TX NCO (via pushEffectiveTxFreq, so PureSignal
    // tracks the XIT-shifted TX for free).  Both persisted; clamped ±9999 Hz.
    void setRitEnabled(bool on);
    void setRitOffsetHz(int hz);
    void setXitEnabled(bool on);
    void setXitOffsetHz(int hz);
    // #174 CTUNE.  setCtuneEnabled(true) locks the DDC centre at the current
    // dial; (false) releases it.  setCtuneCenterHz sets an explicit locked
    // centre (0 = off) — used by the re-centre logic later.
    Q_INVOKABLE void setCtuneEnabled(bool on);
    Q_INVOKABLE void setCtuneCenterHz(quint32 hz);
    // #174 CTUNE Stage 2 — display-context feeds for the Thetis edge model
    // (smooth-scroll / far-jump re-center / bCanFitInView / zoom force-center).
    // Plumbed from main.cpp: span from WdspEngine.spanHz, signed filter edges
    // from the operator mode + filterLow/rxBandwidth.  setCtuneDisplaySpanHz
    // re-evaluates the edge model (a zoom-in may no longer fit the passband).
    void setCtuneDisplaySpanHz(double hz);
    void setCtuneFilterEdges(int lowHz, int highHz);
    void setTxDriveLevel(int level);
    void setTxStepAttnDb(int db);
    void setPaEnabled(bool on);
    void setMicBoost(bool on);
    void setBandVoltsOutput(bool on);
    // TX-0c-tune — arm/disarm the tune-tone generator.  The EP2 writer
    // fills TX-I/TX-Q with a 1 kHz complex tone @ 0.95 full scale only
    // when (mox_ on the wire) AND (this flag set).  Auto-cleared on
    // every moxActiveChanged(false) edge by the ctor's self-wired
    // safety so a stray MOX click after a tune session can't re-emit
    // the carrier.  Not persisted — operator must explicitly arm.
    void setTuneEnabled(bool on);

    // ---- TX-0c-fsm: MOX/PTT sequencer (single funnel) ----------------
    // Operator/CAT/PTT/TUN intent gets funneled here.  Internally drives
    // a TR-sequenced state machine that times the wire MOX-bit flip
    // around the ATT-on-TX RX-protect raise/restore.  All delays via
    // QTimer::singleShot on this QObject's thread (no sleeps); the wire
    // worker thread reads mox_ / txStepAttnDb_ atomics as before.
    //
    //  Keydown (RX → MOX_TX):
    //      save txStepAttnDb_ → force ATT-on-TX (31)
    //      → wait kMoxDelayMs (15 ms)
    //      → mox_ = true       (C0 bit 0 on wire next datagram)
    //      → wait kRfDelayMs   (50 ms; rf settle, hot-switch-safe)
    //      → emit moxActiveChanged(true)
    //
    //  Keyup   (MOX_TX → RX):
    //      wait kSpaceMoxDelayMs (13 ms; re-key window — if requestMox
    //          fires true during this, collapse: stay TX, no wire flip)
    //      → mox_ = false
    //      → wait kPttOutDelayMs (5 ms)
    //      → restore txStepAttnDb_
    //      → emit moxActiveChanged(false)
    //
    // Bench discipline: at TX-0c-fsm the MOX bit lands on the wire but
    // paOn_ stays false, so the HL2's T/R relay clicks audibly but the
    // PA bias never enables — STILL ZERO RF.  TX-0c-pa-debug adds the
    // operator-gated PA enable.
    //
    // Re-entrancy: requestMox() is the SINGLE funnel; calling it during
    // a transition just updates the intent and the sequencer reads it
    // at each step (standard HF-rig re-key collapse, cancel mid-
    // keydown, etc., all work).
    //
    void requestMox(bool on);                       // PttSource::Manual

public:
    // Task #33 — PTT source tracking (Thetis-faithful TCIPTT-vs-MOX
    // pattern, see docs/refs/mshv_tci/README.md §"Thetis TCI TX
    // architecture").  The 2-arg overload records which subsystem
    // initiated the press; the 1-arg overload (the public slot above:
    // legacy callers — mic-PTT, FSM keyup, operator MOX button,
    // TX-timeout, CAT) is a wrapper that records PttSource::Manual.
    // Wire behaviour is IDENTICAL across sources — same TR-delay
    // chain, same wire MOX bit, same ATT-on-TX safety raise; the
    // source is metadata that pttSource() exposes for subsystems that
    // need it (e.g. the TCI server's clearQueuedTxAudio on TCI-PTT
    // release vs operator-PTT release, the future panadapter TX-state
    // badge styling).
    //
    // The 2-arg overload + the PttSource enum + the pttSource()
    // getter live in this PUBLIC (not public-slots) section because
    // moc cannot parse a non-method declaration (enum class, return-
    // type-with-namespace) inside a `slots:` block.  The Q_INVOKABLE
    // wrappers below stay reachable from QueuedConnection dispatches.
    enum class PttSource : std::uint8_t {
        Manual = 0,   // operator MOX button, CAT, TX-timeout, FSM cancel
        HwPtt  = 1,   // EP6 ptt_in foot-switch / hand-mic / mic button
        Tci    = 2,   // TCI client (MSHV TX_AUDIO_STREAM workflow)
        Serial = 3,   // serial PTT line — a digital app (WSJT-X / VarAC /
                      // fldigi) asserts RTS/DTR on a (virtual) COM port,
                      // polled via QSerialPort (lyra::cat::SerialPtt).
        Vox    = 4,   // #91 voice-operated TX — the VoxDetector mic-RMS gate
                      // keys through this source (never overrides a Manual /
                      // HwPtt key; releases only its own key).
        Keyer  = 5,   // #89 voice keyer / clip playback-OTA — ClipRecorderPlayer
                      // keys through this source while a clip transmits (same
                      // own-key discipline as Vox: never overrides a Manual /
                      // HwPtt key; releases only its own key; a manual key or
                      // Stop aborts the clip).
        // Forward-compat: Cw lands with its subsystem, same pattern.
    };
    void requestMox(bool on, PttSource source);     // explicit

    // Read the source of the currently-active (or most-recently-active)
    // PTT request.  Stable across the keydown/keyup transitions; gets
    // reset to PttSource::Manual after the FSM fully settles back to
    // RX (the "no active TX" state).  Subscribers can read this in
    // their moxActiveChanged slot to branch on source.
    PttSource pttSource() const noexcept {
        return pttSource_.load(std::memory_order_acquire);
    }

public slots:
    // Q_INVOKABLE thin wrappers used by cross-thread QueuedConnection
    // dispatches (the rx-worker thread's HW-PTT forwarder; the TCI
    // server's binaryMessageReceived TRX handler) so the source is
    // recorded without needing the PttSource enum to be a registered
    // Qt metatype.  Wire behaviour is IDENTICAL to requestMox(bool,
    // PttSource) — pure source-tagging convenience.
    Q_INVOKABLE void requestMoxFromHwPtt(bool on);
    Q_INVOKABLE void requestMoxFromTci(bool on);
    Q_INVOKABLE void requestMoxFromSerialPtt(bool on);   // serial PTT input
    Q_INVOKABLE void requestMoxFromVox(bool on);         // #91 VOX gate edge

    // #171 — straight-key / external-keyer key edge from a serial COM line
    // (SerialCwKey).  The host drives tx[0].cwx / cwx_ptt directly off the
    // operator's (or the keyer's) own timing — NO host iambic — mirroring the
    // #105 CWX path; cwx_ptt is held across the CW break-in hang on key-up so
    // inter-element gaps don't drop TX.  CW mode only.  Reference posture:
    // Thetis cwkeyer.cs reads a key on a COM port's CTS/DSR and feeds the
    // keyer; on HL2 a serial key can't reach the gateware key jack, so the
    // host keys via cwx (the HL2 KEY jack stays the lower-jitter path).
    Q_INVOKABLE void requestCwKeyFromSerial(bool down);

    // #175 bench (increment 2a) — render `callsign` as a waterfall-ID raster
    // (lyra::dsp::WaterfallId, 48 kHz to match the TXA in-rate), clear + push
    // it into the TCI TX-audio bridge as the SOLE TX source, and return the
    // burst length in ms (0 = blank call / empty render).  QML keys via
    // requestMox(true) then schedules requestMox(false) after the returned
    // ms + a short tail.  Bench prereqs (operator-set for now): mic source =
    // TCI + a flat/digital TX path (DIGU).  The full self-keyed / auto-flat /
    // mic-mute / cadence / 11M-lockout orchestration is increment 2-full;
    // this is the manual-trigger raster-validation cut.
    Q_INVOKABLE int pushWaterfallIdAudio(const QString &callsign, double level,
                                         bool lsb = false);

    // ---- TX-0c-pa-debug: host-side safety timeout ----------------
    // setTxTimeoutSec clamps to kTxTimeoutMinSec..kTxTimeoutMaxSec
    // (60..1200 s; 1..20 min UI), persists to QSettings, and emits
    // txTimeoutSecChanged.  If the radio is currently keyed the
    // timer is re-armed with the new full duration (operator intent
    // = "I want N more seconds from now").
    //
    // setTxTimeoutBypass false → true cancels any active safety
    // timer (operator turned safety OFF mid-key); true → false re-
    // arms with full duration when keyed (operator turned safety
    // back ON, applies immediately).
    void setTxTimeoutSec(int sec);
    void setTxTimeoutBypass(bool on);

    // Task #36 — Hardware PTT forwarder opt-in (default OFF).  See
    // Q_PROPERTY decl above for the §10 Q#1 safety rationale.  Thread-
    // safe (atomic store + signal emit); turning OFF resets the
    // internal edge-detect so the next ON-edge doesn't fire on a
    // stale "was already pressed" mismatch.
    void setHwPttEnabled(bool on);

    // TX-1 component 5a — TR-sequencing + cos² envelope tuning.
    // Each setter clamps to a sane operator range, persists to
    // QSettings (tx/trSeq/<key>), and emits the matching changed
    // signal.  Live-apply: the FSM picks up the new value at the
    // next QTimer::singleShot scheduling boundary (so a change made
    // while keyed takes effect on the NEXT MOX edge).
    void setMoxDelayMs(int ms);
    void setRfDelayMs(int ms);
    void setSpaceMoxDelayMs(int ms);
    void setPttOutDelayMs(int ms);
    // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
    // Each clamps to its declared range, persists under tx/<key>,
    // emits the changed signal, and forwards to the registered
    // TxControl callback (no-op if TxControl not yet registered).
    void setMicGainDb(double db);
    void setAlcMaxGainLinear(double linear);
    // §15.27 Commit B — ALC decay + Leveler trio.  Each clamps to its
    // declared range, persists under tx/<key>, emits the changed signal,
    // and forwards to the registered TxControl callback.
    void setAlcDecayMs(int decay_ms);
    void setLevelerOn(bool on);
    void setLevelerMaxGainLinear(double linear);
    void setLevelerDecayMs(int decay_ms);
    void setPhrotEnabled(bool on);   // #109 phase rotator on/off
    // §15.31 — ATT-on-TX enable + dB value.  Persist under tx/<key>,
    // emit the changed signal, and (if keyed right now) re-apply to the
    // wire live so the operator sees the effect mid-TX.
    void setAttOnTxEnabled(bool on);
    void setTxInhibit(bool on);   // #94 External TX Inhibit (hard keying lockout)
    void setAttOnTxDb(int db);
    // #169 — SWR-protection operator surface.  Each persists under
    // tx/<key>, emits the changed signal, and (enable/limit) re-arms or
    // stands down the live evaluator if keyed right now.
    void setSwrProtectEnabled(bool on);
    void setSwrProtectLimit(double ratio);
    void setSwrProtectDuringTune(bool on);
    void setSwrProtectAction(int action);   // 0 = Cut, 1 = Fold
    void setFoldMinDrivePct(int pct);        // fold floor, 1..90 %
    void setMaxDrivePct(int pct);            // #170a drive cap, 1..100 %
    // #105 CW-1a — CW keyer config setters.  Each clamps, stores, persists
    // (tx/cw/*), emits, and (if prn exists) pushes the whole CW block to
    // prn->cw via applyCwConfigToPrn().  INERT on the wire until the keying
    // commit sets cw_enable.
    void setCwKeyerSpeedWpm(int wpm);
    void setCwKeyerWeight(int weight);
    void setCwIambic(bool on);
    void setCwModeB(bool on);
    void setCwRevPaddle(bool on);
    void setCwStrictSpacing(bool on);
    void setCwBreakInMode(int mode);   // 0=QSK 1=Semi 2=Manual
    void setCwHangDelayMs(int ms);
    void setCwSidetoneOn(bool on);
    void setCwSidetoneLevel(int level);
    // #105 CW-2 — the single CW pitch (shared with WdspEngine::cwPitchHz /
    // the RX pitch / the marker).  Drives the keyed CW carrier offset so the
    // carrier paints on the marker, and the gateware HW sidetone freq.  Wired
    // from WdspEngine::cwPitchChanged in main.cpp; clamps 200..1500.
    void setCwPitchHz(int hz);
    // #105 CW-3a — host software keyer (CWX).  sendCw queues `text` to the
    // morse keyer at the operator WPM/weight; no-op outside CW mode (the
    // gateware ignores the CW bits unless cw_enable, which is CW-mode only).
    // abortCw flushes the queue + drops the key.  The keyer drives
    // tx[0].cwx (per element) + tx[0].cwx_ptt (held per message); the
    // gateware keys the carrier + HW sidetone.  QSK (default break-in):
    // host stays RX, exactly like the paddle.  Thread-safe.
    Q_INVOKABLE void sendCw(const QString& text);
    Q_INVOKABLE void abortCw();
    // #93/#106 — AM/SAM carrier level (0..100 %); persists, emits, forwards
    // the 0..1 fraction to SetTXAAMCarrierLevel.
    void setAmCarrierPct(double pct);
    // #107 — FM operator knobs (persist + emit + forward via TxControl).
    void setFmDeviationHz(double hz);   // clamp 1000..6000
    void setCtcssEnabled(bool on);      // mode-gated run via applyCtcssRun()
    void setCtcssToneHz(double hz);     // snapped to the standard tone table
    void setFmEmphasisMode(int mode);   // 0=Off, 1=Comm; forward via TxControl

    // TX-1 component 8a-tx-mode — push WDSP TXA mode (0=LSB, 1=USB)
    // to the TX channel via the registered TxControl.setMode callback.
    // Driven by main.cpp from the operator's RX-mode change handler
    // (WdspEngine::modeChanged) — TX always tracks RX sideband.  Not
    // persisted here (the source-of-truth lives in WdspEngine via its
    // own mode QSettings); HL2Stream just relays the WDSP integer to
    // the TxControl callback so the TX channel sees the operator's
    // current sideband selection.  No-op if TxControl not registered
    // or if the callback is null.
    void setTxMode(int wdspMode);

    // P4.b TUN — TX NCO freq for the current state: the dial when not
    // tuning, dial ∓ kTuneCwPitchHz while tuning (USB −, LSB +) so the
    // ±kTuneCwPitchHz postgen tone nets to a zero-beat carrier at the
    // dial.  Mirrors Thetis's tx_freq computation gated on chkTUN
    // (console.cs:32574-32587).
    int txDdsHzForTune(quint32 dialHz) const;
    // #105 CW-2 — VFO − DDS carrier offset for the current TX mode (CWU
    // +pitch / CWL −pitch / other 0), == WdspEngine::cwMarkerOffsetForMode,
    // using the live cwPitchHz_.  Added to the non-tuning TX NCO so the keyed
    // CW carrier lands on the marker.
    int cwTxCarrierOffsetHz() const;

    // P4.b TUN display-honesty — the NCO−dial offset the panadapter crop
    // needs (= txDdsHzForTune(dial) − dial, freq-independent): ∓kTuneCwPitchHz
    // while tuning (USB −, LSB +), 0 otherwise.  Emitted via
    // txAnalyzerOffsetChanged → WdspEngine::setTxAnalyzerOffsetHz.
    int txAnalyzerOffsetHz() const;

    // TX-1 component 8c / Task #53 — operator TX bandpass.  Forwards
    // (low, high) Hz straight to TxControl.setBandpass.  Pulled
    // through from Prefs.filterLow (shared with the RX bandpass) +
    // Prefs.txBandwidth — see the QObject::connect block in
    // main.cpp.  TxChannel internally sign-codes per WDSP mode
    // (USB pass-through, LSB negate-and-swap), so we always pass
    // positive edges.  No-op if hz<=0 or no TxControl registered.
    void setTxBandpass(int lowHz, int highHz);
    void setTxStopDelayMs(int ms);

    // TX-1 component 6 — SSB I/Q injection gate.  See the
    // Q_PROPERTY decl above for the contract; defaults FALSE,
    // not persisted, set by the FSM at keydown / cleared at keyup.
    // Component 7 wires this: HL2Stream's setter ALSO forwards to
    // the registered TxControl.setInjectTxIq callback so the
    // TxDspWorker producer side is gated in lockstep with the
    // EP2-packer consumer side.  Single point of control from
    // the FSM.
    void setInjectTxIq(bool on);

    // TX-1 component 7 — register TX channel lifecycle callbacks
    // (start, stop, setInjectTxIq).  See TxControl struct doc above.
    // Caller-owned lifetime; clear via {} before the underlying
    // object goes away.  Mutex-protected (same pattern as
    // registerTxIqSource).
    void registerTxControl(TxControl ctl);

    // TX-1 component 6 — register a TX I/Q source for the EP2
    // packer to pull from when (MOX && injectTxIq) is true on a
    // given datagram.  See the public TxIqSource type alias above
    // for the callback contract.
    void registerTxIqSource(TxIqSource src);

signals:
    void runningChanged();
    void statsChanged();
    void rx1FreqChanged();
    void splitEnabledChanged();
    void vfoBHzChanged();
    void ritChanged();   // RIT enable and/or offset
    void xitChanged();   // XIT enable and/or offset
    void ctuneChanged();           // #174 CTUNE engage / locked-centre changed
    void rxShiftHzChanged(double hz);  // #174 WDSP RX demod shift (-> WdspEngine)
    void freqCorrectionChanged(double factor);  // freq-cal ppm trim (2026-07-03)
    void lnaGainChanged();
    // Emitted ONLY by the manual setLnaGainDb() path (operator slider /
    // wheel / per-band restore) — NOT by Auto-LNA's roaming.  Lets
    // BandMemory save the operator's per-band manual set point without
    // capturing an auto-roamed value.
    void lnaSetByOperator(int db);
    void autoLnaChanged();
    void adcOverloadChanged();
    void filterBoardChanged(bool on);
    void ocBitsChanged(int pattern);
    void logLine(QString line);
    // TX-0c-fsm — fires when the wire MOX state changes after a TR-delay
    // transition completes (true at end of keydown rf_delay; false at end
    // of keyup ptt_out_delay).  Does NOT fire on mid-transition states.
    void moxActiveChanged(bool on);
    // #105 — CW message-level keyed state changed (see cwKeyingActive()).
    void cwKeyingActiveChanged(bool on);
    // #105 — display TX-state (moxActive || cwKeyingActive) changed.
    void txDisplayActiveChanged(bool on);
    // Fires ONCE per requestMox(true) call (MOX button click, TUN arm,
    // space-bar press — any path that signals operator intent to key).
    // Lets the TX panel paint a short "press-intent" indicator (orange
    // pulse) so the operator gets instant feedback even when their press
    // is shorter than the ~65 ms TR-delay window and moxActiveChanged
    // therefore never fires.  Decoupled from FSM outcome — fires even if
    // the keydown gets cancelled mid-mox_delay; the pulse decays via the
    // UI timer regardless.
    void moxIntentPulse();
    // TX-0c-pa-debug — fires on operator-set changes to the safety
    // timeout + bypass.  Settings UI binds via Q_PROPERTY; persistence
    // lives in the setter.
    void txTimeoutSecChanged(int sec);
    void txTimeoutBypassChanged(bool on);
    // Task #36 — HW PTT forwarder opt-in changed (Settings checkbox,
    // persistence reload, or stop()-safety reset).
    void hwPttEnabledChanged(bool on);
    // TX-0c-pa-debug — operator-gated PA-enable changed (via Settings
    // checkbox, persistence reload, or stream open/close safety clear).
    void paEnabledChanged(bool on);
    void micBoostChanged(bool on);
    void bandVoltsOutputChanged(bool on);
    // TX-0c-pa-drive — operator-tunable drive DAC level changed (via
    // Settings SpinBox or persistence reload).  Raw 0..255 wire value.
    void txDriveLevelChanged(int level);
    // TX-0c-tune — tune-tone armed state changed (via TX panel button,
    // operator unarm, or the moxActiveChanged(false) safety auto-clear).
    void tuneEnabledChanged(bool on);
    // P4.b TUN display-honesty — the TX-analyzer NCO−dial offset (Hz)
    // changed.  Wired to WdspEngine::setTxAnalyzerOffsetHz so the panadapter
    // crop renders the TUN carrier at its true RF (the dial) rather than
    // NCO-relative.  Fires from setTuneEnabled / setTxMode (the only places
    // the tune DDS offset can change).  0 when not tuning.
    void txAnalyzerOffsetChanged(int hz);
    // TX-1 component 6 — SSB I/Q injection gate edge.
    void injectTxIqChanged(bool on);
    // TX-1 component 5a — TR-sequencing + cos² envelope tuning.
    // Emitted on every successful setter call (post-clamp + persist).
    // QML Settings UI binds to these to refresh spin-box values when
    // changed programmatically (e.g. Restore Defaults button).
    void moxDelayMsChanged(int ms);
    void rfDelayMsChanged(int ms);
    void spaceMoxDelayMsChanged(int ms);
    void pttOutDelayMsChanged(int ms);
    void txStopDelayMsChanged(int ms);
    // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
    void micGainDbChanged(double db);
    void alcMaxGainLinearChanged(double linear);
    // §15.27 Commit B.
    void alcDecayMsChanged(int decay_ms);
    void levelerOnChanged(bool on);
    void levelerMaxGainLinearChanged(double linear);
    void levelerDecayMsChanged(int decay_ms);
    void phrotEnabledChanged(bool on);   // #109 phase rotator
    // §15.31 — ATT-on-TX operator surface.
    void attOnTxEnabledChanged(bool on);
    void txInhibitChanged(bool on);   // #94
    void voxEnabledChanged(bool on);  // #91
    void voxKeyingChanged(bool on);   // #91
    void voxMicDbfsChanged(double dbfs); // #91 — live mic level for threshold
    void voxRxDbfsChanged(double dbfs);  // #91 — live RX-audio level for anti-VOX
    void attOnTxDbChanged(int db);
    // #169 — SWR protection.
    void swrProtectEnabledChanged(bool on);
    void swrProtectLimitChanged(double ratio);
    void swrProtectDuringTuneChanged(bool on);
    void swrProtectTrippedChanged(bool tripped);
    void swrProtectReasonChanged(const QString& reason);
    void swrProtectActionChanged(int action);
    void foldMinDrivePctChanged(int pct);
    void maxDrivePctChanged(int pct);
    void maxOutputWChanged(double watts);   // Stage 3b — watts Max cap
    void capArmedChanged(bool on);          // cap arm gate (2026-07-03)
    void capStatusChanged();                // TX-panel CAP chip (0/1/2)
    // Fires once per auto-cut (distinct from txTimeoutFired) so the UI
    // can toast "TX cut: SWR x.x:1".
    void swrProtectCut(const QString& reason);
    // #105 CW-1a — CW keyer config.
    void cwKeyerSpeedWpmChanged(int wpm);
    void cwKeyerWeightChanged(int weight);
    void cwIambicChanged(bool on);
    void cwModeBChanged(bool on);
    void cwRevPaddleChanged(bool on);
    void cwStrictSpacingChanged(bool on);
    void cwBreakInModeChanged(int mode);
    void cwHangDelayMsChanged(int ms);
    void cwSidetoneOnChanged(bool on);
    void cwSidetoneLevelChanged(int level);
    // #93/#106 — AM/SAM carrier level (percent).
    void amCarrierPctChanged(double pct);
    void fmEmphasisModeChanged(int mode);   // FM pre-emphasis: 0=Off, 1=Comm
    void fmDeviationHzChanged(double hz);   // #107
    void ctcssEnabledChanged(bool on);      // #107
    void ctcssToneHzChanged(double hz);     // #107
    // Fires once when the safety timeout actually expires and the FSM
    // auto-clears MOX.  Useful for a status-bar toast / log highlight;
    // the actual MOX-off is driven through requestMox(false) regardless.
    void txTimeoutFired();

private slots:
    void onStatsTick();
    void onAutoLnaTick();
    // Stage 2b2-fix-v2 — HW-PTT-in poll slot (Qt main thread,
    // ~20 Hz).  Reads `lyra::wire::prn->ptt_in` level, edge-
    // detects, fires requestMoxFromHwPtt() on transitions.
    // Reference-faithful: replaces the prior wire-side push
    // callback with FSM-side polling per `console.cs`-class
    // PTT consumer pattern.  Opt-in gated by hwPttEnabled_.
    void onHwPttPoll();
    void onVoxPoll();       // #91 — VOX gate tick (own slot on the HW-PTT timer)
    void applyVoxParams();  // #91 — push the 5 member params into vox_
    void onFatalError(QString reason);

private:
    // Mirror a safety-critical TX/wire event to BOTH the in-app log
    // dock (emit logLine) AND the Qt logging surface (qInfo / qCritical
    // → stderr + any installed file handler).  Operator-facing record
    // of "what did the radio do, when?" survives a crash where the in-
    // app log dock contents are lost; complements §3.9-style discipline
    // for events the operator needs to be able to reconstruct after the
    // fact (PA-on/off edges, wire MOX edges, safety-timer fires, etc.).
    //
    // safetyLog → qInfo()    (normal-but-important event)
    // fatalLog  → qCritical() (FATAL: stream died on its own)
    //
    // Both still emit logLine() so the in-session UI experience is
    // unchanged — this only adds a second sink.
    void safetyLog(const QString& msg);
    void fatalLog(const QString& msg);

    // (§7) txWorkerLoop retired — EP2 writer is the verbatim
    // sendProtocol1Samples thread (prn->hWriteThreadMain).
    // TX-0c-fsm — sequencer steps (all run on this QObject's thread via
    // QTimer::singleShot, NOT on the wire workers).  Each step re-reads
    // requestedMox_ so an operator change mid-transition (cancel during
    // keydown, re-key during keyup space window) takes effect cleanly.
    void fsmAdvance();        // entry — picks keydown vs keyup vs idle
    // §15.25 keydown/keyup ordering CORRECTED 2026-06-09 per Thetis
    // console.cs:30269-30383 read-3×-verified.  The prior codification
    // had inject-IQ-clear-FIRST (keyup) and TXA-start-BEFORE-rf_delay
    // (keydown) — both reference-divergent.  Corrected to:
    //   keydown: mox_=true → rf_delay → setInject(true) → startFn
    //   keyup:   stopFn (BLOCKING) → mox_delay → setInject(false) +
    //            mox_=false → ptt_out_delay → settled
    // fsmKeyupInFlight retired — its body folded into fsmKeyupTxOff
    // alongside setInjectTxIq(false) per Thetis' AudioMOXChanged +
    // HdwMOXChanged + cmaster.Mox grouping at console.cs:30373-30376.
    void fsmKeydownPostMox(); // after moxDelayMs — wire MOX bit set
                              //   (equivalent of Thetis HdwMOXChanged +
                              //   cmaster.Mox).  Schedules fsmKeydown-
                              //   Settled via rfDelayMs_ QTimer.
    void fsmKeydownSettled(); // after rfDelayMs — setInjectTxIq(true)
                              //   (Thetis AudioMOXChanged) + startFn()
                              //   (Thetis SetChannelState(id(1,0),1,0))
                              //   + emit moxActiveChanged(true).
    void fsmKeyupPostSpace(); // after spaceMoxDelayMs — stopFn()
                              //   BLOCKING (Thetis SetChannelState(
                              //   id(1,0),0,1)).  Schedules fsmKeyupTxOff
                              //   via txStopDelayMs_ (= Thetis mox_delay)
                              //   so WDSP TXA cos² downslew tail reaches
                              //   the wire while XmitBit is still 1.
                              //   (Or collapse-stay-TX if re-keyed.)
    void fsmKeyupTxOff();     // after txStopDelayMs_ — setInjectTxIq(false)
                              //   (Thetis AudioMOXChanged false) +
                              //   mox_.store(false) (Thetis HdwMOXChanged
                              //   + cmaster.Mox false).  Schedules
                              //   fsmKeyupSettled via pttOutDelayMs_.
    void fsmKeyupSettled();   // after pttOutDelayMs — restore step-att,
                              //   restore OC pattern, emit
                              //   moxActiveChanged(false).
    // TX-0c-pa-debug — arm the safety timer (called at moxActive=true
    // edge if bypass is off) / cancel it (at moxActive=false edge).
    // No-ops if bypass is on.
    void armTxSafetyTimer();
    void cancelTxSafetyTimer();
    // QTimer expiry slot — driven by tx_safety_timer_; routes through
    // requestMox(false) so the standard keyup TR-delay chain runs
    // cleanly (no shortcut on the wire bit).
    void onTxSafetyTimeout();
    // #169 — SWR-protection evaluator (stream-side, beside the TX-safety
    // timer).  arm: on key-down clear the latch + reset the tick/dwell
    // counters + start the 50 ms eval tick.  disarm: on key-up stop the
    // tick (the trip latch persists so the operator sees WHY TX cut until
    // they re-key).  eval: the periodic tick implementing the four
    // guards — key-down blanking, fwd/rev power floors, sustained dwell —
    // then CUT via requestMox(false) + latch.
    void armSwrProtect();
    void disarmSwrProtect();
    void evalSwrProtect();
    void tripSwrProtect(double swr);   // Cut action (+ fold-floor escalate)
    void foldSwrProtect(double swr);   // Fold action — x0.5 drive step-down
    void foldWattsProtect(double fwdW, double capW);  // Stage 3b-2 reactive cap
    // #169 Phase 1b — apply a TX drive level to the wire WITHOUT
    // persisting it (mirrors applyLnaGainNoPersist): used by Fold so its
    // transient step-downs never overwrite the operator's stored drive
    // set point; the operator's drive is restored on the next key-down.
    void applyDriveLevelNoPersist(int level);
    // #109 — push the EFFECTIVE PHROT run (operator intent AND not-digital)
    // to the WDSP TX channel.  Called from the operator toggle, every TX
    // mode change, and channel open.  Auto-OFF in DIGU/DIGL (mode 7/9).
    void applyPhrotRun();
    // #107 — push the EFFECTIVE CTCSS run (ctcssEnabled_ && FM mode) to the
    // WDSP TX channel.  Called from the operator toggle, every setTxMode
    // edge, and channel open.  CTCSS is an FM-only modulator stage (WDSP
    // mode 5); non-FM modes force run=0.
    void applyCtcssRun();
    // SPLIT — push the EFFECTIVE TX freq to the wire: VFO B when split is
    // on, else VFO A (rx1).  The SINGLE TX-freq writer for split paths so
    // PureSignal's feedback DDCs (which read tx[0].frequency) follow it.
    void pushEffectiveTxFreq();
    // Writes the RX DDC NCOs from rx1FreqHz_ + (ritEnabled ? ritOffsetHz : 0)
    // — the single RX-NCO writer, mirror of pushEffectiveTxFreq.  Called by
    // setRx1FreqHz (every dial gesture) and the RIT setters.
    void pushEffectiveRxFreq();
    // #170a — the Max-TX-drive cap as a raw 0..255 ceiling (100 % → 255).
    // Header-safe integer rounding (no <cmath>/<algorithm> dependency);
    // maxDrivePct_ is already clamped 1..100 by its setter + the ctor.
    int  maxDriveRaw() const {
        const int r = (maxDrivePct_ * 255 + 50) / 100;
        return r < 1 ? 1 : (r > 255 ? 255 : r);
    }
    // TX power model Stage 2/3 — the single power chokepoint.  Takes the
    // operator's REQUESTED drive raw (0..255, already cap-clamped),
    // computes the Thetis HL2 RadioVolume from the active TX band's PA
    // gain, and drives BOTH the AD9866 drive_level byte (round(255·RV))
    // AND the ChannelMaster txgain digital fixed gain (SetTXFixedGain(RV)).
    // Every drive-byte write + every TX band change funnels here.  No-op
    // until create_xmtr (set_drive_level/SetTXFixedGain are guarded).
    void   applyTxPower_(int requestedRaw);
    // Thetis HL2: RadioVolume = min(drive% · gbb[band]/100 / 93.75, 1).
    double radioVolumeFor_(int requestedRaw) const;
    // Stage 3b/B — the watts-cap drive ceiling (0..255 raw) for the active
    // TX band: the TUN-learned ceiling if the band was tuned for the current
    // cap, else the conservative fallback (safe-under).  255 when the cap is
    // off.  applyTxPower_ clamps the requested raw to this.
    int    wattsDriveCeilingRaw_() const;
    // The watts cap only actually LIMITS when a value is set AND it is armed
    // (arm gate, 2026-07-03).  Every cap-limiting path checks this.
    bool   capActive_() const {
        return maxOutputW_.load(std::memory_order_relaxed) > 0.0
            && capArmed_.load(std::memory_order_relaxed);
    }
    // Stage B — the conservative (safe-under-the-cap) drive ceiling for an
    // un-tuned band, + the TUN auto-learn servo.
    int    wattsFallbackCeilingRaw_(int band, double capW) const;
    void   tickCapServo_(double fwdW);
    bool   capTunedFor_(int band, double capW) const;
    // Recompute the OC pattern (frame-0 C2) from the current band +
    // filter-board-enabled state.  Main thread only.  `transmitting`
    // picks the TX-side or RX-side per-band OC table (n2adrOcPattern
    // in bands.cpp): on N2ADR boards the LPF bits are identical and
    // only the 3 MHz HPF differs (RX-only), but the call still wires
    // correctly for other filter boards.  Defaults to false so non-FSM
    // callers (ctor, freq change, board enable) emit the RX pattern;
    // the FSM keydown/keyup hooks pass true/false to switch at the
    // right TR-sequenced moments.
    void updateOcPattern(bool transmitting = false);
    // #199 Stage 2 — OC wire sink (the write-tail of the old updateOcPattern)
    // + the N2ADR preset seeder for the editable OcControl table.
    void setOcOutput(quint8 bits);
    void seedOcN2adr();
    void loadOcSettings();        // override the N2ADR seed from oc/* if present
    void saveOcSettings() const;  // persist the editable OC table to oc/*
    // Apply an LNA gain to the live wire value WITHOUT persisting it —
    // used by Auto-LNA so its transient adjustments never overwrite the
    // operator's manual set point in QSettings.  Emits lnaGainChanged
    // so the slider / dB readout / S-meter compensation all track.
    void applyLnaGainNoPersist(int db);

    SocketHandle         socket_ = kInvalidSocket;
    // Step 14 Stage 1 — sockaddr_in storage for lyra::wire::metis_wire_bind().
    // The wire layer stores the dest_addr POINTER caller-owned (see
    // wire/MetisFrame.h:43-49); its target must outlive the EP2/EP6
    // threads.  Holding the bytes here ties their lifetime to the
    // HL2Stream object.  Opaque 16-byte buffer keeps winsock2.h out of
    // this header (mirror of the existing SocketHandle pattern at line
    // 88-92).  sockaddr_in is 16 bytes (2 sin_family + 2 sin_port + 4
    // sin_addr + 8 sin_zero) on every Windows/POSIX target; 4-byte
    // alignment (uint32_t array) satisfies its strictest field
    // (uint32_t sin_addr).  The .cpp reinterpret_casts to sockaddr_in.
    std::uint32_t        destStorage_[4] = {};
    std::atomic<bool>    running_{false};
    // Stage 2b2: totalDg_/seqErrors_/framingErrors_/windowDg_
    // atomics retired — Ep6RecvThread owns them as TU-scope atomics
    // (ep6_total_datagrams() / ep6_seq_errors() /
    // ep6_framing_errors() / ep6_drain_window_datagrams() facades).
    // Task #48 diagnostic — Windows system-wide UDP RX counters
    // (GetUdpStatisticsEx, AF_INET).  Snapshotted at stream start
    // so close() logs the per-session delta:
    //   * udpStartInDatagrams_ — total UDP datagrams the IP layer
    //     handed to the kernel UDP code (system-wide).
    //   * udpStartInNoPorts_   — datagrams to an unlistened port.
    //   * udpStartInErrors_    — datagrams the kernel could not
    //     deliver for reasons OTHER than no-listener; on Windows
    //     this INCLUDES socket-receive-buffer-full discards (the
    //     interesting one for diagnosing where EP6 drops happen).
    // Compare Δ udpInErrors vs Lyra-side seqErrors:
    //   * Δ udpInErrors > 0  → drops in kernel UDP layer (our RX
    //     thread stalled, rcvbuf overflowed).  Lyra-side issue.
    //   * Δ udpInErrors == 0 but seqErrors > 0 → drops upstream
    //     of kernel (NIC ring, NIC HW, fabric).  Operator-side
    //     fix: NIC Properties → Advanced → Receive Buffers ↑,
    //     Interrupt Moderation off, etc.
    // -1 sentinel = snapshot failed.  See snapshotUdpStatsV4()
    // in hl2_stream.cpp for the full interpretation table.
    std::atomic<qint64>  udpStartInDatagrams_{-1};
    std::atomic<qint64>  udpStartInNoPorts_{-1};
    std::atomic<qint64>  udpStartInErrors_{-1};
    // Stage 2b2: windowDg_ atomic retired (see facade comment above).
    std::atomic<qint64>  txTotalDg_{0};
    std::atomic<qint64>  txWindowDg_{0};
    std::atomic<qint64>  txSendErrors_{0};
    // Stage 2b2: txSeq_ retired — metis_write_frame() owns the wire
    // sequence counter via the TU-scope MetisOutBoundSeqNum, shared
    // with the priming path for PureSignal-correct posture.
    // Stage 2b2: rx1DbFs_ / micDbFs_ atomics retired — strict-
    // reference rule: WDSP audioDbFs is the reference-faithful
    // S-meter path; the Q6.5 mic bench instrument is no longer
    // needed once Hl2Ep6MicSource ships the live AK4951 mic path.
    // Step 4: DDC0 (RX1) receive frequency, Hz.  Read by the EP2 writer
    // each send.  Default 7.074 MHz (40m FT8) so first launch lands on
    // a known-active spot.  std::atomic<quint32> is lock-free on x86_64.
    std::atomic<quint32> rx1FreqHz_{7074000};
    // Frequency-calibration factor (crystal ppm trim).  Authoritative copy;
    // the wire-side value lives in wire::g_freq_correction.  1.0 = none.
    std::atomic<double> freqCorrection_{1.0};
    // RX1 LNA gain (dB, −12…+48).  Read by the EP2 writer each cadence
    // tick.  Default +31 dB — old Lyra's practical-max set point: a
    // strong starting gain that's still below the +48 hardware ceiling,
    // with Auto-LNA (default on) backing it off on overload.  Overridden
    // from QSettings in the ctor once the operator sets it.
    std::atomic<int>     lnaGainDb_{31};
    // ADC-overload telemetry + Auto-LNA (standard HF SDR pattern, gain-sense).
    // adcOverloadNow_ holds the ADC0 overload bit from the MOST RECENT
    // EP6 address-0 status frame (direct overwrite — NOT a window-OR
    // latch).  The 400 ms tick samples it INSTANTANEOUSLY, matching the
    // reference HL2 read loop's getAndResetADC_Overload poll: a window-
    // OR over the ~1000 address-0 frames per 400 ms over-reported on a
    // strong front end (one micro-clip pinned overload every cycle →
    // gain driven to the floor).  adcOverload_ / overloadLevel_ /
    // autoLna*_ are main-thread state owned by onAutoLnaTick().
    // Stage 2b2: adcOverloadNow_ retired — Auto-LNA's onAutoLnaTick()
    // reads prn->adc[0].adc_overload direct (the EP6 status path
    // writes it; reference posture per the HL2 read loop's single-
    // frame assignment at networkproto1.c:502).
    // TX-0a — raw 12-bit EP6 telemetry slots, written by the RX worker
    // (direct overwrite, most-recent wins), read by the main-thread
    // getters.  −1 = no data yet.  std::atomic<int> is lock-free on
    // x86_64 so the getters take no lock on the hot path.
    // EP6 status-slot raw atomics, slot map per reference HL2 read
    // loop (`MetisReadThreadMainLoop_HL2`, networkproto1.c:498-525).
    // The slot→atomic mapping uses the GENERIC HPSDR atomic names
    // (exciter_power, user_adc0, etc.) — but on HL2 specifically those
    // atomics carry DIFFERENT physical quantities, reinterpreted by
    // the reference's C# consumer layer (`console.cs:24937-24941`):
    //
    //   slot 0x08 C1:C2 → exciter_power  → temperature      (HL2)
    //   slot 0x08 C3:C4 → fwd_power      → fwd_power
    //   slot 0x10 C1:C2 → rev_power      → rev_power
    //   slot 0x10 C3:C4 → user_adc0      → PA current       (HL2)
    //   slot 0x18      → zeros / debug    (per gateware Verilog
    //                                      `_hl2src/hl2_rtl_control.v:475`
    //                                      "Unused in HL")
    //
    // The reference deliberately decodes by the GENERIC slot names,
    // then reinterprets at the display layer per-model.  Lyra mirrors
    // this exactly: decode atomics named per the reference C atomics,
    // then reinterpret in the HL2-specific accessors below.
    //
    // Supply voltage is NOT in the HL2 EP6 status rotation per reference.
    // The reference's status-bar "Volts" label slot (console.cs:26758-
    // 26761) is REUSED on HL2 to display temperature with a "C" suffix.
    // Lyra's hl2SupplyV() therefore returns NaN; the UI banner shows
    // "n/a" for V on HL2, matching reference behavior.
    // Stage 2b2: tel*Raw_ atomics retired — telemetry accessors
    // (hl2TempC/paCurrentA/fwdPowerW/revPowerW) and the diagnostic
    // raw-int log read prn->tx[0].exciter_power/fwd_power/rev_power
    // and prn->user_adc0 direct (the single telemetry state-of-
    // record after Ep6RecvThread is the live writer).  Reference
    // posture: int=0 at startup until the first telemetry frame
    // arrives (no -1 sentinel; the formulas natively cope with
    // raw=0 → quiet temp/voltage rather than NaN).
    bool                 adcOverload_    = false;
    int                  overloadLevel_  = 0;      // 0..5 confirm accumulator
    bool                 autoLnaEnabled_ = false;
    bool                 autoLnaUndo_    = true;
    int                  autoLnaHoldSec_ = 4;      // "Undo N s" creep interval
    QTimer               autoLnaTimer_;
    QElapsedTimer        holdClock_;               // since last auto gain change
    bool                 recovering_     = false;  // creeping gain back up
    // External filter board (N2ADR).  ocC2_ is the frame-0 C2 byte the
    // EP2 writer reads each send (= 7-bit OC pattern << 1; C2[0] stays 0).
    // filterBoardEnabled_ / ocPattern_ are main-thread state (the
    // Q_PROPERTY getters + Settings UI); ocC2_ is the atomic wire value.
    std::atomic<std::uint8_t> ocC2_{0};
    // Frame-0 C1 IQ speed bits [1:0]: 1=96k, 2=192k(default), 3=384k.
    // Read by the EP2 writer each send.
    std::atomic<std::uint8_t> sampleRateBits_{0x02};
    // ---- TX-0b: RF-transmit state (foundation; NOT yet emitted) ----
    // Encodings per the verified HL2 wire-protocol C reference's HL2
    // main-loop emitter + ak4951v4 gateware decode (see the setter
    // doc block for cite pointers).  INERT — read by no emission path yet;
    // at MOX=0/PA-off the datagram is byte-identical to RX.  A later TX
    // phase wires these into the C&C round-robin under MOX gating.
    std::atomic<bool>    mox_{false};          // C0 bit 0 (RF transmit)
    std::atomic<quint32> txFreqHz_{7074000};   // TX NCO -> 0x02/0x08/0x0a
    // SPLIT — VFO B (the split TX VFO) + the split-on flag.  txFreqHz_
    // tracks vfoBHz_ when splitEnabled_, else rx1FreqHz_ (the simplex
    // mirror in setRx1FreqHz, now gated on !splitEnabled_).
    std::atomic<bool>    splitEnabled_{false};
    std::atomic<quint32> vfoBHz_{7074000};
    // RIT/XIT — signed Hz offsets, ±9999 Hz, default disabled / 0.  RIT
    // folds into the RX DDC NCO (pushEffectiveRxFreq); XIT into the TX NCO
    // (pushEffectiveTxFreq → set_tx_freq, so PS tracks the XIT-shifted TX).
    std::atomic<bool>    ritEnabled_{false};
    std::atomic<qint32>  ritOffsetHz_{0};
    std::atomic<bool>    xitEnabled_{false};
    std::atomic<qint32>  xitOffsetHz_{0};
    std::atomic<quint32> ctuneCenterHz_{0};   // #174 CTUNE locked DDC centre (0=off)
    std::atomic<double>  ctuneDispSpanHz_{0.0}; // #174 CTUNE Stage2 — display span (Hz, from WdspEngine.spanHz; 0=unknown→full IQ)
    std::atomic<int>     ctuneFiltLoHz_{0};   // #174 CTUNE Stage2 — signed RX filter low edge (Hz)
    std::atomic<int>     ctuneFiltHiHz_{3000};// #174 CTUNE Stage2 — signed RX filter high edge (Hz)
    std::atomic<int>     txDriveLevel_{0};      // 0..255; 0x12 C1 (16 steps)
    // TX power model Stage 3 — per-band "PA Gain By Band" (Thetis port).
    // 11 HF/6m bands (amateurBands() order, 160m..6m); default 100 =
    // neutral.  Atomic per element: read on the power chokepoint
    // (applyTxPower_), written by the PA Gain Settings tab.  Loaded from
    // QSettings pa_gain/<band>/gain in the ctor.
    static constexpr int    kNumPaGainBands = 11;
    static constexpr double kPaGainDefault  = 100.0;
    std::atomic<double>  paGainByBand_[kNumPaGainBands];
    // Stage 3b — per-band measured full output (W); 0 = not measured.
    // QSettings pa_gain/<band>/fullW.  + the single watts Max cap (0 =
    // off).  QSettings tx/maxOutputW.
    std::atomic<double>  fullOutputWByBand_[kNumPaGainBands];
    std::atomic<double>  maxOutputW_{0.0};
    // TX-panel CAP chip status computed in applyTxPower_ (0/1/2 — see the
    // capStatus Q_PROPERTY).  Recomputed at the drive chokepoint so it
    // tracks every drive / PA-gain / band / cap change with no extra timer.
    std::atomic<int>     capStatus_{0};
    // Stage B — per-band AUTO-LEARNED drive ceiling for the watts cap.
    // The TUN servo walks this UP from the conservative fallback until the
    // PWR meter reaches the cap, then locks it (approach-from-below = never
    // overshoots).  capCeilRaw_ = the learned raw drive ceiling (-1 = unset);
    // capCeilCapW_ = the cap value it was learned for (-1 = none).  A drive→
    // watts curve that varies per band + the 16-step DAC make a static model
    // unreliable; measuring it live is the only way to land exactly at the
    // cap on every band.  QSettings pa_gain/<band>/capCeilRaw + capCeilCapW.
    std::atomic<int>     capCeilRaw_[kNumPaGainBands];
    std::atomic<double>  capCeilCapW_[kNumPaGainBands];
    // Stage B — "parked on the step just under the cap" latch (coarse-DAC
    // straddle).  Once the servo steps down off an over-cap reading it sets
    // this so it won't climb back over the cap; cleared on a cap change or a
    // reading that falls well under the cap.  QSettings pa_gain/<band>/capSettled.
    std::atomic<bool>    capServoSettled_[kNumPaGainBands];
    // Per-band PWR-meter trim (1.0 = raw formula, no correction).  Applied
    // by fwdPowerCalW() so the displayed watts + the cap servo share ONE
    // calibrated basis.  QSettings meter/pwrTrim/<idx>.
    std::atomic<double>  pwrTrimByBand_[kNumPaGainBands];
    // Cap ARM gate (2026-07-03) — the cap only limits when armed.  Default
    // false.  QSettings tx/capArmed.  See capActive_() / capArmed().
    std::atomic<bool>    capArmed_{false};
    // Per-band raw-watts capture latch (session-only, NOT persisted).  Updated
    // every keyed eval tick with steady RF: an EWMA of the un-trimmed formula
    // watts + the drive level (0..255) it was taken at.  Feeds "Calibrate this
    // band" (capture-while-you-tune) — 0 = no sample captured this session.
    std::atomic<double>  capturedRawW_[kNumPaGainBands];
    std::atomic<int>     capturedDrive_[kNumPaGainBands];
    int                  capServoTicks_ = 0;   // throttle (let the meter settle)
    static constexpr int    kCapServoStepTicks = 3;    // step every 3 ticks (150 ms)
    static constexpr int    kCapServoStepRaw   = 3;    // ~1.2 % drive per step
    static constexpr double kWattsFallbackExp  = 2.0;  // conservative un-tuned-band exp
    // Last TX band applyTxPower_ ran for — so a freq dial tick only
    // re-applies the power when the band (gbb) actually changed.
    std::atomic<int>     lastTxBand_{-2};
    std::atomic<int>     txStepAttnDb_{0};      // 0..31 dB; 0x1C C3 (31-db)
    std::atomic<int>     txMode_{1};            // 0=LSB 1=USB; mirror of the WDSP TXA mode, for the TUN DDS-offset sign (txDdsHzForTune)
    // #105 CW-2 — the single live CW pitch (shared with the RX pitch +
    // marker; fed from WdspEngine::cwPitchHz via setCwPitchHz, wired in
    // main.cpp).  Drives the keyed CW carrier offset + the HW sidetone freq.
    std::atomic<int>     cwPitchHz_{600};
    std::atomic<bool>    paOn_{false};          // 0x12 C2 bit 3 (active-high)
    std::atomic<bool>    micBoost_{false};      // 0x12 C2 bit 0 (+20 dB HW boost)
    std::atomic<bool>    bandVolts_{false};     // 0x00 C3 bit 3 (band-volts enable)
    // TX-0c-tune — operator-armed tune-tone generator.  Atomic so the
    // EP2 writer thread reads it lock-free on every datagram.  The NCO
    // phase below is owned single-thread by that writer (no atomic
    // needed since only that thread mutates it).
    std::atomic<bool>    tuneEnabled_{false};

    // Task #36 — Hardware PTT input forwarder.
    //
    // hwPttEnabled_ is the operator opt-in gate.  Default false (see
    // Q_PROPERTY for the §10 Q#1 phantom-TX rationale).  Written from
    // Qt main thread via setHwPttEnabled() (atomic store), read from
    // the RX worker thread each datagram (atomic load).
    //
    // lastPttIn_ is the edge-detect memory, owned by the RX worker
    // thread (single-thread access, no atomic needed).  Reset to
    // false when the gate is turned off so the next enable doesn't
    // fire a spurious release-edge on whatever the wire happens to
    // be reading at the moment.
    std::atomic<bool>    hwPttEnabled_{false};
    // Stage 2b2-fix-v2: HW-PTT edge-detect state lives here on
    // the Qt main thread (sole writer/reader = onHwPttPoll +
    // open()'s seeding line — single-threaded discipline by
    // construction).  Replaces the prior `thread_local bool prev`
    // inside the wire-side `hw_ptt_sink` lambda; reference-
    // faithful "FSM polls prn->ptt_in on its own clock" posture.
    bool                 lastHwPttLevel_ = false;
    QTimer               hwPttTimer_;

    // ── #91 VOX ── all Qt-main-thread state (onVoxPoll sole owner)
    // except voxRxRmsLin_ (written by the audio thread → atomic).
    lyra::tx::VoxDetector vox_;
    bool                 voxEnabled_       = false;   // operator opt-in
    bool                 voxOwnsKey_       = false;   // this gate holds the key
    bool                 voxKeying_        = false;   // mirror for the lamp
    double               voxMicDbfs_       = -200.0;  // live mic level readout
    double               voxRxDbfs_        = -200.0;  // live RX-audio (anti-VOX) readout
    double               voxThresholdDbfs_ = -35.0;
    double               voxAntiVoxDbfs_   = -45.0;
    int                  voxOpenMs_        = 10;
    int                  voxHangMs_        = 300;
    bool                 voxAntiVoxOn_     = true;
    std::atomic<double>  voxRxRmsLin_{0.0};           // anti-VOX RX-audio feed (push)
    std::function<double()> voxRxRmsProvider_;        // anti-VOX pull (main-thread)

    // ── TX-1 component 6: SSB I/Q injection (wire-inert default) ──
    // Gate atomic — see Q_PROPERTY decl + setInjectTxIq() doc for
    // the contract.  Defaults FALSE = no SSB I/Q on the wire even
    // with MOX keyed; the FSM (follow-up commit) sets it TRUE at
    // keydown after rf_delay + WDSP TXA channel start.
    std::atomic<bool>    injectTxIq_{false};
    // EP2-side underrun count.  See Q_PROPERTY decl for semantics.
    std::atomic<qint64>  txIqUnderruns_{0};
    // Registered source callback + the lock that protects it from
    // mid-flight clearing by a registerTxIqSource({}) call.  The
    // EP2 writer thread reads/calls this; the Qt main thread
    // writes it via registerTxIqSource.  Mutex is fine here —
    // contention is essentially zero (source is registered at
    // app boot, possibly cleared at app shutdown, never touched
    // on the hot path).
    TxIqSource           txIqSource_;
    mutable std::mutex   txIqSourceMtx_;

    // TX-1 component 7 — TX channel lifecycle callbacks (start, stop,
    // setInjectTxIq).  See TxControl struct + registerTxControl() doc
    // above for the contract.  Same mutex pattern as txIqSourceMtx_
    // (contention zero — registration is app-boot, never on hot path).
    TxControl            txControl_;
    mutable std::mutex   txControlMtx_;
    // ---- TX-0c-fsm: MOX/PTT sequencer state (single-thread, this thread) -
    // Operator/CAT intent — last requested MOX state.  Sequencer reads
    // it at every timer step so an operator change mid-transition takes
    // effect on the next scheduled boundary (cancel keydown → exit clean;
    // re-key during keyup space window → collapse-stay-TX).
    bool                 requestedMox_ = false;
    // True while any TR-delay timer chain is in flight; gates re-entry
    // into the FSM so a single intent edge schedules exactly one chain.
    bool                 fsmRunning_   = false;
    // Saved operator step-att before ATT-on-TX raise on keydown, so the
    // keyup restore lands the operator's pre-key set point (today always
    // 0 on RX, but forward-compat for a future operator-tunable RX
    // step-att).  Touched only by the FSM on this thread.
    int                  savedTxStepAttn_ = 0;
    // Task #33: source of the active (or most-recently-active) PTT.
    // Atomic because subscribers (e.g. TciServer) may read it from
    // any thread in a moxActiveChanged slot.  Set on the rising edge
    // of a keydown intent; reset to Manual when the FSM fully settles
    // back to RX.  See requestMox(on, PttSource) docstring above.
    std::atomic<PttSource> pttSource_{PttSource::Manual};
    // Wire-level MOX truth — true once the post-keydown rf_delay has
    // settled, cleared at the end of the keyup ptt_out_delay.  Drives
    // the UI red-on-air indicator (NOT the toggle button's checked
    // state).  Touched only on this thread.
    bool                 moxActive_    = false;
    // #105 — CW message-level keyed state for the UI meter flip (parallel
    // to moxActive_; QSK CW never raises the wire MOX).  Touched only on
    // this QObject's thread via setCwKeyingActive() (the CwKeyer's onState
    // callback marshals to here with a queued invoke).
    bool                 cwKeyingActive_ = false;
    // #105 — cached moxActive_ || cwKeyingActive_ for the UI display state
    // (recomputed by updateTxDisplayActive on either edge).
    bool                 txDisplayActive_ = false;
    // #105 — paddle/straight-key CW detect: hang countdown (in onHwPttPoll
    // ~50 ms ticks) holding cwKeyingActive after forward power drops, so the
    // meter doesn't chatter between keyed elements.  0 = not holding.
    int                  cwKeyHangTicks_  = 0;
    // TX-0c-pa-debug — host-side safety timeout state.  Both ints/
    // bools are single-thread (this QObject's thread) — set by the
    // operator via the Settings UI, read by the FSM keydown hook to
    // decide whether to arm tx_safety_timer_ on each keydown edge.
    int                  txTimeoutSec_     = 600;   // 10 min default
    bool                 txTimeoutBypass_  = false;
    // Single-shot timer driving the auto-MOX-off on safety expiry.
    // Owned by this QObject (parent = this), runs on this thread.
    QTimer              *txSafetyTimer_    = nullptr;
    bool                 filterBoardEnabled_ = false;
    int                  ocPattern_ = 0;   // live 7-bit J16 pattern
    // #199 — the editable OC table + emit choke (Stage 1 core).  Stage 2
    // routes updateOcPattern() through it; seeded with the N2ADR preset so
    // an enabled board reproduces today's per-band pattern byte-for-byte.
    lyra::oc::OcControl  oc_;
    // Step 3d: DDC0 IQ sink (DSP engine).  Set once before open();
    // read on the RX worker thread.  Default-empty = no DSP wired.
    // Stage 2b2: iqSink_ retired — setIqSink() now registers on
    // router_instance(0) port=0/call_idx=0 directly (the live
    // Ep6RecvThread→Router dispatch path).  No member needed.
    // Stage 2b2c: the entire mic-forwarder state retired from
    // HL2Stream — micConsumer_ / micConsumerMtx_ /
    // micDecimationFactor_ / micDecimationCount_ all live elsewhere
    // now.  The decimation factor is at TU scope in RadioNet.cpp
    // (`lyra::wire::mic_decimation_factor`), updated by
    // HL2Stream::setSampleRate; the decimation count_ persists
    // inside Ep6RecvThread's mic-tap state (reference posture per
    // networkproto1.c:566-577).  The mic-block delivery shape and
    // consumer registration live on Ep6RecvThread directly
    // (`set_mic_sink(...)`, signature
    // `(int n_samples, const double* iq_pairs)` matching the
    // reference's `Inbound(inid(1,0), n_samples, double*)` —
    // mic_source.cpp drives it).
    // (§7) old EP2 RX-audio injection ring (audioBuf_/audioMtx_/audioCv_/
    // injectAudio_) retired — RX audio is OutBound(0) via dispatchAudioFrame.
    double               dgPerSec_   = 0.0;
    double               txDgPerSec_ = 0.0;
    QString              targetIp_;
    QTimer               statsTimer_;
    // Measures the ACTUAL interval between stats ticks so dg/s is
    // correct even when the 5 Hz timer fires late (e.g. main-thread
    // paint load) — dividing by a fixed period inflated the reading
    // and made the WIRE-OK band flap.
    QElapsedTimer        statsClock_;

    static constexpr quint16 kRadioPort    = 1024;
    static constexpr int     kMetisDgSize  = 1032;   // 8 hdr + 2*512 USB
    static constexpr int     kStatPeriodMs = 200;    // 5 Hz UI updates
    // EP2 keepalive cadence: 48000 audio samples/sec ÷ 126 samples
    // per datagram (63 LRIQ tuples × 2 USB frames) = 380.95 dg/sec
    // → 2.6316 ms period.  Same cadence the verified HL2 wire-
    // protocol references all fire.
    static constexpr int     kEp2RateHz    = 380;
    // Auto-LNA overload-poll cadence (matches the reference ~400 ms loop).
    static constexpr int     kAutoLnaPeriodMs = 400;
    // Once recovery begins (after the hold time confirms the band is
    // clear), creep gain back up at this brisker fixed cadence so the
    // operator isn't left deaf for minutes — pull-DOWNs still honor the
    // operator's hold-time setting; only the pull-UP is sped up.
    static constexpr int     kAutoLnaRecoverMs = 1000;
    // ---- TX-0c-fsm: TR-sequencing delays (ms) -----------------------
    // Defaults pulled from the operator's working-station DB export
    // (HL2+/AK4951 bench-validated, hot-switch-safe for typical 1 kW
    // solid-state HF linears):
    //   moxDelayMs_       = 15  (RX-protect → wire MOX bit)
    //   rfDelayMs_        = 50  (MOX → RF settle; AMP HOT-SWITCH SAFE)
    //   spaceMoxDelayMs_  = 13  (keyup re-key window before MOX-off)
    //   pttOutDelayMs_    = 5   (MOX-clear → final cleanup)
    //
    // Promoted to mutable instance fields (component 5a) so the
    // operator can tune via Settings UI to match their specific amp's
    // T/R relay switching spec.  Persisted under tx/trSeq/<key>.
    //
    // ⚠ HOT-SWITCH PROTECTION: reducing rfDelayMs_ below the external
    // amp's T/R relay settle spec risks PA damage from RF into mid-
    // transition relays.  Defaults are bench-safe; Settings UI
    // tooltips carry the warning.
    //
    // Per project-memory §6.7: per-radio defaults eventually live in a
    // capabilities struct; HL2+ values here are the v0.2 starting set.
    static constexpr int     kDefaultMoxDelayMs      = 15;
    static constexpr int     kDefaultRfDelayMs       = 50;
    static constexpr int     kDefaultSpaceMoxDelayMs = 13;
    static constexpr int     kDefaultPttOutDelayMs   = 5;
    // TX-1 component 7 — keyup-side in-flight clear delay (between
    // TX-DSP-stop blocking flush and wire-MOX-clear).  Reference-
    // faithful default 10 ms, matches the verified reference's
    // keyup `mox_delay`.  Lets UDP datagrams already-sent or in-OS-
    // buffer (carrying MOX=1 + non-zero samples) actually reach +
    // be processed by the HL2 BEFORE we flip the wire state.
    static constexpr int     kDefaultTxStopDelayMs   = 10;
    // Operator-tuning bounds.  Below kMinFsmDelayMs lands you in
    // hot-switch-unsafe territory for typical SS amps; above
    // kMaxFsmDelayMs is unphysical (PTT events don't last that long).
    static constexpr int     kMinFsmDelayMs = 1;
    static constexpr int     kMaxFsmDelayMs = 500;
    // Live values — written by setters on the Qt main thread, read by
    // the FSM (also on the Qt main thread via QTimer::singleShot).
    // Single-threaded access; no atomic needed.
    int moxDelayMs_      = kDefaultMoxDelayMs;
    int rfDelayMs_       = kDefaultRfDelayMs;
    int spaceMoxDelayMs_ = kDefaultSpaceMoxDelayMs;
    int pttOutDelayMs_   = kDefaultPttOutDelayMs;
    int txStopDelayMs_   = kDefaultTxStopDelayMs;

    // ---- TX-1 component 8a: operator-tunable WDSP TXA gain stages -
    //
    // Defaults match the verified reference's Setup-load values
    // EXACTLY (§15.27 reference-faithful posture, "do as the
    // reference does, no variation"):
    //
    //   alcMaxGainLinear_ = 3.0 LINEAR  (= 3.0× amplitude = +9.54 dB
    //                                    amplification headroom).
    //                                    Reference UI: integer
    //                                    spinner 0..120 incr 1
    //                                    default 3, passed straight
    //                                    through to SetTXAALCMaxGain
    //                                    with NO unit conversion.
    //                                    WDSP create-time is 1.0
    //                                    linear (= 0 dB) which pins
    //                                    the TXA output chain at
    //                                    a hard 0 dB ALC ceiling
    //                                    regardless of mic level —
    //                                    the load-bearing trap that
    //                                    THIS default lifts.
    //   micGainDb_        = 0.0 dB     — WDSP create-time unity.
    //                                    Matches lyra-cpp's ship-
    //                                    no-setters posture for what
    //                                    the channel sees at boot.
    //                                    Operator dials up via
    //                                    TxPanel for typical ESSB
    //                                    headroom.
    //
    // Persisted under tx/micGainDb + tx/alcMaxGainLinear in
    // QSettings.  The old `tx/alcMaxGainDb` key (dB semantics) is
    // intentionally abandoned on §15.27 upgrade — operator silently
    // inherits the new reference-faithful default on first launch.
    static constexpr double  kDefaultMicGainDb         =  0.0;
    static constexpr double  kDefaultAlcMaxGainLinear  =  3.0;
    // Operator-tuning bounds.  Mic gain range matches the verified
    // reference's Default TX profile exactly (Max=+40, Min=-90) so a
    // Lyra-cpp slider drag covers the same operator-facing travel an
    // operator coming from a reference-pattern radio is used to.
    // Min=-90 is a deep attenuator (essentially mute for typical mic
    // levels) — useful for bench-test with a hot signal generator +
    // matches the reference's profile floor.
    // ALC max-gain bounds match the reference's spinner range
    // EXACTLY (0..120 LINEAR).  0 = ALC cannot amplify (Lyra's old
    // -3 dB lower bound was conservative); 120 = the reference's
    // ESSB-friendly upper bound (+41.6 dB amplification ceiling —
    // far past where most operators run but matches the reference
    // so the operator-tuning surface is identical).
    static constexpr double  kMinMicGainDb           = -90.0;
    static constexpr double  kMaxMicGainDb           = +40.0;
    static constexpr double  kMinAlcMaxGainLinear    =   0.0;
    static constexpr double  kMaxAlcMaxGainLinear    = 120.0;
    // ALC decay (exponential-curve tau in ms).  Reference UI spinner
    // range 1..50 incr 1 default 10; Lyra mirrors exactly.
    static constexpr int     kDefaultAlcDecayMs      =  10;
    static constexpr int     kMinAlcDecayMs          =   1;
    static constexpr int     kMaxAlcDecayMs          =  50;
    // Leveler trio defaults match the reference UI's ship-defaults
    // EXACTLY (Max Gain 0..20 LINEAR default 15; Decay 1..5000 ms
    // default 100).  The ENABLE checkbox is the one operator-
    // preference override: reference UI ships Enabled=true, Lyra
    // ships Enabled=false (operator's explicit PS/predistortion
    // quality concern about always-on leveler interaction).
    // Operator opts in via Settings → TX → Leveler.
    static constexpr bool    kDefaultLevelerOn              = false;
    static constexpr double  kDefaultLevelerMaxGainLinear   =  15.0;
    static constexpr double  kMinLevelerMaxGainLinear       =   0.0;
    static constexpr double  kMaxLevelerMaxGainLinear       =  20.0;
    static constexpr int     kDefaultLevelerDecayMs         = 100;
    static constexpr int     kMinLevelerDecayMs             =   1;
    static constexpr int     kMaxLevelerDecayMs             = 5000;
    // Live values — written by the Q_PROPERTY setters on the Qt
    // main thread, forwarded to the registered TxControl callbacks
    // (which run on the same thread; TxChannel does the WDSP setter
    // call under its own channelMtx_).
    double micGainDb_              = kDefaultMicGainDb;
    double alcMaxGainLinear_       = kDefaultAlcMaxGainLinear;
    int    alcDecayMs_             = kDefaultAlcDecayMs;
    bool   levelerOn_              = kDefaultLevelerOn;
    double levelerMaxGainLinear_   = kDefaultLevelerMaxGainLinear;
    int    levelerDecayMs_         = kDefaultLevelerDecayMs;
    // #109 — PHROT (phase rotator) enable.  Default ON = WDSP create-time
    // / reference posture (zero behaviour change for existing users; the
    // toggle just hands over control).  Forwarded to TxControl.setPhrotRun.
    static constexpr bool kDefaultPhrotEnabled = true;
    bool   phrotEnabled_          = kDefaultPhrotEnabled;
    // §15.31 — ATT-on-TX operator surface.  Default ENABLED / 31 dB
    // (kAttOnTxDb) = the reference HL2 working posture.  Touched by the
    // Q_PROPERTY setters on the Qt main thread; the FSM reads them on
    // the same thread (keydown/keyup run via QTimer::singleShot here).
    static constexpr bool kDefaultAttOnTxEnabled = true;
    bool   attOnTxEnabled_         = kDefaultAttOnTxEnabled;
    bool   txInhibit_             = false;   // #94 External TX Inhibit (persisted)
    bool   convertLockout_        = false;   // #201 transient MP4-convert keying lockout
    int    attOnTxDb_              = kAttOnTxDb;   // 0..31; default 31
    // #169 — SWR-protection state.  Operator surface persisted under
    // tx/swr*; the advanced blank/dwell/floor knobs are loaded from
    // QSettings in the ctor (no UI — tunable without a rebuild).  All
    // touched on this QObject's thread: the Q_PROPERTY setters, the
    // swrEvalTimer_ tick, and the moxActiveChanged arm/disarm lambda all
    // run here (same affinity as the TX-safety timer + FSM).
    static constexpr bool kDefaultSwrProtectEnabled    = true;
    static constexpr bool kDefaultSwrProtectDuringTune = true;
    bool    swrProtectEnabled_    = kDefaultSwrProtectEnabled;
    double  swrProtectLimit_      = kSwrProtectDefaultLimit;
    bool    swrProtectDuringTune_ = kDefaultSwrProtectDuringTune;
    bool    swrProtectTripped_    = false;   // latched until next key-down
    QString swrProtectReason_;               // e.g. "SWR 7.2:1"
    int     swrBlankMs_   = kSwrBlankMsDefault;   // QSettings tx/swrBlankMs
    int     swrDwellMs_   = kSwrDwellMsDefault;   // QSettings tx/swrDwellMs
    double  swrFwdFloorW_ = kSwrFwdFloorWDefault; // QSettings tx/swrFwdFloorW
    double  swrRevFloorW_ = kSwrRevFloorWDefault; // QSettings tx/swrRevFloorW
    QTimer* swrEvalTimer_ = nullptr;   // 50 ms eval tick while keyed
    int     swrTicks_     = 0;         // eval ticks since key-down (blank)
    int     swrOverTicks_ = 0;         // consecutive over-limit ticks (dwell)
    int     wattsOverTicks_ = 0;       // Stage 3b-2 — over-watts-cap dwell
    // #169 Phase 1b — Fold action state.  swrProtectAction_ 0=Cut/1=Fold.
    // While folded, swrFoldPreDrive_ holds the operator's drive at the
    // first fold step so the next key-down (armSwrProtect) restores it.
    int     swrProtectAction_ = 0;          // 0 = Cut (default), 1 = Fold
    int     foldMinDrivePct_  = kFoldMinDrivePctDefault;
    bool    swrFolded_        = false;      // a fold step has been applied
    int     swrFoldPreDrive_  = 0;          // operator drive at fold start
    // #170a — Max TX drive cap (1..100 %; 100 = no cap).  Enforced in
    // setTxDriveLevel (every drive write funnels there) + the open
    // re-push + applyDriveLevelNoPersist via maxDriveRaw().
    int     maxDrivePct_      = kMaxDrivePctDefault;
    // #105 CW-1a — CW keyer config (operator surface; cw_tx_design.md §7
    // defaults).  Mirrored into prn->cw via applyCwConfigToPrn(); the
    // composer (cases 12/13/14) reads prn->cw.  INERT until cw_enable is
    // set by the keying commit (CW-2/CW-3).
    int    cwKeyerSpeedWpm_  = 25;     // prn->cw.keyer_speed (1..60 WPM)
    int    cwKeyerWeight_    = 50;     // prn->cw.keyer_weight (33..66)
    bool   cwIambic_         = true;   // prn->cw.iambic (false = straight key)
    bool   cwModeB_          = false;  // prn->cw.mode_b (false = iambic A)
    bool   cwRevPaddle_      = false;  // prn->cw.rev_paddle
    bool   cwStrictSpacing_  = false;  // prn->cw.strict_spacing
    // #105 CW break-in mode: 0=QSK (default), 1=Semi, 2=Manual.  Drives
    // prn->cw.break_in (QSK+Semi=1, Manual=0) AND the host-MOX gate in
    // onHwPttPoll (QSK = host stays RX, clean dits/dahs on the RX waterfall;
    // Semi/Manual = host MOXes off the key line, the reference QSKEnabled
    // distinction).  Default QSK: operator prefers it — the TX-analyzer view
    // Semi triggers settles a beat late + smears the keying into a blob.
    int    cwBreakInMode_    = 0;
    int    cwHangDelayMs_    = 300;    // prn->cw.hang_delay (0..1000 ms)
    bool   cwSidetoneOn_     = true;   // prn->cw.sidetone (FPGA HW sidetone)
    int    cwSidetoneLevel_  = 64;     // prn->cw.sidetone_level (0..127)
    // #105 CW-2 — firmware (gateware) CW keyer enable.  Mirrors Thetis
    // CWFWKeyer (console.cs, default true): when in CW it arms the HL2's
    // internal iambic keyer so a physical paddle/key in the radio jack
    // keys + shapes the carrier + runs the HW sidetone autonomously (the
    // gateware TX state machine engages on ext_keydown — NO host MOX).
    // Default true (firmware keyer is the only CW path until the software
    // keyer + a CWFWKeyer Settings toggle land in CW-3).
    bool   cwFwKeyer_        = true;
    // Push the whole CW block into prn->cw (no-op if prn not yet created).
    void   applyCwConfigToPrn();
    // #105 CW-2 — Thetis EnableCWKeyer analog: arm/disarm prn->cw.cw_enable
    // (the firmware-keyer + sidetone master).  Armed only in CW mode so a
    // paddle press outside CW can't key a CW carrier.  No-op if prn null.
    void   applyCwKeyerEnable();
    // #105 CW-3a — host CW keyer (CWX).  setCwxKey/setCwxPtt write the
    // wire CW bits (guard prn-null; single-int writes tolerate the EP2
    // writer's 1-frame read skew, benign for CW element timing).  The
    // keyer is lazily created and runs its element pump on a dedicated
    // thread, calling these via injected callbacks.  ensureCwKeyer()
    // builds it on first use; close() aborts it before prn teardown.
    void   setCwxKey(bool down);   // → tx[0].cwx     (per-element key)
    void   setCwxPtt(bool on);     // → tx[0].cwx_ptt (message-level hold)
    // #105 — set the UI-facing CW keyed state (cwKeyingActive_) + emit the
    // change.  Call on this QObject's thread only (the CwKeyer onState hook
    // marshals via QueuedConnection).  Dedups; does NOT touch wire MOX.
    void   setCwKeyingActive(bool on);
    // #105 — recompute txDisplayActive_ from moxActive_ || cwKeyingActive_
    // and emit txDisplayActiveChanged on change.  Wired to both
    // moxActiveChanged and cwKeyingActiveChanged in the ctor.
    void   updateTxDisplayActive();
    void   ensureCwKeyer();
    std::unique_ptr<lyra::tx::CwKeyer> cwKeyer_;
    // #171 — break-in hang for the serial straight-key path: on key-up we
    // drop tx[0].cwx immediately but hold cwx_ptt for cwHangDelayMs_ so
    // inter-element gaps don't drop TX (lazy-created, single-shot, main thread).
    QTimer *serialCwHangTimer_ = nullptr;
    // #93/#106 — AM/SAM carrier level, % of standard carrier POWER
    // (100 % = standard AM = WDSP c_level 0.5 = 25 % of PEP).  Maps via
    // c = sqrt(pct/100)*0.5 to match the reference's AM carrier control.
    static constexpr double kDefaultAmCarrierPct = 100.0;
    double amCarrierPct_           = kDefaultAmCarrierPct;

    // #107 — FM operator knobs.  Deviation default 5000 Hz = WDSP create-time
    // default (±5 kHz wide).  CTCSS default OFF / 100.0 Hz; run is mode-gated
    // (FM only) via applyCtcssRun().  Persisted under tx/fm*.
    static constexpr double kDefaultFmDeviationHz = 5000.0;
    static constexpr double kDefaultCtcssToneHz   = 100.0;
    double fmDeviationHz_          = kDefaultFmDeviationHz;
    bool   ctcssEnabled_          = false;
    double ctcssToneHz_           = kDefaultCtcssToneHz;
    int    fmEmphasisMode_        = 1;   // 0=Off, 1=Comm (voice default)

    // §3.9-5 revert (operator-rejected 2026-06-06): the Lyra-native
    // cos² SSB envelope shim (MoxEdgeFade) was deleted per Rule 1
    // (DO AS THE REFERENCE DOES) — reference does not envelope-shape
    // SSB at all (WDSP TXAUslewCheck at wdsp/TXA.c:819-824 returns 0
    // for SSB modes; uslew only arms for ammod/fmmod/gen0/gen1).
    // Hot-switch protection for external linear amps relies solely
    // on rfDelayMs_ (TR sequencing), matching the reference.
    // ATT-on-TX value (matches operator's working-station config = 31): forces
    // the AD9866 step-att to its 31-dB code on TX, which the encoder
    // turns into wire (31-31)&0x3F | 0x40 = 0x40 = min-LNA on the
    // FAST_LNA arbiter = max RX-ADC protection during TX coupling.
    static constexpr int     kAttOnTxDb       = 31;
    // TX safety timeout range — see public section above (canonical
    // bounds exposed for the Settings UI).

    // ===== Stage 2b2 — Ep6RecvThread member =====
    //
    // MUST be declared LAST in the private member block.  Destructors
    // run in reverse declaration order; declaring ep6Thread_ last
    // means its destructor (which joins the EP6 thread) runs FIRST
    // when HL2Stream goes away — guaranteeing the EP6 thread has
    // stopped touching any HL2Stream state BEFORE other members
    // dissolve.  Belt-and-suspenders: main.cpp's aboutToQuit handler
    // calls stream->close() explicitly BEFORE delete'ing HL2Stream
    // (matches reference's "explicit stop, never rely on implicit
    // teardown" posture at network.c:1434-1452 IOThreadStop).
    //
    // The Ep6RecvThread itself wires LIVE in open() (set_router +
    // set_hw_ptt_sink + start()), retires in close() (stop() joins
    // the thread bounded by the WSAWait timeout).  Replaces the
    // OLD rxWorker_ jthread + rxWorkerLoop body that Stage 2b2b
    // deletes from this file.

public:
    // Stage 2b2 — getter used by Hl2Ep6MicSource to wire its mic
    // consumer via ep6Thread_.set_mic_sink(...).  The MicSource
    // owns its consumer registration; HL2Stream just owns the
    // EP6 thread.
    lyra::wire::Ep6RecvThread& ep6Thread() noexcept { return ep6Thread_; }

private:
    // Stage 2b2-fix-v2 — Router lifecycle is now process-singleton
    // via the explicit `lyra::wire::create_router(0)` / `destroy_
    // router(0, 0)` calls in main.cpp, matching the reference's
    // `cmaster.c:316` `create_router(0)` / `destroy_cmaster()`
    // `destroy_router(0, 0)` posture.  The transient HL2Stream
    // value-member `router0_` (which existed in the first 2b2b-fix
    // pass) is RETIRED — RAII-on-HL2Stream was a Lyra-native idiom
    // translation, but operator-locked rule is "do as the reference
    // does, period."  The router is created BEFORE the HL2Stream
    // ctor runs (so router_instance(0) is non-null for any sink
    // registration site), and destroyed AFTER HL2Stream's dtor
    // runs (so ep6Thread_'s join completes before the router slot
    // is freed).
    lyra::wire::Ep6RecvThread ep6Thread_;
};

} // namespace lyra::ipc
