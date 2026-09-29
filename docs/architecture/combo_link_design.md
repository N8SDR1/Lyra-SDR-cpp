# Lyra ↔ SDRLogger+ "Combo" link — design

**Status:** design locked, not yet built. 2026-07-07.
**Canonical spec:** this file. SDRLogger+ keeps a pointer copy at
`SDRLoggerPlus-v2/docs/COMBO_LINK.md` — edit *this* file, mirror there.

## 1. Goal

Make Lyra (the SDR: CW decode + keying) and SDRLogger+ (the logger: QRZ /
HamQTH callbook + logging) knowingly cooperate so the operator's **CW Console
contact row** and the **SDRLogger+ log-entry form** behave as two views of one
live QSO. Neither app duplicates the other:

- Lyra decodes the call and keys the rig; it has **no callbook lookup**.
- SDRLogger+ has QRZ + HamQTH already wired (incl. compound-call handling); it
  does the lookup and owns the log.

So a grabbed CW call flows Lyra → SDRLogger+, gets looked up, and the operator's
**name / QTH come back** to fill Lyra's `{NAME}` / `{QTH}` macro tokens. A
`{LOG}` macro tag logs the QSO. That is the whole feature.

## 2. Architecture decision (locked)

- **No separate bridge application.** The link rides the **existing TCI
  WebSocket** that SDRLogger+ already opens to Lyra. Both ends are ours, so we
  own the protocol; a third process would only add launch/config/version/uptime
  failure modes for zero capability gain.
- **One master toggle, in Lyra:** *Settings → "SDRLogger+ Combo"* (default OFF).
  Lyra is where the operator physically works CW, and it is the shared radio both
  apps hang off. Lyra announces the state over the link.
- **SDRLogger+ gets NO toggle** — it shows a read-only **`Lyra Combo: Linked ●`**
  status indicator and simply obeys while linked. One switch, one place; no
  half-on "I turned it on but nothing happens" trap.
- **The `{LOG}` tag is self-authorizing.** Auto-logging is the one irreversible
  action; it only fires because the operator *deliberately* put `{LOG}` in a
  macro and sent it. So SL+ needs no separate "allow Lyra to log me" gate — the
  tag in the macro IS the per-action consent.
- Messages are **namespaced (`lyra_*`)** so other TCI clients (WSJT-X, N1MM)
  ignore unknown commands. Harmless noise to them; meaningful only to SL+.

## 3. Shared contact object

The unit of sync. Both apps hold a copy; operator edits propagate.

```
{ call, rstSent, rstRcvd, name, qth, grid, serial }
```

- `call`   — worked station callsign (Lyra `CwMacroModel::hisCall`, SL+ log call).
- `rstSent`/`rstRcvd` — operator-set signal reports (Lyra `{RST}`).
- `name`/`qth`/`grid` — from SDRLogger+'s callbook lookup (fills Lyra `{NAME}`,
  `{QTH}` tokens). Lyra never originates these — it has no lookup.
- `serial` — Lyra CW serial (`{#}`), contest exchange.

## 4. Messages (over the TCI socket, `command:args;` grammar)

Lyra is the TCI **server**; SDRLogger+ is the **client**. Both can send.

| Direction | Message | Meaning |
|---|---|---|
| Lyra → all | `lyra_combo:on;` / `lyra_combo:off;` | Combo master state (Lyra toggle). SL+ shows/hides the "Linked" indicator and starts/stops acting. |
| Lyra → SL+ | `lyra_contact:lyra,<call>,<rstSent>,<rstRcvd>,<name>,<qth>,<grid>,<serial>;` | Lyra's contact-row changed (operator grabbed a call / typed / bumped serial). `src=lyra`. |
| SL+ → Lyra | `lyra_contact:sdrlog,<call>,<rstSent>,<rstRcvd>,<name>,<qth>,<grid>,<serial>;` | SL+'s contact changed — crucially the **callbook lookup result** (name/qth/grid) after it resolves `call`, and reverse call-push (spot click). `src=sdrlog`. |
| Lyra → SL+ | `lyra_log:<call>,<rstSent>,<rstRcvd>,<mode>,<freqHz>;` | `{LOG}` tag fired — SL+ submits the QSO now. |

Empty fields are allowed (e.g. `lyra_contact:lyra,K2K,599,,,,,1;` before lookup).
The `src` first field is the **echo-guard tag** (see §6).

## 5. The `{LOG}` side-effect token

Unlike `{CALL}`/`{RST}` (text substitution), `{LOG}` is a **side-effect token**:

- On send, it is **stripped from the keyed Morse** (never goes out as dits) and
  instead fires a `logRequested` action after the macro starts keying.
- Example: F6 "TU 73" becomes **`TU 73 {LOG}`** → sends "TU 73" on the air AND
  logs the QSO in one keystroke.
- `CwMacroModel::expand()` removes `{LOG}` (case-insensitive) from the returned
  text and sets a flag; `startSending()` emits `logRequested(contact)`;
  `TciServer` broadcasts `lyra_log:…` (only while combo ON).

## 6. Echo / loop guard (must be designed in)

Two-way sync of `name` (SL+ → Lyra) + `call` (Lyra → SL+) can bounce forever.
Rule, both apps:

> **A field change that was applied FROM an inbound `lyra_contact` message must
> NOT trigger an outbound `lyra_contact`.**

Implementation: set an `applyingRemote_` flag while applying an inbound message;
the outbound broadcaster no-ops if that flag is set. Belt-and-suspenders: also
suppress a broadcast whose payload equals the last value received. The `src` tag
lets each side confirm provenance.

## 7. Build order

| Stage | What | Lyra (cpp) | SDRLogger+ |
|---|---|---|---|
| **A** | Call export Lyra→SL+ | Combo toggle + broadcast on `setHisCall` | **Reuse the existing `spot_activated` handler** (already populates the log entry) — zero SL+ code for the first proof |
| **A′** | Name/QTH lookup-back SL+→Lyra | Inbound `lyra_contact:sdrlog,…` → fill `{NAME}`/`{QTH}` tokens | On call resolve, forward the callbook result as `lyra_contact:sdrlog,…` |
| **B** | `{LOG}` tag → log QSO | Side-effect token + `logRequested` + `lyra_log:…` | Parse `lyra_log:…` → submit current log entry |
| **C** | Reverse call push | Inbound → `CwMacros.hisCall` | Send `lyra_contact:sdrlog,…` on spot click / manual select |

A is the near-free proof (reuses this session's `spot_activated` round-trip). A′
is the "feels like magic" one (Lyra borrows SL+'s callbook). B/C round it out.

Note: A ships on `spot_activated` for speed, but the *durable* path is the
namespaced `lyra_contact` object above — migrate A onto it when A′ lands so
call + name travel on one coherent channel.

## 8. Status / provenance

- Provenance rule: shipped code/comments in first-principles terms; the TCI spec
  reference is `D:\sdrprojects\TCI Protocol.pdf`. These `lyra_*` messages are a
  Lyra-owned extension, not part of the TCI spec.
- Meter-export note (2026-07-07, `SESSION_LOG.md`): the TCI S-meter fix that
  preceded this work is unrelated; the combo link does not touch it.

## 9. Implementation status — SHIPPED 2026-07-07 (operator-confirmed A/A′)

Actual symbols as built (a few names differ from the §7 sketch):

| Stage | Lyra (cpp) | Status |
|---|---|---|
| **A** | `TciServer::setComboEnabled` persists + broadcasts `lyra_combo:on/off`; `setCwMacros` connects `CwMacroModel::contactChanged`→`onCwContactChanged`, which broadcasts `lyra_contact:lyra,<call>,<rst>,,<name>,,,<serial>` (gated combo-on + running + non-empty call, suppressed by `comboApplyingRemote_`). Toggle on **Settings → Network → "TCI server"** group. | ✅ shipped, confirmed |
| **A′** | Inbound `LYRA_CONTACT` (src=`sdrlog`, under `comboApplyingRemote_` guard) → `CwMacroModel::setHisCall` + `setOpName` (→ `{NAME}`). NOTE: fills `{NAME}` only (not `{QTH}` — Lyra has no QTH field today). | ✅ shipped, confirmed |
| **B** | `{LOG}` is an ACTION token: `CwMacroModel::expand()` STRIPS it (never keyed as morse); `sendIndex()` detects it in the RAW macro text → emits `logQsoRequested()` (fires even for a pure-`{LOG}` log-only macro). `TciServer::onLogQsoRequested` (wired in `setCwMacros`, gated combo-on + running + call) broadcasts `lyra_log:<call>,<rstSent>,<rstRcvd>,<mode>,<freqHz>` — `cwMacros_->rst()` fills BOTH sent+rcvd (single Lyra RST), `prefs_->mode()`, carrier = `rx1FreqHz()+markerOffsetHz()`. | ✅ shipped (SL+ live; awaiting operator Lyra rebuild) |
| **C** | **NO Lyra change** — the inbound `LYRA_CONTACT` (src=sdrlog) handler already sets `hisCall` from a bare `lyra_contact:sdrlog,<call>,,,,,,`. SL+ now pushes the bare call on a deliberate select (spot/globe click; `LogHub.FocusCallsign` when `Source != "log-entry"`) even when the callbook resolved nothing, so His Call tracks SL+ for unlisted/DX calls. Manual typing still pushes only on callbook resolve (avoids per-keystroke His-Call spam). | ✅ shipped (SL+-only; awaiting quick bench) |

SL+ side (`SDRLoggerPlus-v2`): `TciRadioConnection` cases `lyra_combo` / `lyra_contact` / `lyra_log`; `Combo{Link,Log}RequestedEvent` contracts; `LogHub` broadcasters; `signalr.ts`; `LogEntryPlugin` (badge, populate, `{LOG}`→`submitQso`). Full detail in `SDRLoggerPlus-v2/docs/COMBO_LINK.md` + the cross-session memory.

## 10. Auto received-S (RST) from the signal meter — SHIPPED 2026-07-07

Combo differentiator: while linked, SDRLogger+ auto-suggests the **S** digit of
the received RST from the shared, calibrated S-meter (peak-held over the RX
window, SNR-gated). R stays 5, T stays 9 by convention — S is the only
measurable one. Works SSB / CW / digital; **not** SAT. The *computation* lives
in SDRLogger+ (it already converts dBm→S-units for its meter, and owns the
log-entry lifecycle for the AUTO/MANUAL latch + peak reset). Lyra's one job:
supply the **SNR gate**.

Lyra side (this repo):
- `MeterModel::rxSnrDb()` — numeric in-passband SNR (dB) = `dispDbm_ −
  noiseFloorDbm_` floored at 0, i.e. the SAME value the on-screen SNR readout
  shows. Calibration-independent (a difference of two same-domain dBm values,
  so the operator's `calDb` trim cancels).
- `TciServer::onSmeterTick` also broadcasts **`lyra_snr:<db>`** right after the
  `rx_channel_sensors` frame, **combo-scoped** (`if (comboEnabled_ && meter_)`)
  so it only flows when it's actually used; other TCI clients ignore it.

SL+ side: `lyra_snr` → `TciMeterAggregator.UpdateSnr` → `RxSnrDb` on
`TciMetersEvent` → the Log Entry panel peak-holds `rxSignalDbm` while
`!isTransmitting && rxSnrDb ≥ 3 dB`, maps to an S digit (HF S9 = −73 dBm,
6 dB/unit, clamp 1..9), and fills RST-Rcvd while that field is AUTO. Typing
latches MANUAL (by code path, not value-compare); a new call re-arms AUTO +
resets the peak-hold. Inline "S-auto" toggle + auto/manual badge by the field.

Chosen over an in-app rolling-min noise-floor estimate because Lyra's SNR is a
real DSP measurement and matches what the operator sees on Lyra's meter. Ships
in the same Lyra rebuild as Stage B.

## 11. RTTY is an Apps companion (locked 2026-09-29)

Native in-radio RTTY was dropped. Operators run fldigi / MMTTY / etc. from
**Settings → Apps** (and TCI / VAC). Combo remains CW-only. DX-cluster
spot labels may still say RTTY; that is cluster text, not a decoder.
