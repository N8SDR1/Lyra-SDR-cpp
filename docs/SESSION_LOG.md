# lyra-cpp — Session Log

Running EOD log. Newest entry on top. Short rough-outline format.

---

## 2026-09-25 — v0.24.4 docks QML defer + VAC2 installer

- **Hang #16:** `QQuickWidget::setSource` after first `showEvent`;
  per-dock `[startup]` log; `QSG_RENDER_LOOP=basic` on software/`--safe`.
  Targeted test for Bob (NucBoxM3 / Intel UHD), not a guarantee.
- **VAC2** already on `lyra-p2` (`72fdd60` / `a8ba044`).
- Installer `dist/Lyra-Setup-0.24.4.exe`. Do not merge `main`.

---

## 2026-09-24 EOD — HOLD: Brick SUB/RX2 + SPLIT polish + TCI VFO B

Firmware revision on the Brick unblocked RX2 after **>2 months** of host-side
head-banging. Operator: hold here for the evening; pick up from this HEAD,
no redesign.

### Shipped on `lyra-p2` (pushed)
- **Brick SUB/RX2** live (v0.24.2 already had the RX2 bring-up; this session
  is the operator-confirmed polish on top).
- **`df0e9c2` SPLIT: pile-up polish** — SUB and SPLIT stay independent (not
  a tri-state). Gray TX pip assigns TX; right-click SPLIT per-mode shift
  ±1/5/10 kHz (last-used persisted `tx/splitShiftHz/<MODE>`); middle-click
  pan + TUNE A/B tooltip. SPLIT-only still allows focus 2 / TUNE B.
- **`473e722` TCI: Thetis-style VFO B vs RX2**
  - `vfo:0,1` = split VFO B (WSJT-X hardware split)
  - `vfo:1,0` / `dds:1` = RX2 (SUB)
  - `rx_enable:1` = SUB on/off; **do not auto-enable SUB** on RX2 freq set
  - Handshake seeds RX2 + `rx_enable:1`; live RX2 freq/mode/vol/mute/passband;
    `tx_frequency_thetis` carries real SUB + TX-on-B
  - No new Settings → Network “enable RX2” toggle — TCI already advertises
    `channels_count:2` when the server is running
- Operator bench: SPLIT polish good; TCI VFO B “think we are good.”

### Tree
- Branch **`lyra-p2`**, HEAD **`473e722`**, in sync with `origin/lyra-p2`.
- **`origin/main` still v0.24.2** (no version bump / installer / GitHub
  release this session).
- Scratch `scratch/brick_rx2_working.pcapng` + `.txt` stay **untracked**.

### Not in this push (next if wanted)
- TCI RX2 S-meter (`rx_channel_sensors` still RX1-only)
- Version bump / installer / GitHub release / wiki
- Docs/KNOWN_ISSUES still lag the SPLIT polish

### Next session
Resume at **`473e722`**. No firmware/host redesign unless the operator
opens a new item.

---

## 2026-07-07 — Combo link — Stage A′ + B + auto received-S

Continuation of the Combo link (Stage A below). A + A′ **operator-confirmed
working**; B + the auto-S are built (SL+ side live) and **NEED THE NEXT LYRA
REBUILD**. Canonical: `docs/architecture/combo_link_design.md` §9–§10.

### Stage A′ — name-back (SL+ → Lyra `{NAME}`) — CONFIRMED
- Lyra side was already in place (inbound `LYRA_CONTACT:sdrlog,…` → `setOpName`).
  SL+ now sends `lyra_contact:sdrlog,<call>,,,<name>,,<grid>,` after its callbook
  resolve; Lyra fills `{NAME}`. SL+ sends the **first name only** (QRZ `Name` is
  the surname) — no Lyra change needed.

### Stage B — `{LOG}` auto-log tag — NEEDS REBUILD
- `CwMacroModel`: `{LOG}` is an ACTION token — `expand()` STRIPS it (never keyed
  as morse); `sendIndex()` detects it in the raw macro → emits new
  `logQsoRequested()` (fires even for a pure-`{LOG}` log-only macro). Header doc
  + signal added.
- `TciServer`: `setCwMacros` also connects `logQsoRequested`→`onLogQsoRequested`,
  which broadcasts `lyra_log:<call>,<rstSent>,<rstRcvd>,<mode>,<freqHz>` (gated
  combo-on + running + non-empty call; `rst()` fills both sent+rcvd; `mode` =
  `prefs_->mode()`; carrier = `rx1FreqHz()+markerOffsetHz()`).
- SL+ (live): `case lyra_log`→`ComboLogRequestedEvent`→`LogEntryPlugin` submits
  the current log-entry form (payload RST/mode/freq stamp it).
- Files: `CwMacroModel.h/.cpp`, `tci_server.h/.cpp`.
- Operator usage: put `{LOG}` in a CW macro (e.g. `TU 73 {MYCALL} ee {LOG}`) →
  sends the signoff AND logs the QSO in SDRLogger+.

### Auto received-S (RST) from the signal meter — NEEDS REBUILD
- Combo standout: SL+ auto-suggests the **S** digit of RST-Rcvd from the shared
  calibrated S-meter (peak-hold, SNR-gated). Compute lives in SL+; Lyra supplies
  the SNR gate. R=5/T=9 by convention. Works SSB/CW/digital, not SAT.
- Lyra: `MeterModel::rxSnrDb()` = `dispDbm_ − noiseFloorDbm_` floored at 0 (the
  on-screen SNR value; cal-independent). `TciServer::onSmeterTick` also
  broadcasts **`lyra_snr:<db>`** right after `rx_channel_sensors`, **combo-scoped**
  (`comboEnabled_ && meter_`).
- SL+ (live): `lyra_snr`→`TciMeterAggregator.UpdateSnr`→`RxSnrDb` on the meter
  event → LogEntry peak-holds `rxSignalDbm` while `!TX && SNR≥3 dB` → S digit
  (HF S9=−73, 6 dB/unit) → fills RST-Rcvd while AUTO; typing latches MANUAL; new
  call re-arms. Inline "S-auto" toggle + auto/manual badge by the field.
- Files: `metermodel.h`, `tci_server.cpp`.

### Verify after the (single, bundled) Lyra rebuild
1. Grab a call → SL+ populates + name-back to `{NAME}` (A/A′).
2. Enable "S-auto" by RST-Rcvd → the S tracks their signal; type to override
   (badge → manual); new call re-arms (badge → auto).
3. Send a `{LOG}` macro → SL+ logs the QSO with that RST.

---

## 2026-07-07 — Feature: Lyra ↔ SDRLogger+ "Combo" link — Stage A (call export)

Design: `docs/architecture/combo_link_design.md` (canonical). Building the
Lyra↔SDRLogger+ collaboration layer over the **existing TCI socket** — no
separate bridge app, one master toggle in Lyra, SDRLogger+ shows a read-only
"Lyra Combo: Linked" indicator. Stage A = callsign export Lyra → SDRLogger+.

### Lyra side (this session — NEEDS REBUILD)
- `TciServer`: new master toggle `comboEnabled_` (persisted `tci/combo_
  sdrloggerplus`, default OFF) + `setComboEnabled` (announces `lyra_combo:on/off`
  + pushes current contact on enable). `setCwMacros(CwMacroModel*)` wires the CW
  Console contact row; `onCwContactChanged` broadcasts
  `lyra_contact:lyra,<call>,<rstSent>,,<name>,,,<serial>` on any contact change
  (skipped while `comboApplyingRemote_` — the echo guard). Inbound
  `LYRA_CONTACT:sdrlog,…` applies call/name back into `CwMacros` under the guard
  (the A′ name-back plumbing, dormant until SL+ sends it). `sendInit` announces
  `lyra_combo:on` to freshly-connected clients.
- Wiring: `MainWindow` calls `tci_->setCwMacros(cwMacros_)` after `tci_` ctor.
- UI: Settings → Network → **"SDRLogger+ Combo (share CW Console contact)"**
  checkbox (tooltip explains call-populate + `{NAME}` back + `{LOG}` tag).
- Files: `tci_server.h/.cpp`, `mainwindow.cpp`, `settingsdialog.cpp`.

### SDRLogger+ side (built + compiles clean this session)
- `TciRadioService` parses `lyra_combo` (→ `ComboLinkChangedEvent` for the
  indicator) + `lyra_contact:lyra,…` (→ reuse `SpotSelectedEvent` so the log
  entry populates + QRZ/HamQTH fires; dedup on call). New contract event +
  hub + signalr + store + a **`● Lyra Combo`** badge in the Log Entry header.

### Verify after Lyra rebuild
Enable the Combo checkbox in Lyra + connect SDRLogger+ over TCI → SL+ shows the
"Lyra Combo" badge. Grab a call in Lyra's CW Decoder (`→ His Call`) → the call
should populate SDRLogger+'s log entry + fire its callbook lookup.

### Next: Stage A′ (name-back) — the "magic" half
SL+ sends `lyra_contact:sdrlog,<call>,,,<name>,,<grid>,` after its lookup; Lyra
already applies it into `{NAME}`. Needs an outbound TCI-send path on SL+'s
`TciRadioService` + the `LogHub.FocusCallsign` hook. Bundle into the same Lyra
rebuild as this + the meter fix if built before you compile.

---

## 2026-07-07 — Fix: TCI S-meter export sent post-AGC dBFS, not a calibrated dBm (SDRLogger+ meter "wildly off")

Operator (running SDRLogger+ against Lyra over TCI) reported the SDRLogger+
signal meter was "so far off it's ridiculous." Traced to Lyra's **TCI export**,
NOT SDRLogger+'s math (which is standard S9=−73 dBm) and NOT Lyra's own
on-screen meter (which is correct).

### Root cause
`TciServer::onSmeterTick()` broadcast `rx_channel_sensors:0,0,<dbm>` where
`dbm = WdspEngine::audioDbFs() − 30`. `audioDbFs()` is the **post-AGC audio
level** (dBFS); the `−30` was an admitted "coarse stopgap until per-band cal
lands." That's not an S-meter: AGC compresses it toward full scale so it barely
tracks signal strength, and the offset is arbitrary → clients see a squished /
pegged reading.

Meanwhile the **on-screen meter** (`MeterModel::computeSMeter`) already builds a
proper calibrated dBm from a *different* engine method — `WdspEngine::sMeterDbm()`
(WDSP RXA_S_PK, **in-passband**, pre-AGC) `+ operator calDb trim − LNA gain`,
with the HF/VHF S9 split. The TCI path was just reaching for the wrong engine
value next door.

### Fix (RX-only, display/telemetry path — no wire/protocol/DSP change)
Route the TCI export through the SAME single calibration the face meter uses:
- `MeterModel`: factored the calibration into `calibratedSMeterDbm(raw)` (one
  source of truth) and added `double rxSMeterDbm() const` — the instantaneous
  calibrated RX S-meter dBm, source-independent (always the S-meter, whatever
  the face is currently showing PWR/SWR/etc.) and sentinel-safe (S0 floor when
  the stream isn't running). `computeSMeter()` now calls the shared helper.
- `TciServer`: new `setMeterModel(MeterModel*)` + `meter_` member; `onSmeterTick`
  broadcasts `meter_->rxSMeterDbm()` (falls back to raw `engine_->sMeterDbm()`
  only if unwired). Forward-decl + `#include "metermodel.h"`.
- `MainWindow`: `tci_->setMeterModel(meter_);` right after `tci_` construction
  (meter_ created earlier; both parented to the window → lifetime-safe).

Files: `metermodel.h`, `metermodel.cpp`, `tci_server.h`, `tci_server.cpp`,
`mainwindow.cpp`.

### Net
Every TCI client (SDRLogger+, WSJT-X, N1MM, …) now gets the exact calibrated dBm
the Lyra front panel shows. On the **SDRLogger+ side**, its meter calibration
offset should go back to **0** once this Lyra build ships (the arriving value is
now real dBm, not something needing a −15 dB fudge).

⚠ NEEDS REBUILD + PUSH: this is a source change; rebuild Lyra and push so
connected loggers receive the corrected feed. Regenerate SESSION_LOG.docx/.pdf
via `tools/sync_execution_plan.py` if keeping the siblings in sync.

---

## 2026-07-04 — Research: HardRock-50 / Icom AH-4 amp+ATU band-follow = a GATEWARE feature (no Lyra port needed)

Investigation session (no code). Operator asked why the HardRock-50 band-follows
under Thetis but not Zeus/Lyra with an HL2+ + N2ADR filter board. Ran it to
ground against **hard sources** (not marketing): the AK4951 V4.1 companion-board
KiCad schematic (drive PDF "AK4951 V4.1.pdf") + the HL2+ ak4951v4 gateware RTL
(`Y:\Claude local\_hl2src\control.v` / `hermeslite_core.v` / the variant).

### The finding (definitive)
- **The AK4951 "Plus" companion board has NO microcontroller** — only U1=TPS730
  regulator + U2=AK4951 codec + a few MOSFET level-shifters. It is a **passive
  breakout/level-shifter**. J4 "HR-50/AH4 ATU" carries `ExtAMPTxD`, `EXTTR`,
  `ATU_START`, `ATU_STATUS`, `PTT` — all produced by the HL2, not the board.
- **The HardRock CAT stream is generated on-chip by the HL2 gateware** —
  `control.v` `extamp` module: `extamp(.freq(tx_freq), .ptt(int_tx_on),
  .uart_txd)` → 9600-baud KX3/FT-817-emulation CAT out the DB9. `tx_freq` is
  latched from **`cmd_addr 0x01` (wire `C0=0x02`) = the TX frequency register**.
  The AH-4 is the gateware `exttuner` module (START/KEY/`atu_txinhibit`).
- Both are compile-time gateware params `UART`/`ATU` (0=none, 1=JI1UDD HR50/ATU);
  the `hl2b5up_ak4951v4` variant sets **UART=1, ATU=1** → live.
- ⇒ Band-follow + PTT is **100% automatic in the gateware. NO host enable
  register, NO I2C, NO CAT generation on the host, NO special mode.** The host's
  ONLY job = write `C0=0x02` (TX freq) and keep it current with the VFO.

### Root cause of "follows on Thetis, not Zeus/Lyra"
The amp follows the **TX-freq register**. Thetis rewrites the TX DDS on every
dial change during RX (independent of MOX) → `C0=0x02` always current → amp
band-follows as you tune. Lyra/Zeus historically only set the TX freq **around
keying** (the documented "stale TX NCO 0 Hz, `0x02/0x08/0x0a` lazily registered
at the MOX edge") → stale/0 during RX → amp doesn't follow until keydown.

### What it means for Lyra (corrects earlier wrong scoping)
- This is **NOT a feature to build** — no serial code, no I2C, no OC/BCD table.
  My earlier "editable OC-pin table" scoping was the wrong subsystem entirely.
- It's a **one-behavior requirement**: keep the TX-freq register (`C0=0x02`)
  tracking the operating VFO **continuously incl. during RX**, like Thetis.
  Recent TX-freq registration work (`start()` re-push + eager-register
  `0x02/0x08/0x0a`) may already do it or be one small change away.
- **Checkable in Lyra ALONE, no amp needed** — does Lyra emit `C0=0x02 =
  current VFO` while tuning during RX? Verify in the wire path. The amp is only
  needed to confirm final on-air behavior. Operator no longer has a HardRock, so
  on-air verification waits on a tester with HL2+ Plus + HardRock (or re-acquire).

### Next session
Dig into Lyra's TX-freq behavior: does it keep `C0=0x02` current to the VFO
during RX, or only around MOX? (Safe desk read of the wire path.) Full detail in
`THETIS_DIRECT_PORT_PLAN.md` → "HARDROCK-50 / ICOM AH-4" section + memory
`project_lyra_cpp_hardrock_atu.md`.

---

## 2026-06-30 — RELEASED v0.8.0 (companion-app launch + CW external/serial keyer + the post-v0.7.0 batch)

Cut **v0.8.0** bundling everything on `main` since v0.7.0 + today's work.

### Today's work (this session)
- **#193 Companion-app launch on profile pick — `40afa46`, operator-confirmed
  ("bingo") with VarAC + MSHV.** New `ProfileManager::loadByUser()`/`userLoaded`
  distinguish an explicit pick from the silent auto-recall; new
  `src/profile/CompanionLauncher.{h,cpp}` (Enable / name / command+args /
  start-delay; `startDetached`, .bat via `cmd.exe /c`; already-running skip; no
  auto-kill). **Per-machine QSettings `profileLaunch/*`, filtered by
  `isMachineSpecificKey` → never in the portable profile/.lyra.** Fires on the
  front `ProfilePanel` combo (`loadByUser`) + the Settings Load button; the
  auto-recall combos still call `load()`. Profiles tab reworked (companion group
  + width-capped 2-col auto-recall grid).
- **#159 DSP Options — CLOSED-AS-SCOPED.** Filter-type-only tab is at parity for
  what modern-HW/Qt6 can trade; added a one-line note cross-referencing
  Audio→VAC as the one tunable latency. Rationale in memory.
- **#186 CW keyer settings — CLOSED-AS-AT-PARITY (no code).** Diffed the actual
  Thetis CW screen + the `_cw` wire struct: every gateware-honorable knob is
  already in Settings→CW; the two not-present items (Auto Mode Switch, Software
  sidetone) are host-only with no wire field → inert on HL2, deliberately not
  ported.
- **#171 External CW keyer / Winkeyer — BUILT (default-OFF, bench pending).**
  Gateware verify (HL2+ ak4951v4 `CW=2`) settled it: KEY-jack keys the carrier
  autonomously (= the shipped CW-2 path). **Piece A** = WKmini→HL2 jack, Iambic
  OFF (a Settings→CW note). **Piece B** = new `src/cat/SerialCwKey.{h,cpp}`
  (sibling of SerialPtt): polls a COM port CTS/DSR pin → `HL2Stream::
  requestCwKeyFromSerial()` → the #105 cwx/cwx_ptt path, holds cwx_ptt across
  the break-in hang; NO host iambic. Settings→CW "CW key over COM port" group,
  default-OFF, per-machine QSettings. Ported Lyra-native from Thetis
  `Console/cwkeyer.cs` (open attribution). Awaiting operator on-air bench.
- **#194 logged (come-back-to): CWX/console CW wrong above ~27-29 WPM.** Not a
  clamp — two compounding timing causes: `cwDitMs` integer truncation
  (`1200/wpm`) + the wire `cwx` bit only moving at the ~2.6 ms EP2-datagram
  cadence (inherent to host CWX; high-speed CW → paddle/keyer in the KEY jack).
  Easy fix = round the timing; wire floor is structural. See task #194.

### Release
- Version 0.7.0→0.8.0 (CMakeLists + lyra.iss), README features, USER_GUIDE
  (companion launch + CW external/serial keyer), `docs/releases/v0.8.0.md`.
- Bundle also carries the earlier unreleased `main` commits: #188 tuner-delete
  trashcan (`103c5fe`), docking-feel smoothing (`f31375a`), #191 CAT virtual-COM
  enumeration (`732b753`), #189 layout save/recall fidelity (`11dbed8`), #190
  USB-BCD cable lifecycle (`27b7287`), #158 VAC latency (`dc9c4c2`), docs
  (`e199993`).

## 2026-06-29 (EOD) — Post-v0.7.0: docs + VAC latency posture (VarAC turnaround)

All on `main`, unreleased (after v0.7.0). A doc pass + one operator feature.

### Docs (`e199993`)
- Recorded the v0.7.0 **FM transmit refinements** as a TX-relevant entry in
  `THETIS_DIRECT_PORT_PLAN.md`'s SHIPPED-WORK LOG (FM is a TX modulator
  concern): fix #2 AF brick-wall `b72bc50`, FM-aware bandwidth `4f32ca1`,
  pre-emphasis Off/Comm `a826c8c`, mic-rack auto-bypass `12c8991`, Tuning-panel
  pre-emph chip + Dev→RX-BW `67abf05`. RESUME POINTER + trailer refreshed to
  v0.7.0 (PureSignal + RX2/Split = remaining major TX-port arcs). Regen plan
  PDF/DOCX.
- New `docs/REMAINING_WORK.md` (+ PDF): standalone what's-left checklist — much
  smaller now (two big arcs left, RX2/Split + PureSignal, plus TX/CW
  follow-ons). Cleaned two stale tasks (CW macros #176, Spots #182 — shipped).

### VAC latency posture — `dc9c4c2` (#158 follow-up, operator-bench-confirmed
### "works better for VarAC")
- Operator wanted to squeeze VAC TX↔RX turnaround for VarAC/ARQ (runs 25 ms
  rings in Thetis). The IVAC engine already ported the full latency surface
  (`SetIVACInLatency`/`OutLatency`/`PAInLatency`/`PAOutLatency` + `vac_size`,
  driven by hardcoded `vac1LatencyMs_=120`/`vac1VacSize_=2048`); this exposed it.
- **Settings → Audio → VAC1** gains: **Buffer size** dropdown (128..8192, each
  ms-labelled at 48 kHz), **Latency** spinbox (5..500 ms — rmatchV ring depth;
  one knob feeds all four reference latency fields), + a live **Monitor** line
  (ring fill % + overflow/underflow per direction, from `getIVACdiags` on a
  500 ms QTimer — the reference VAC1 Monitor). Drop latency until counters
  twitch, back off.
- `setVac1LatencyMs`/`setVac1VacSize` persist (`vac1/latencyMs`, `vac1/vacSize`)
  + reopen VAC when live via the proven `rebuildVac1` path; `vac1Diags()` =
  guarded diag read.
- **Carried per-profile (Profile schema v4→v5, tolerant-load):** a tight VarAC
  profile keeps low latency while SSB stays at 2048/120 — flip profiles, flip
  the whole VAC posture. USER_GUIDE "Latency (for fast ARQ modes like VarAC)"
  note added.
- GOTCHA: reference `ivac.c:795` type comment is REVERSED vs its code; the
  TO/FROM VAC diag labels follow the code (`rmatchOUT`=TO type0 /
  `rmatchIN`=FROM type1, Ivac.cpp:797). Flag if they read backwards at bench.
- Operator decision: **ACCUMULATE** — stays on `main` unreleased; testers stay
  on v0.7.0 until the next release bundles it.

### NEXT SESSION — operator-flagged (2026-06-29 EOD)
1. **Tuner panel delete → trashcan icon.** Cosmetic: the per-row delete control
   on the Tuner panel (currently the `−` button, with the ✓/✗ confirm) should be
   a trashcan symbol. `src/qml/TunerPanel.qml`.
2. **BUG — layout save/recall re-adds the Solar panel.** Saving a named panel
   layout WITHOUT the Solar (HamQSL) panel open, then recalling it, ADDS the
   Solar panel even though it wasn't present at save time. **Only the Solar
   panel shows this so far** — other panels round-trip their hidden state fine.
   Smells like the Solar panel has a default-visible/auto-show that the
   layout-restore path doesn't suppress (cf. the Tuner base-dock "shows on
   summon" pattern from this session — a dock that buildDocks creates
   visible/auto and the saved hidden state doesn't override on restore).
   Investigate the named-layout save/restore (#184 / v0.4.8) dock-visibility
   serialization for the Solar/HamQSL panel specifically. See
   [[reference-hamqsl-solar]].
3. **⚠ SAFETY BUG (task #190) — USB-BCD adapter intermittently undetected +
   amp changed bands during RX.** FTDI C232HM (FT232H, COM1 in Windows) often
   not seen by Lyra's BCD dropdown; amp "beeped + changed bands in RX" (NOT
   RFI — heavily choked cable). Code-read root causes in `src/usb_bcd.cpp`:
   (a) enumeration is D2XX-only and the adapter is in VCP mode (owns the
   interface) → intermittent D2XX enumerate (the unplug/replug symptom);
   (b) `devices()` (~103-114) filters empty serials, but FTDI returns an empty
   serial for any OPEN device and Lyra's ctor opens+holds the device when BCD
   is enabled → its own dropdown shows "(none)" (matches the screenshot). Fix
   on the operator's bench (verify-don't-guess; spurious-BCD glitch is a
   separate lower-confidence item — get a repro + logging first). Interim
   safety: untick "Enable USB-BCD amp band output". Full detail in the memory
   note + task #190.

---

## 2026-06-29 — RELEASED v0.7.0 "Tuner memory" (Tuner panel + FM polish + tooltips toggle + header tidy)

Cut **v0.7.0** off `main` (bump `CMakeLists.txt` + `installer/lyra.iss`
0.6.0 → 0.7.0; README Features → v0.7.0 + Tuner bullet; release note
`docs/releases/v0.7.0.md`; USER_GUIDE "Tuner (manual ATU memory)" section +
header-chip + TOC; regen PDF/DOCX). Installer `dist/Lyra-Setup-0.7.0.exe`.

Headline: the new **Tuner** manual-ATU memory subsystem (operator-requested
2026-06-29, benched this session).
- `src/tunermemory.{h,cpp}` + `src/qml/TunerPanel.qml` + Settings → Tuner tab.
  3 antennas × freq-keyed Input/Output/Inductor, live colour-coded SWR,
  nearest-point **bracket** view (1 each side; exact + 1 each side when on a
  point — `windowEach=1`), Store→Save, single-row inline edit, ✓/✗ delete
  confirm. Match by carrier freq (USB/LSB-agnostic). Pure UI + QSettings,
  RF-safe.
- Tuner polish this session: SWR pill enlarged; flat (background-`Item{}`)
  header buttons; `matchDeltaHz` sign flipped to `point − dial` (so **+** =
  saved point above the dial — read the natural way).
- Header chip strip regrouped: **TX DSP** · **RX DSP** · **Options**
  (CTUN · Tuner · CW · CW Dec · WF-ID). CW/CW Dec/Tuner moved into Options;
  RX DSP slid left. `mainwindow.cpp buildToolbar`.
- Also folded into v0.7.0 (committed since v0.6.0): FM pre-emphasis Off/Comm
  selector + FM-aware bandwidth + Deviation→RX-BW auto-size + native mic rack
  auto-bypass on FM; global **Show tooltips** toggle (Settings → Visuals) +
  Profile-picker polish.
- The earlier "float like TX DSP" experiment for the Tuner dock was reverted
  (`11dd6d2`) after it caused intermittent launches; re-approached as just the
  `isChipSummonedPanel` predicate entry (`297a753`) which fixed the cyan
  dock-drop targets without the launch issue.

---

## 2026-06-29 — FM TX "beat Thetis" arc (pre-emphasis + clean chain + Tuning-panel UX)

All on `main`, unreleased (on top of v0.6.0). The FM transmit arc, brought
to a complete stopping point. WDSP study-first: read `fmmod.c` + `emph.c` in
full before touching shipped behaviour.

### FM Fix #2 — clean audio band-limit (`b72bc50`, earlier; logged for completeness)
- The FM primary bandpass `bp0` runs on the AUDIO before `xfmmod`, so it's
  now a symmetric **±3 kHz brick-wall AF low-pass** (was inheriting ±(dev+3k)
  ≈ ±8 k, over-feeding the modulator). The occupied-RF (Carson) clamp stays
  owned by the modulator's own internal bandpass, pinned via new
  `SetTXAFMAFFreqs(txch, 300, 3000)` on FM entry. Supersedes Fix #1's bp0
  value (v0.6.0).

### Mode-aware FM bandwidth (`4f32ca1`, earlier) — `ModeFilterPanel.qml`
- FM TX-BW selector + RX↔TX 🔗 lock disabled in FM (FM width is derived from
  deviation). TX-BW box is a read-only **"<N> k (auto)"** Carson readout
  `2·(dev+3k)`, live-tracking Dev. FM RX presets = FM channel widths
  **8/10/12/16 k** (was the SSB list).

### #3a — pre-emphasis selector Off / Comm (`a826c8c`)
- WDSP-native via `SetTXAFMEmphPosition` (bound in wdspcalls X-macro):
  **Comm** = position 1 = the native 6 dB/oct (300–3000) comms curve runs;
  **Off** = position 2 = both call sites pass through = a TRUE bypass (FM
  force-runs the emph block, so the chain position is the on/off — a real
  Off, which rigs/Thetis don't give you).
- `HL2Stream` `fmEmphasisMode` Q_PROPERTY (0=Off, 1=Comm, default Comm) +
  QSettings `tx/fmEmphasisMode` + `TxControl::setFmEmphasis` callback;
  applied on FM entry in `pushTxFilter` + live on operator change.
- UI: **Settings → TX → FM** combo (beside Deviation / CTCSS).

### #3b-3 — FM auto-bypasses the native mic rack (`12c8991`)
- In FM the whole TX rack (EQ + Combinator + Plate + speech enhancements)
  auto-bypasses, same forced-auto as the digital modes — a multiband
  compressor/reverb into FM's pre-emphasis = mush. One-line predicate
  change in `main.cpp` (`txModeBypassesRack` now fires for "FM" alongside
  DIGU/DIGL → `SetTxRackBypass`).

### Tuning-panel UX (`67abf05`) — `TuningPanel.qml`
- **Emph** quick chip (Comm / Off) added to the FM front row beside Dev/RPT,
  two-way-synced with Settings via the shared `Stream.fmEmphasisMode` prop.
- **Deviation now auto-sizes RX bandwidth** to the smallest FM channel
  preset ≥ Carson `2·(dev+3k)` — ±2.5 k → 12 k, ±5 k → 16 k. TX BW was
  already deviation-derived; RX now follows (Thetis-style). Still
  overridable in the Filters panel.

### #3b 50/75 µs + 300 Hz HPF — DROPPED / DEFERRED (operator call)
- Read `emph.c`: WDSP FM pre-emph is a 6 dB/oct ramp between f_low/f_high.
  **50/75 µs are broadcast-FM constants** (15 kHz audio, ±75 kHz dev),
  meaningless over our 3 kHz comms band (75 µs corner ≈2122 Hz = a weak
  top-end lift; 50 µs corner ≈3183 Hz is *above* the 3 kHz cutoff → no
  pre-emph at all). "Comm" already IS the correct amateur curve → 50/75 µs
  **dropped** (dead UI options). **300 Hz low-cut deferred** — needs a new
  native HPF stage (can't ride bp0 = Hilbert trap; rack is bypassed in FM),
  mostly redundant with Comm's bass rolloff; build only if FM-data-flat
  bass-cut is later wanted.

### Docs
- USER_GUIDE: FM section rewritten (deviation / pre-emphasis / CTCSS +
  clean-chain note + Tuning-panel mirror + Dev→RX-BW); TX-BW + Lock bullets
  note the FM auto-width behaviour; Tuning-panel FM front-row bullets updated.

### UI cosmetics — global tooltips toggle + Profile picker (`ff0cdf9`)
- **Global "Show tooltips" toggle** (Settings → Visuals, default on,
  persisted `ui/tooltips_enabled`). New `Prefs.tooltipsEnabled`; all 69 QML
  `ToolTip.visible` bindings across 9 panels AND it in (swept), and an
  app-wide `QEvent::ToolTip` event filter (`TooltipGate` in mainwindow.cpp)
  swallows QWidget (Settings dialog / menu) tooltips too → one genuinely
  global switch. USER_GUIDE Visuals → Tooltips section added.
- **Profile picker (ProfilePanel):** the recall tooltip no longer pops over
  the open dropdown (gated `!popup.visible`); combo width 300 → 170.

---

## 2026-06-28 — v0.6.0 RELEASED: per-band TX power model + auto-calibrated watts cap + FM fix #1

Released `v0.6.0` (`5b41fc3`; installer `dist/Lyra-Setup-0.6.0.exe`, 68.2 MB,
GitHub release published; main FF'd). Bundled the whole TX power-model arc +
the first FM fix that had been pending on main.

### Per-band TX power model (Settings → **PA Gain** tab)
- ChannelMaster `txgain.c` ported → `wire/TxGain.*`; operator drive% drives a
  digital fixed gain (Thetis RadioVolume) on top of the AD9866 byte, so **1 %
  drive is a true sub-watt** (was pinned at the ~0.78 W nibble-0 dead zone).
- Per-band **PA Gain table** (`RadioVolume = min(drive·gbb[band]/100/93.75,1)`)
  so the dial can be tuned to read true watts per band.
- **Watts Max cap** (amp protection) + **auto-calibrate**: key TUN per band
  into a dummy and Lyra walks power up to the cap and locks that band exactly
  (✓), SS-amp-safe (approach-from-below). Coloured Cap-tuned marks (green ✓
  tuned / red — not tuned). The old "Max TX drive %" (#170) control retired.

### FM fix #1 (`ae6eb19`) — FM Carson bandpass clamp (value later superseded by Fix #2)
- FM output bandpass sized from the FM signal's own occupied channel instead
  of inheriting the wide SSB/ESSB TX width → no FM splatter at wide TX BW.

### Docs
- USER_GUIDE "Settings → PA Gain" section; `docs/releases/v0.6.0.md`; README
  bumped to v0.6.0.

---

## 2026-06-27 — v0.5.0 RELEASED: DX-cluster spots subsystem (#182) + CW RX-decoder overhaul (#187) + #159 DSP filter type

Released `v0.5.0` (`63b0799`; CW `7ebc599`) — two commits on `main`,
installer `dist/Lyra-Setup-0.5.0.exe` (68.2 MB) published as a GitHub
release. (v0.4.11 — the Kenwood CAT-over-COM/TCP + serial-PTT arc —
shipped between v0.4.10 and this; it has its own release notes.)

### DX-cluster spots subsystem #182 — the headline (`spotstore.*`, `spothole_feeder.*`, `dxcluster_feeder.*`, `dxcc.*`, `PanadapterPanel.qml`, `settingsdialog.*`)
- Source-agnostic **spot bus** (`SpotStore`): call-keyed dedup, age-out,
  own-call highlight + toast, `source`/`continent`/`firstSeen` fields.
- **3 feeders**, each its own on/off (Settings → Network → DX-cluster spots):
  - **TCI** inbound (logger / SDRLogger+) — `addSpot(...,source="tci")`.
  - **SpotHole REST** (`SpotHoleFeeder`) — `spothole.app/api/v1/spots`,
    standalone (internet only), default-OFF poll. Parse is **snake_case**
    (`dx_call` / `freq` in **Hz** / `de_call` / `dx_continent` / skip `qrt`)
    — first cut used hamlog's processed field names and got zero spots;
    fixed by probing the raw API.
  - **DX-cluster telnet** (`DxClusterFeeder`) — host/port/login-call
    (defaults MYCALL, overridable) + login-commands, auto-login +
    auto-reconnect 20 s. NOTE: VE7CC-type nodes default to **RBN CW
    skimmer only**; `set/ft8;set/ft4` login cmds open up FT8/FT4 (confirmed
    by reading the node banner — not a bug).
- **Filters** (display-only; the bus keeps everything): `modeShown`
  (family-aware CW/SSB/DIGI/AM/FM), `regionShown` + `DxccLookup::continentOf`
  ISO-2→continent table (continent codes NA/EU/AS/SA/AF/OC always mean
  continents, else ISO-2 country; "US,EU" = US + all Europe).
- **Clutter caps**: per-freq bucket cap (`freqHz/bucketHz` bins, keep N
  newest, "+K more" badge — tames FT8 piles) + global display cap.
- **Colour modes**: source / single / by-mode / by-region / by-country +
  optional legend (`legendEntries()` Q_INVOKABLE). **New-spot colour**:
  a fresh spot wears a solid colour for N s (driven by `firstSeen`, not
  `added`, so a re-poll doesn't reset it) then settles. Two bugs fixed
  here: spots flipping green↔gold every poll (separated `firstSeen` from
  `added`), and the original blink flashing EVERYTHING on startup
  (reverted blink → solid new-spot colour; renamed all "Flash" UI +
  docs → "new spot colour" so folks don't expect a blink).
- **Console import**: click a spot → tune to it + `CwMacros.hisCall` grabs
  the call into the CW-console contact row (#176/#181).
- Band display filter DEFERRED (redundant on the panadapter; lands with a
  future spot-LIST view).

### CW RX-decoder overhaul #187 (`CwDecoderPanel.qml`)
- Decode-window **scroll fix**: ScrollView + `onContentHeightChanged →
  Qt.callLater(scrollToBottom)` so rows stop clipping at the 3rd line
  before a resize. (Took two passes — the first "fixed" it only until the
  text reflowed.)
- Narrow detect-filter BW chips tuned to `[80,100,120,150]`; Auto level
  with live readout; Advanced (Seek / DSP filter / AFC range). Operator
  copy-confirmed on real Field Day CW (S4–S9, 10–25 wpm).

### Also shipped
- **CW console scroll** (`CwConsolePanel.qml`): wrapped `body` in a
  ScrollView so the macro tags + bottom option rows stay reachable when
  the console is sized small.
- **#159 DSP filter type** (Linear Phase / Low Latency per mode family,
  opt-in) — was the prior pending item; rode out with v0.5.0.

### Docs / release
- USER_GUIDE: new "DX-cluster spots" section + reworked "Reading CW — the
  RX decoder" section; #159 Linear Phase / Low Latency already present.
- `docs/releases/v0.5.0.md`; version single-sourced via CMake
  `project(VERSION 0.5.0)` → installer `.iss` + README.
- Installer `Excludes` now drops `test_*.exe` (release hygiene).

---

## 2026-06-25 — v0.4.10 RELEASED: CTUN drag-the-marker (4th look, FIXED) + CW decoder A-series + CW-decoder docs

Working on `main`. Two releases since v0.4.8: **v0.4.9** (fast patch —
floating tool panels are float-only, Lock no longer disables the CW
console/decoder; `162906f`) and today's **v0.4.10** (`ebb3c67`).

### CTUN #174 — 4th look, the pass that actually fixed it (`PanadapterPanel.qml`)
- Operator (Thetis Help open as ground truth) said CTUN still wasn't Thetis
  after the v0.4.4/v0.4.5 "parity" rebuilds: you should drag the FREQ MARKER
  onto signals with the display LOCKED; a click INSIDE the filter must NOT
  jump the 0-beat.
- Read the real Thetis mouse handlers FIRST (console.cs 50230-50433 press +
  51887-51960 drag). The DSP core (locked-LO + WDSP NCO shift) was already
  right; the whole gap was the MOUSE + MARKER layer.
- ROOT CAUSE the 3 prior passes all missed: Lyra's carrier marker is
  hard-pinned to the locked display centre (`markerOffsetHz` = CW pitch
  only), so under CTUN there was NO marker tracking the dial — nothing to
  drag. First bench report: "marker does not move."
- Fix (CTUN-gated, non-CTUN byte-identical): new `ctunDialOffHz` slides the
  carrier marker + filter passband to (rx1FreqHz − centre); body-drag =
  relative 1:1 marker-grab (`setRx1FreqHz`); top-15px strip = LO pan
  (`setCtuneCenterHz`); inside-passband click suppresses the retune; the
  edge-drag + inPassband math subtract the dial offset.
- Operator-bench-CONFIRMED "Do as Thetis does state." #174 DONE.

### CW decoder — rest of the safe Tier-A A-series (A4/A6/A7/A8) (`CwDecoder.cpp/.h`)
- A-slice A1/A2/A3/A5 was committed last session (`eb4c89d`). v0.4.10 adds:
  - A4 sub-bin AFC: 3-pt parabolic interp on the winning Goertzel bin
    (refines bestHz only).
  - A6 hysteretic SNR gate: open at squelch×floor, hold to 0.8× that
    (≈1.2× floor); inert on clean signal.
  - A7 unify the two word-gap emitters: guard the processEdge Word emit with
    `lastEmitted_ != ' '` so a word gap can't double-emit.
  - A8 add `_` / `$` punctuation; prosigns deliberately omitted (alias +/( ).
- All additive — correct decodes unchanged, ~35 tuned constants untouched.
  Unit test ALL PASS (VVV TEST CQ / CQ PARIS / CQ TEST; AFC lock 749).

### CW decoder docs (`USER_GUIDE.md`)
- Operator couldn't recall what the decoder NB / DSP-filter did → realised
  none of the decoder was documented (+ a stale "decoder coming in a later
  release" line). Added "Reading CW — the RX decoder" section + TOC entry:
  panel, confidence dimming, decoded-call/name grab → His Call / Name (feeds
  {CALL}/{NAME}), Match-TX-to-RX-WPM, and every detector knob. Console
  His-call cross-linked to the decoder grab.

### B1 anchor (NEXT CW work — NOT started)
- Benched a live **W1AW** run; it announces its own speed ("END OF 30WPM
  TEXT", "NOW 25 WPM") = labeled truth. Sent 30→25 wpm, decoder read **35**
  → over-reads ~15-20%, dit estimate runs short. Copy still readable;
  residual errors (WILL→WIRL, ONCE→JNCE) are valid-letter swaps A1 can't
  touch by design. W1AW = the labeled bench to tune B1 (independent
  dah-length + warm-up/Gaussian boundary fix).

### Release mechanics + standing
- 0.4.9 → 0.4.10 (CMakeLists / lyra.iss / README); notes
  `docs/releases/v0.4.10.md`. Commit `ebb3c67`, tag `v0.4.10`, pushed main +
  tag, GitHub release + `dist/Lyra-Setup-0.4.10.exe` published.
- Daily backup task ("Lyra-cpp Daily Backup", 22:30 → `_backups/` + mirror
  `Z:\LyraBU\`) confirmed ACTIVE + healthy (last run 6/24 OK, 0x0).
- Open: CW B1 (with the W1AW bench). CTUN done. Bigger arcs still open —
  RX2 #96-101, CW macro chips #176, grab-spot-as-call #182, PureSignal.

---

## 2026-06-24 — v0.4.8 RELEASED: panel docking overhaul + CW transmit metering (+ wiki, CW-decoder review)

Working on `main` (last tag `v0.4.7` = `ff6c9c9`). Tester feedback (Randy
W7CPA / Jim / a 3rd) said the main panels "all float, nothing docks,
drop-against-an-edge does nothing," and resizing was a fight. Big UI arc +
a CW metering arc, shipped as **v0.4.8**.

### Panel docking overhaul (#184) — `dockdragcontroller.{h,cpp}` (NEW) + `mainwindow.{h,cpp}`
- Root cause: the custom dock title bar (`makeDockTitleBar`) replaces
  QDockWidget's built-in drag, killing Qt's drag->drop-indicator->snap-to-edge.
- New `DockDragController` (public QMainWindow API only): press->threshold->
  drag->drop FSM on each title bar, translucent **cyan drop-zone overlay** —
  snap-to-edge (`addDockWidget`), neighbor split L/R/T/B thirds
  (`splitDockWidget`, "before"=double-split), center tabify
  (`tabifyDockWidget`), off-region float (`setFloating`). Save-on-commit.
- Resize: dock separators widened 6px + cyan-on-hover; tab bar styled.
  Compact custom move cursor (arrow while locked). One-time status-bar hint.
- **Lock = move + resize**: disables move/float/close via features AND blocks
  the dock-separator resize press in `event()` (split-cursor detect). The
  earlier `setFixedSize`-on-lock froze panels and collapsed the layout on a
  locked restart — REMOVED; event-interception locks resize cleanly and
  always restores. (Two intermediate restart-collapse symptoms chased + fixed.)
- View -> Layouts: **4 named slots + Lyra default** (recall no longer
  force-shows the chip-summoned TX/RX/CW rack panels).

### Factory default fixed — `default_layout.h` + `applyDefaultLayout`
- Prior embedded default was base64 of the raw registry REG_BINARY -> carried
  the QSettings `@ByteArray(...)` UTF-16 wrapper -> `restoreState()` rejected
  it (the "screwed up default"). Re-embedded from operator's **N8SDR** slot,
  de-wrapped to the real saveState bytes, + `defaultPanadapterSplit()`.

### CW transmit metering (#105) — `hl2_stream.{h,cpp}`, `metermodel.{h,cpp}`, `main.cpp`, QML
- Bug: QSK CW keys the PA at the gateware and deliberately leaves wire MOX
  low, so the meter (gated on `moxActive`) never flipped to TX during CW
  (external watt-meter showed power). Other modes flip because they assert MOX.
- `cwKeyingActive` (message-level, from the CwKeyer `onState` hook, marshalled
  to the stream thread) drives the meter RX<->TX swap + the Ladder source set.
  Reads `fwdPowerW()` (already valid during gateware CW). Does NOT assert wire
  MOX — QSK keying unchanged.
- `txDisplayActive` (= `moxActive || cwKeyingActive`) drives the **red-on-air**
  QML (VFO border + RX/TX tag in TuningPanel, split/XIT TX markers in
  PanadapterPanel).
- **Paddle/key** detection: `onHwPttPoll` (~50 ms) latches `cwKeyingActive`
  from **forward power** (>=0.5 W, ~500 ms hang), gated to CW mode, skipped
  while CWX drives it — this HL2+ reads non-zero `ptt_in` at rest (§10 Q#1),
  so forward power is the clean signal.
- The panadapter spectrum **stays RX** during CW (gateware CW never feeds the
  WDSP TX analyzer -> swapping shows an empty pan; RX shows the keyed
  carrier). Operator-confirmed regression + fix.

### Release / docs / wiki
- Version 0.4.7->0.4.8 (CMake `project(VERSION)` + `installer/lyra.iss`).
  `docs/releases/v0.4.8.md`; USER_GUIDE "Getting around the window" rewritten
  + Meter CW note; README Features->v0.4.8. ISCC -> `dist/Lyra-Setup-0.4.8.exe`
  (69 MB). Tag `v0.4.8`, pushed main+tag, GitHub Release published w/ installer.
- **GitHub Wiki initialized + built**: Home, User Guide (mirror of
  USER_GUIDE.md), Installing-and-first-connection, FAQ-and-Troubleshooting,
  _Sidebar. Sources version-controlled in `docs/wiki/` (User Guide generated
  from USER_GUIDE.md at publish). NB: the first wiki page must be created once
  in the web UI (no API, no push-init) — operator did it; rest pushed via
  `…wiki.git`.

### CW decoder accuracy review (analysis only — NOTHING changed)
- Recovered the 4-reviewer + code-cross-checked review (was transcript-only)
  and committed it: **`docs/CW_DECODER_REVIEW.md`** (Tiers A–D). Target =
  `src/dsp/CwDecoder.cpp/.h` (field-tuned constants).
- Operator chose the safe **A-slice = A1+A2+A3+A5** to do next (nearest-code
  `?` rescue / AFC center-bias / per-char confidence / crash-proof peak-snap
  — none touch the tuned constants; rule: never corrupt a correct decode).
  Then **B1** (independent dah-length + the √3-vs-1.565 boundary
  discontinuity). NOT STARTED. See [[project-lyra-cpp-cw-decoder-accuracy]].

## 2026-06-23 — v0.4.6 RELEASED: connectivity + multi-radio + front-panel polish

Working directly on `main` (last release v0.4.5 `4a106d5`). Connectivity/
polish release — no wire/TX/audio-path changes; all UI / discovery / installer.

### Installer (lyra.iss)
- Auto Windows Firewall **inbound** rules for `lyra.exe`: UDP (HL2 discovery
  reply + EP6 RX stream) + TCP (TCI server, future networked clients). Named
  "Lyra SDR", delete-then-add (idempotent), removed on uninstall. **Fixes the
  "needs admin to connect" report** — the unsolicited inbound discovery reply
  was being dropped for a non-elevated launch. Matches Thetis's WiX
  FirewallException approach.
- Opt-in **`NetworkThrottlingIndex=0xffffffff`** task (default UNticked) — the
  classic multimedia network-throttle "gaming" tweak; `[Registry]`, left in
  place on uninstall. Mirrors Thetis `NetworkThrottle.cs`.

### Discovery (hl2_discovery.{h,cpp})
- **Dual broadcast**: `sendBroadcastFromAllSockets` now sends to both
  `255.255.255.255` AND each NIC's subnet-directed broadcast (`entry.broadcast()`).
- **`probe(ip)`** — directed unicast discovery for Add-by-IP (own socket +
  1 s deadline, reuses parseReply→radioFound). Independent of the sweep.

### Settings → Hardware radio panel (settingsdialog.cpp)
- **Add by IP** fires `probe()` → the placeholder upgrades in place to the real
  board/gw/rx (addRadio changed skip-on-dup → update-in-place).
- **Double-click a row → Open**; **connected radio shown green + bold**
  (item roles, survives re-render); one-line helper.

### Front panel (mainwindow.{h,cpp})
- **Start green / Stop red** (backing QToolButton), **Connected green /
  Disconnected red / Connecting-Scanning amber** (connStatus_).
- Header chip strip: new **Options:** separator+label so **CTUN** / **WF-ID**
  no longer read as RX-DSP functions.

### Release
- Version bump CMakeLists `0.4.5→0.4.6` + lyra.iss AppVersion. Release note
  `docs/releases/v0.4.6.md`; USER_GUIDE header + Radio sections; README
  Features (v0.4.6). Tag `v0.4.6`, installer rebuilt, pushed to origin/main +
  GitHub release.

---

## 2026-06-22 — v0.4.5 RELEASED: CTUNE rebuild + Waterfall ID + CW macros

Branch `feature/ctune-rebuild-wfid`; `main` was at v0.4.4. This is the v0.4.5
release.

### #174 CTUNE rebuilt (operator: v0.4.4 "built wrong")
- 4-agent Thetis-vs-Lyra audit. Centre/shift/edge decision relocated from QML
  into the C++ RX-NCO chokepoint (`HL2Stream::pushEffectiveRxFreq`) so EVERY
  tune source behaves (dial / click / keypad / band+memory / TCI spot): smooth-
  scroll at the 5% display margin, far-jump re-centre, zoom force-centre, IQ-
  Nyquist clamp. `RXANBPSetShiftFrequency` added so notches/NB track the shift.
- Commit `29acc6d` (design docs `0e0f2f2`).

### #175 Waterfall callsign ID
- TX courtesy ID: the call rendered as a readable image in the SSB passband.
  Header arm chip (USB/LSB only), Settings → TX config (level ≤0.065 / repeat /
  over-drive warning), crash-safe transient flat-TX orchestrator (TCI source +
  whole-rack bypass + DIGU/DIGL, restore on un-key).
- **LSB mirror bug fixed** this session: DIGL flips audio↔RF, so the call came
  out mirrored in LSB (USB was fine). Renderer now pre-mirrors the frequency
  layout for LSB → reads upright on both sidebands. Commit `79d8d43`.

### #176 CW macros — named click / F-key memories
- Redesigned floating CW console into a contest-grade macro bank: named click +
  F-key (F1–F12, global) memories, a "My macros" group the operator builds out,
  live "sending" state, Repeat, a contact row (His call / Name / RST / #).
- Tokens: contact `{CALL}/{NAME}/{RST}/{#}` + `{MYCALL}`, plus operator personal
  tokens (`{ME}/{PWR}/{QTH}/{RIG}/...`), with a click-to-insert palette.
- New `CwMacroModel` persists macros / tokens / serial to QSettings. F-keys via
  `MainWindow::keyPressEvent` (QML Shortcuts don't fire in the QQuickWidget
  docks). Commit `9f0d054`.

### Settings dialog
- Per-tab scroll-area + larger default size (long tabs were clipping).

### Release
- Version bumped (`CMakeLists.txt` + `installer/lyra.iss` → 0.4.5), release note
  `docs/releases/v0.4.5.md`, User Guide CW section updated, annotated tag
  `v0.4.5`, installer `dist/Lyra-Setup-0.4.5.exe`, pushed to GitHub; `main`
  fast-forwarded to the release.

---

## 2026-06-21 — v0.4.3 RELEASED: 60 m band plan + tune-drive modes + BCD polish

Operator quality-of-life batch + release. All on `main` (project commits
releases directly to `main`, tags + GitHub release + installer). Released
mid-afternoon, then held for the day.

### 60 m band plan (`bandplan.{h,cpp}`, `PanadapterPanel.qml`)
- **Clickable 60 m channels**: US CH1–CH5 now render as always-visible
  labelled bars (a touch larger), each a MouseArea that QSYs (`Prefs.mode=
  "USB"` + `Stream.setRx1FreqHz(qsy)`) to the channel's USB dial — same
  one-click idiom as the FT8/FT4/PSK watering-hole markers. `segments()`
  flags `channel`/`qsy` per sub-band; delegate gained `chan` prop +
  conditional label size/visibility + tooltip.
- **Region/country-correct 60 m**: precise US channel dial freqs
  (5330.5/5346.5/5357.0/5371.5/5403.5 kHz); WRC-15 contiguous band
  (5351.5–5366.5) added to `r1Bands()` (was missing). New **Country**
  override layer: `regionBands(region)` + `countryBands(country)` +
  `bandsFor(region, country)` merge (replaces same-named band in place).
  `BandPlan::country()` + `bandPlanCountryChanged→regionChanged`; all 3
  `bandsFor` call sites updated. UK = its 11 RSGB/Ofcom segments; Canada =
  WRC-15. `Prefs.bandPlanCountry` (key `band_plan/country`, default "AUTO",
  validated vs {AUTO,UK,CA}). Settings → Hardware Country combo added.
- Verify-don't-guess applied: UK 11-segment list WebFetch-confirmed
  (rsgb.org); AskUserQuestion before writing international freqs.

### Tune-drive selector — #95 (`prefs`, `main.cpp`, `Profile`, settings, TxPanel)
- Replaced bool `useTuneDrive` with enum `tuneDriveMode`
  {Slider=0/Tune=1/Fixed=2} + `fixedTuneDrivePct` (default 25), one-time
  QSettings migration (legacy bool → 0/1). main.cpp tune orchestrator
  rewritten to a `tunePctFor()` lambda + mode-aware arm/release (fixes a
  latent mode-switch-mid-tune restore edge). Profile capture/apply +
  TxPanel `visible: Prefs.tuneDriveMode === 1` updated.
- **Max-drive cap is honored**: wire chokepoint `setTxDriveLevel`
  (`std::min(maxDriveRaw(), …)`) already covers TUN; added cosmetic
  defense-in-depth — Tune/Fixed spinners `setRange(0, maxDrivePct())` +
  live follow on `maxDrivePctChanged`. So TUN can't exceed the operator's
  Max-TX-drive ceiling.

### 11 m → 10 m BCD option (`usb_bcd.{h,cpp}`, settingsdialog)
- New `elevenAsTen` (key `hw/bcd11as10`, default ON), mirroring the
  existing `sixtyAsForty`. Substitution lives in `reapply()`: an 11 m/CB
  freq (`cbBandIndexForFreq`>=0, amateur table returns -1) remaps to the
  10 m amateur index → emits **BCD 9**. Settings → Hardware checkbox.
- Plus an amber regulatory advisory under the BCD options: "Operate only
  within the maximum power and band limits permitted by your country /
  region's regulations." (Operator-requested — states it plainly at the
  point of use re: 11 m abuse.) USER_GUIDE BCD section mirrors it.

### Docs + release
- USER_GUIDE: Band-plan (Region+Country+60 m channels), Settings → TX
  (Tune-drive subsection + Max-drive cap note), USB-BCD (11 m option +
  advisory). New `docs/releases/v0.4.3.md` (= GitHub release body).
- Version 0.4.2 → 0.4.3 (`CMakeLists.txt` + `installer/lyra.iss`); full
  rebuild (LYRA_VERSION embedded, USER_GUIDE resource recompiled) + ISCC →
  `dist/Lyra-Setup-0.4.3.exe` (68 MB).
- `06921f2` Release v0.4.3 (16 files, +598/−158) + the already-on-main
  help-doc commit `4e62bb9` (log-capture section) rode out together. Tag
  `v0.4.3`, pushed `main` (`ddd6677..06921f2`) + tag, GitHub release
  published with installer attached + verified.

### Standing rules in effect
- NO PATCHING (tune-mode done as a proper enum + migration, not a shim).
- Commit/push only when asked — operator explicitly requested this release.
- Verify-don't-guess for regulatory data (60 m freqs, UK segments).

---

## 2026-06-19 — v0.4.1 RELEASED: TX protection (SWR cut + drive cap) + PHROT

Short session: shipped the TX-protection arc, a PHROT parity feature with a
digital auto-gate, doc updates, then cut + pushed **v0.4.1** (main FF'd,
tag + GitHub release + installer). All on `main`.

### TX protection — #169 SWR + #170 drive cap (SHIPPED + operator-bench-confirmed)
- **#169 SWR Cut** (`6c3e8e7`): a 50 ms QTimer (`swrEvalTimer_`) armed on the
  MOX edge in `HL2Stream` decodes `prn->tx[0].fwd/rev_power` →
  ρ=√(rev/fwd), SWR=(1+ρ)/(1−ρ) (calibration-free). Trips through the single
  `requestMox(false)` unkey funnel above a user threshold (default 5:1).
  Four false-trigger guards: blank window, fwd/rev power floors (NaN-safe),
  dwell. Latch + reason + **PROT lamp** on TxPanel (gray/green/red, elided so
  it can't overflow the ATT lamp). Settings → TX group (enable + limit +
  Cut/Fold + fold-floor + during-tune).
- **#169 Phase 1b Fold**: optional action — steps drive ×0.5 via
  `applyDriveLevelNoPersist` (no QSettings write, fold restored next keydown)
  and escalates to a hard trip at the floor.
- **#170a drive cap**: `maxDrivePct` (1..100%) clamps every drive write at the
  one chokepoint `setTxDriveLevel` (`std::min(maxDriveRaw(), …)`) — covers the
  operator slider, TUN, BandMemory restore, TCI DRIVE alike, + re-clamps live
  on lowering. Operator bench-confirmed ("ran slider max down, couldn't pull
  more"). **#170b over-power trip DEFERRED** by operator ("fine as it is").
- Design doc `docs/architecture/tx_protection_design.md` stamped COMPLETE.

### PHROT — #109 phase rotator toggle + digital auto-off (SHIPPED)
- WDSP TXA `SetTXAPHROTRun` exposed via the `wire/wdspcalls` X-macro seam +
  a `TxControl.setPhrotRun` callback (`d99794d`). `phrotEnabled` Q_PROPERTY
  (Settings → TX checkbox, default ON = WDSP/reference posture). Parity with
  the reference Setup PHROT control — it is fully exposed there, not a hidden
  knob (operator asked; verified).
- **Auto-off in digital** (`165e176`): one chokepoint `applyPhrotRun()`
  computes `run = phrotEnabled_ && !(txMode==DIGU|DIGL)` (WDSP mode 7/9) and
  pushes via the callback — called from the operator setter, **every
  `setTxMode` edge**, and `registerTxControl` (channel open). The checkbox is
  now the operator's *voice-mode* intent; DIGU/DIGL switch it off on the wire
  and back on in voice modes (mirrors the native-rack `SetTxRackBypass` gate
  and the RX EQ #59 mode gate).
- USER_GUIDE: new Phase Rotator section incl. a **"PHROT and wide / ESSB
  audio (4–10 kHz)"** note (helps punch on comms-grade wide SSB; all-pass
  group delay smears hi-fi ESSB → many ops leave it off; A/B on own voice).

### Release
- v0.4.0 already shipped, so this is **v0.4.1**. Bumped CMakeLists +
  installer.iss, rebuilt clean, ISCC → `dist/Lyra-Setup-0.4.1.exe` (68.1 MB).
- Commits `6c3e8e7` (SWR/drive-cap/#172/docs) → `d99794d` (PHROT toggle) →
  `165e176` (PHROT digital auto-off) → `eff9a5d` (version bump). Pushed
  `main` (`1c6de15..eff9a5d`), tag `v0.4.1`, GitHub release w/ installer.
- One snag: LNK1104 (operator had Lyra running) — asked operator to close,
  relinked clean (the standing don't-force-kill-operator-Lyra rule).

---

## 2026-06-18 — CW TX ON AIR: paddle + keyboard console + CW-over-TCI (#105)

CW transmit went from "design mapped" to fully on-air this session.

### CW-2 paddle (gateware iambic) — SHIPPED + operator-confirmed
- `applyCwKeyerEnable` arms `prn->cw.cw_enable` in CW mode (CWL/CWU) — the
  firmware-keyer + sidetone master; paddle in the HL2 KEY jack keys the
  carrier autonomously (gateware CWTX), host stays out of timing.
- Carrier-on-marker fix: keyed carrier rides the marker in both sidebands
  (single shared `cwPitchHz`); the off-marker was a DISPLAY bug (host MOX
  off the gateware key line switched to the TX analyzer) — fixed by NOT
  forwarding host MOX in firmware-keyer CW (QSK). Break-in selector
  QSK(default)/Semi/Manual; CW MON sidetone slider on the Audio panel.
- Manual break-in foot-switch confirmed ("tone footswitch and paddle"):
  foot switch = PTT hold (gateware PTTTX), paddle keys within (→CWTX).
- Vestigial `cwSidetoneFreqHz` path removed (`14be1ed`) — CW pitch is the
  single freq source.

### CW-3a/3b host software keyer (CWX) + CW console — SHIPPED `8904749`
- Gateware-verified (radio.v / dsopenhpsdr1.v): host CWX keys the carrier
  via the SAME CWTX path as the paddle — host drives `tx[0].cwx` (per
  element) + `cwx_ptt` (held per message); `cw_enable` (CW-mode armed)
  enables BOTH the host EP2 overlay AND the gateware CWX decode
  (`cmd_data[24]` = the 0x0f C1 bit). NO WDSP / no host carrier gen. The
  earlier "dot/dash bit" guess was WRONG — tracing corrected it.
- `tx/CwMorse` (table + PARIS timing + operator weight, pure/testable),
  `tx/CwKeyer` (dedicated-thread element pump, monotonic absolute-deadline
  scheduling, interruptible abort), HL2Stream `sendCw`/`abortCw` +
  `setCwxKey`/`setCwxPtt` + lazy keyer + abort-on-close.
- `qml/CwConsolePanel` — chip→floating console (audio-rack idiom): WPM,
  type+Enter sends / Esc aborts, Send+Stop, reserved CW-5 decoder pane.
  "CW" launcher chip after the TX DSP strip.
- Design doc `docs/architecture/cw3_software_keyer_design.md`.

### CW-4 CW-over-TCI — SHIPPED `fcfcb6b`, EESDR-spec-verified
- `tci_server` dispatch routes the EESDR TCI CW commands into the CW-3
  keyer: `cw_macros` (plain send), `cw_msg` (contest prefix/call/suffix
  + `$N` repeat), `cw_macros_speed`/`cw_keyer_speed` (WPM, bidirectional),
  `cw_macros_stop`. Reserved-char un-escape `^~*`→`:,;`. sendCw/abortCw
  marshalled to the stream thread (QueuedConnection) — no race vs the
  console. CW-mode-only.
- **Verified vs the EESDR TCI protocol manual** (operator added
  `docs/TCI Protocol.pdf` §3.2) + Thetis TCIServer.cs + SDRLogger+. The
  EESDR cross-check caught 3 drafting errors: `cw_macros_empty` is a
  server→client terminal signal (not a stop), reserved-char un-escape was
  missing, and `cw_msg` (the canonical contest cmd) was missing.

### Docs
- `docs/help/USER_GUIDE.md` (`2a09824`): new "CW operating" section
  (paddle/console/TCI + break-in + CW MON) + "CW keying over TCI" + TOC.

### Standing directive (recorded)
- TCI work verifies against BOTH Thetis `TCIServer.cs` AND the EESDR TCI
  manual (`docs/TCI Protocol.pdf`) — the authoritative spec.

### Next
- **CW-5 (#173)** — RX CW decoder (faithful port of SDRLogger+'s
  Bayesian/AFC/Farnsworth decoder, `hamlog/cw.html`) into the console's
  reserved pane + a macros field. Deep-read the decoder first.
- CW follow-ons: Semi/Manual host-MOX for CWX, F-key memories, CWFWKeyer
  toggle; TCI `cw_terminal`/`cw_macros_delay`/`|..|`+`<>` macro syntax.
- On-air TCI CW bench via SDRLogger+ still TODO (spec-verified + builds).

### Commit summary (today)
```
2a09824 docs(cw): USER_GUIDE — CW operating + CW-over-TCI
fcfcb6b feat(cw): CW-4 — CW keying over TCI, verified vs EESDR TCI spec
8904749 feat(cw): CW-3a/3b — host software keyer (CWX) + CW console
14be1ed refactor(cw): remove the vestigial cwSidetoneFreqHz path
```

---

## 2026-06-17 EOD — v0.4.0 RELEASED + main consolidated + CW TX mapped

### UI readability batch (shipped `8fe2100`)
- TX DSP launcher chips (Speech/EQ/Combinator/Plating) → boxed buttons that
  fill with the accent when their panel is open (were flat text-only).
- EQ graph: frequency-marker row across the top; x-range tracks the TX audio
  passband (SSB/digital = high edge, AM/DSB/FM/SAM = half occupied) + amber
  dashed TX-edge marker; gain scale bigger (bold 11px) + brighter, labelled
  every 3 dB (added ±3/±9 rows), 0 dB bright blue.
- Speech/Combinator/Plate parameter labels + readouts brighter + bold 13px;
  Combinator per-band micro-labels 9/10→11px. Operator: "easier on the old
  eyes." `.gitignore` += `_run_lyra_vac1.bat`, `Line`.

### main consolidated (operator request)
- `origin/main` fast-forwarded `a3a517f..8fe2100` (clean, 32 commits, no
  foreign divergence). Local `main` moved up + checked out — **`main` is the
  working trunk again** (TX largely done; stops the "forgot to FF main"
  risk). `tx-rebuild` kept as a legacy ref.

### v0.4.0 RELEASED
- Version bump 0.3.1→0.4.0 (CMakeLists + installer/lyra.iss); release notes
  `docs/releases/v0.4.0.md`; commit `88f17f2`. Full rebuild (version baked),
  Inno Setup installer `dist/Lyra-Setup-0.4.0.exe` (~68 MB). Annotated tag
  `v0.4.0`, pushed main + tag, GitHub Release published with installer
  attached + verified. `main`==`origin/main`==`v0.4.0`==`88f17f2`.

### #156 (restart-after-hard-kill) — PARKED
- Operator empirical: a real accidental hard kill of lyra.exe mid-VOICE-TX
  (my pre-build force-kill fired while they were tuning) → **restarted
  CLEAN**. Only ever one observed hang (2026-06-13 mid-TUN). Intermittent →
  parked until it reproduces; prime suspect noted (infinite
  `hReadThreadInitSem.acquire()`), optional bounded-timeout hardening if
  revisited. PROCESS RULE logged: don't force-kill lyra for a build when the
  operator may be on air — ask first.

### CW TX (#105) — DESIGN MAPPED (rainy-night research, no code)
- 2-lane reference dive (Thetis host cwx.cs/console.cs/TXA.c + HL2 wire
  networkproto1.c + operator's HL2+ gateware RTL). Wrote
  **`docs/architecture/cw_tx_design.md`** — full port plan.
- THE finding: HL2 CW is **100 % gateware-keyed** (no host/WDSP carrier;
  `TXA.c` SetTXAMode has no CWL/CWU case; `radio.v:1145` bypasses EP2 TX-I/Q
  during CW, feeds the FPGA envelope ramp; FPGA also makes the hardware
  sidetone). Host just sends key-state. **The verbatim EP2 CW packer is
  ALREADY in tree** (`NetworkProto1.cpp:99-108`, dormant) + `prn->cw` seeded;
  EP6 dot/ptt decode already avoids the reference's left-shift bug. Missing:
  CW C&C composer cases (0x0f/0x10/0x0b) + the native CWX keyer engine + FSM
  CW PttSource + UX. Build order C-1..C-5 in the doc. ⚠ flagged: 0x0b
  register collision with the shipped ATT-on-TX step-att — resolve before
  composing.

---

## 2026-06-17 (cont. — AM/DSB/FM native TX modulation + AM carrier #106/#93)

### #165 follow-on → basic WDSP-native AM / DSB / FM transmit
Operator: keying AM showed only the upper sideband. RX was correct; the
one-sided signal was the operator's own TX — the modulator was SSB-only.
- **`hl2_stream.cpp` `setTxMode`**: widened the WDSP-mode clamp `0..1` →
  `0..13` so AM(6)/DSB(2)/FM(5)/SAM(10) reach `SetTXAMode` instead of
  being pinned to USB.
- **`main.cpp` `wdspTxModeFor`**: full mode map (LSB0…SAM10, default USB),
  replacing the AM/FM/DSB/SAM→USB collapse.
- **`main.cpp` `pushTxFilter`**: per-mode sign-coded bandpass (TX mirror of
  the RX §14.2 convention) — USB-side `+low..+high`, LSB-side `-high..-low`,
  double-sideband symmetric. NEVER calls `SetTXABandpassRun` (§15.23 trap);
  `SetTXABandpassFreqs` + `SetTXAMode` configure bp0. FM CTCSS silenced via
  `SetTXACTCSSRun(ch,0)` (TXA defaults it ON at 100 Hz).
- **TUN ∓cw_pitch offset** generalized in `txDdsHzForTune` /
  `txAnalyzerOffsetHz` so double-sideband modes stay centred.
- New wire bindings: `SetTXACTCSSRun`, `SetTXAAMCarrierLevel`
  (`wdspcalls.{h,cpp}` — pointer + X-table + extern).

### Doubled-bandwidth bug (operator bench) — FIXED
AM straddled centre correctly but occupied ~2× the set TX BW. Cause: the
symmetric branch used `±high` = 2×high total. `txf->high` is the set TX BW
(`Prefs.txBandwidth`); a double-sideband signal occupies the full ±span, so
the edge must be **±high/2** — mirroring `WdspEngine::computePassband`
(`half = bw/2`, ±half) which also draws the filter markers. Now AM at 6k =
±3k = 6k total, inside the markers. Operator bench: **correct**.

### AM carrier level control (#93) — operator-facing, reference-faithful
- HL2Stream `amCarrierPct` Q_PROPERTY (+ getter/setter/signal/member,
  QSettings `tx/amCarrierPct`), `TxControl.setAmCarrierLevel` callback,
  `main.cpp` lambda → `SetTXAAMCarrierLevel`. Settings → TX "AM Carrier"
  spinbox (0–100 %).
- **Mapping matches the reference exactly** (verified at
  `setup.cs:9846`): UI is **% of standard carrier POWER**, coefficient =
  `√(pct/100) × 0.5`, **default 100 = standard AM** (= 25 % of PEP). So
  operator-stored values transfer 1:1. Helper `amPctToCarrierLevel()`,
  applied at both push sites. (Earlier draft passed pct→coeff straight,
  default 50 — corrected before bench.)
- N8SDR's exported profiles confirm the intent: `8K-AM-N8SDR`=40,
  `*-N8SDR-AM`=84, `AM 10k CFC`=95 (reduced/controlled carrier).

### Profile import — decided NOT to pursue (operator call)
DSP chain is Lyra-native (EQ/Speech/Combinator/Plate, not WDSP/CFC), so
importing reference profiles is pointless — the *sound* lives in the native
rack, which has no reference equivalent. `amCarrierPct` NOT added to the
Profile struct. Profile manager stays as-is.

### Status
All built clean + operator-bench-confirmed (AM/DSB/FM both sidebands inside
the markers; AM carrier behaves). Committed this session; #106/#93 closed.

---

## 2026-06-17 (cont. — doc reconciliation + ATT-on-TX UI §15.31)

### Doc reconciliation (operator-delegated while out)
- `THETIS_DIRECT_PORT_PLAN.md` reconciled (agent pass + my accuracy
  corrections): obbuffs/P1 → [DONE]; compress/cfcomp/eqp → [N/A]
  (native-superseded); wcpagc leveler/ALC → [DONE] but **softened** —
  metering "shipped in code; #160 bench close-out + Brent/Timmy
  field-confirm still IN PROGRESS" (not over-claimed). PDF + DOCX
  regenerated via the generalized `tools/sync_execution_plan.py`
  (now `--md/--docx/--pdf-path/--title/--no-docx`). SESSION_LOG.pdf
  regenerated.

### §15.31 — ATT-on-TX operator surface + visible lamp (operator-flagged)
Operator: "ATT on TX doesn't appear to be live — I don't see the LNA
drop to −31 on TX." TRACED the path (did NOT re-assert from the doc —
the [DONE] I'd let stand was operator-disproven):
- **WIRE path IS live + correct**: `fsmAdvance` raises
  `setTxStepAttnDb(31)` on keydown (`hl2_stream.cpp:1651`) →
  `tx_step_attn=0` → XmitBit-gated `compose_case_11` C4 = `0x40` →
  ak4951v4 cmd_addr 0x0a rx_gain = 0 = min LNA during TX. The front end
  WAS being attenuated.
- **Root of the report = VISIBILITY gap** (operator confirmed "1 and 3"):
  the LNA readout shows the RX setpoint (unchanged on TX); no on-screen
  confirmation, where Thetis visibly jumps the ATT to 31. Also the
  `src/tx/AttOnTxPolicy.{h,cpp}` class is **unused / wire-inert** — the
  live mechanism is inline in hl2_stream.cpp; the doc citation was wrong.
- **FIX** (operator-locked design confirmed before code; matches Thetis
  Setup → General → Ant/Filters → "ATT on Tx" ✓ / "ATT: 31"):
  * HL2Stream: `attOnTxEnabled_` (default ON) + `attOnTxDb_` (default
    31), Q_PROPERTY + setters + QSettings (`tx/attOnTx*`); `fsmAdvance`
    gates on the toggle + uses the value (replaces hardcoded
    `kAttOnTxDb`); mid-TX toggle re-applies live. Disabled → axis 0
    (= reference `SetTxAttenData(0)`).
  * TxPanel.qml: "ATT" lamp in the gap between the Mic and Tune sliders,
    AUTO/TUN/MOX lit idiom — gray `ATT off` / orange `ATT 31` armed (RX)
    / solid-red + white-text `ATT -31` engaged (keyed). Engaged colour
    fixed from a salmon-on-maroon blend that read PINK → MOX-style solid
    red; label shows the negative (applied attenuation) on TX per
    operator request.
  * settingsdialog.cpp: Settings → TX → "ATT on TX (RX-ADC protection)"
    group (left/safety column) — Enable + ATT dB (0..31) spin,
    bidirectional with the lamp.
- Builds clean; operator-approved the UI in review. THETIS row kept
  **[WIP]** (flips [DONE] only on the on-air bench confirm). AttOnTxPolicy
  class still unused (flagged for later delete-or-wire-up). Task #114
  (broader: panadapter offset + PA-enable safety) stays pending.

---

## 2026-06-17 (build day — #162/#163 ship, #90 TX monitor 3-route, #164 Out picker; all HL2-confirmed)

Branch `tx-rebuild` (NOT pushed; `main`/origin stays v0.3.1). Every commit
below was operator-bench-confirmed on the HL2+ before it landed.

### #162 TX Speech profile NOT saving — FIXED (root cause CORRECTED vs the 06-16 theory)
The 06-16 "binding-break / UI-refresh" theory was **WRONG**. Operator
pushback ("are we even capturing the Speech panel?") + a registry dump
(`HKCU\Software\N8SDR\Lyra-cpp\profiles\item\<name>`) proved it: the
`speech` blob saved all-default. **Real root cause = QML signal
shadowing** — the Stage card's custom `signal toggled(bool)` was shadowed
by `AbstractButton.toggled`, so the ON button's `onClicked: toggled(checked)`
emitted the Button's signal, never the model write → Speech always saved
defaults.
- `c17960c`: qualify the emit `stage.toggled(checked)` → the model write
  fires; registry-verified (`deessOn:true` … now persist).
- `46a428f`: `Binding{target:en; property:"checked"; value:on}` re-asserts
  the toggle lamp from the model on recall (a checkable Button flips its own
  `checked` on click, severing the inline binding).
- Operator-confirmed: toggles save, recall re-lights lamps + restores
  sliders, 2 fresh profiles round-tripped. Lesson: clean build + qmllint ≠
  proof — verify by registry dump / launch.

### #163 Rack-lamp dim in digital/CW + EQ analyzer recolor (cosmetic, operator-requested)
- `9be1fc1`: Speech/Combinator/Plate panels gray the ON lamp + dim controls
  + show an amber "bypassed (MODE)" header hint in DIGU/DIGL **and CW**
  (digital → `SetTxRackBypass`; CW → no mic). Purely visual off a
  `WdspEngine.mode` binding; model untouched, re-lights on USB/LSB.
- `558d5d9`: EqPanel — same dim + the operator's analyzer recolor:
  Accumulate peak-hold → RED, pre-EQ "Before" overlay → WHITE (were
  near-white/amber), live "After" stays cyan. Line + RTA. Doc `66adc5e`.

### #90 TX audio monitor — ALL THREE reference-faithful routes SHIPPED (#90 closed)
ONE shared post-rack tap (`xcmaster` case 1, READ-ONLY on `pcm->in`, after
the rack / before `fexchange0`) → lock-free SPSC `MonitorRing`
(`src/dsp/MonitorRing.h`); drained ONCE at the top of `dispatchAudioFrame`
into `monScratch_`, shared by all routes. WdspEngine `monEnabled_`/
`monVolume_` (atomics, persisted); Audio-panel MON TX toggle + Monitor slider.
- Route 1 (HL2 jack, `6e6a282`): monitor mono on L/R in place of the
  auto-muted RX when MOX up + MON on, via the existing `OutBound(0)` path.
- Routes 2+3 (`e1fd186`): Route 2 = VAC stream-2 + `SetIVACmon`/
  `SetIVACmonVol` (ivacGet-gated, re-applied in `rebuildVac1`); Route 3 =
  TCI RX-audio (tap moved into `dispatchAudioFrame`; MON→monitor / TX→mute /
  RX→live).
- **Reference-VERIFIED (operator asked "is this Thetis or have we drifted?"):**
  Thetis `audio.cs:386-404` drives `SetIVACmon(0,1)`+`SetIVACmonVol(0,
  monitor_volume)` on the PRIMARY VAC (id 0 = our `kVac1Id`) and
  `SetTCIRxAudioMox/Mon` (368-404) — the single MON drives jack + VAC + TCI,
  exactly as built. NOT drift.
- ⚠ Faithful behavior change: TCI RX-audio is now MUTED on the air (was
  live RX during TX). Operator-bench-confirmed harmless — MSHV FT8-over-TCI
  decoded + completed a QSO (N8SDR↔KB6DAD to 73) with MON TX on, TX 5.1 W,
  DIGU rack auto-bypassed. Docs `266d8cd`.

### #164 "Out" output-device picker wired + MON→MON TX relabel (`740f769`, doc `dc45485`)
The disabled placeholder "Out" button is now a live one-click output picker:
a styled top-level popup (`popupType: Popup.Window` so it ESCAPES the short
Audio-panel QQuickWidget clip — a plain Item-type popup was getting chopped
at the panel bottom with no scroll). Lists HL2 jack + PC devices, current
▶/cyan/bold, hover highlight, as-needed scrollbar. Reads the same
`audioOutputDevices()`/`setAudioOutputDevice`/`audioDeviceIndex` the
Settings → Audio combo uses; full config stays in Settings. MON button
relabeled **MON TX** so its purpose is clear. ComboBox considered + rejected
(long device names blow out the row width).

### New items logged (not started)
- **#165 (⚠ bug):** AM/DSB modes only produce the UPPER sideband (lower
  missing). AM/DSB are double-sideband → the WDSP passband for those modes is
  likely set one-sided (USB-only) instead of symmetric −W..+W. RX-DSP
  investigation, queued next.
- **GitHub / `main` branch:** operator has questions to raise on return.

### Docs
- USER_GUIDE updated for the rack dim/recolor, the monitor (MON TX + the
  VAC/TCI routes), and the Out picker.
- This session-log entry + a `THETIS_DIRECT_PORT_PLAN.md` reconciliation
  pass (mark done-but-unmarked items, add a post-P4 shipped-work entry).
  `.pdf`/`.docx` of both NOT regenerated (no pandoc/doc-gen tool on this
  box) — MD sources updated; PDF/docx regen pending the operator's tool.

## 2026-06-16 (design + investigation day — NO commits; two items parked for tomorrow)

Context: native TX DSP rack (EQ/Speech/Combinator/Plate) + #49 Profile
Manager bundle in flight; operator field-testing profiles on HL2+.

### #90 TX audio monitor — DESIGN LOCKED, parked for tomorrow's build
Operator ask: MON button + Monitor volume slider on the Audio panel upper
row. Studied Thetis MON / MonitorVolume (gates processed TX audio into the
RX audio mixer + `SetIVACmon`; the verbatim `xMixAudio(0,0,…,out[2])`
"mix monitor audio" line is already carried at `CMaster.cpp:458`, fed empty
out[2]). Operator chose **both: HL2 jack now, separate PC device later**.
- **Key finding:** `WdspEngine::dispatchAudioFrame()` (wdsp_engine.cpp:3100)
  STAYS LIVE during TX — RX audio is just gain-zeroed by the
  `txMuted_ && autoMuteOnTx_` gate, then `OutBound(0)` → EP2 LR → AK4951
  jack. So the jack is live-but-silent during TX = the inject seam (no
  fight with RX stand-down).
- **Locked design — ONE shared tap:** in `xcmaster` case 1 capture
  `pcm->in[stream]` AFTER the rack, BEFORE `fexchange0` (the processed
  "what you sound like" mic audio) → small SPSC ring.
  - Route 1 (HL2 jack, NOW): drain ring in `dispatchAudioFrame`; when MOX
    up + MON on, output monitor audio (scaled by new Monitor-vol) in place
    of muted RX audio → existing `OutBound(0)` path. No new stream.
  - Route 2 (separate PC device, LATER): feed same tap into
    `xvacOUT(kVac1Id, stream 2)` (replace `vacMonSilence_`) + `SetIVACmon`
    = **IVAC Stage 5 / DL-4** done reference-faithfully.
  - Tradeoff (note in tooltip): post-rack tap is pre-WDSP-ALC/bandpass —
    hear your rack DSP faithfully, not WDSP's corrective ALC.
- Operator: **"hold for tomorrow's build."**

### #162 Speech-panel profile not saving — ROOT-CAUSED, parked first-thing tomorrow
Operator: TX Speech panel options aren't saved/recalled by Profiles while
EQ/Combinator/Plate are. Decisive repro: turned De-esser ON, recalled an
all-off profile → **De-esser button stayed ON.**
- Traced the full path: capture (main.cpp:752) / apply (main.cpp:798) /
  `Profile` toJson+fromJson+sameValues / `SpeechModel` saveState+loadState /
  single instance / context property — **all symmetric & correct.** The
  data DOES save and DOES apply to the model + DSP.
- **Root cause = QML binding-break (UI-refresh, not persistence):**
  SpeechPanel (and PlatePanel/CombinatorPanel) use `Button{checkable:true;
  checked:<model>}` + `Slider{value:<model>}`. The first click/drag makes
  Qt Quick write `checked`/`value` imperatively, **severing the binding to
  the model.** Recall then updates model+DSP+numeric labels but NOT the
  toggle/slider visuals — the button is a dead light. EqPanel dodges this
  (non-checkable toggles + pure `checked:` binding off a `rev` counter +
  `Q_INVOKABLE` getters). De-esser is the clearest demonstrator because a
  stuck binary toggle is unambiguous; Plate/Combinator share the latent
  pattern but only surface after mid-session interact-then-recall.
- **Fix (#162, high pri):** adopt EqPanel's binding-safe pattern in the
  three rack panels. QML-only, no DSP/wire risk.

### Tomorrow's order (operator-set)
1. **#162** Speech (+Plate/Combinator) panel binding-safe rebuild — FIRST.
2. **#90** TX monitor build (Route 1 HL2 jack; Route 2 PC device as the
   follow-on within #90 / IVAC Stage 5).

Memory: `project-lyra-cpp-monitor` (the #90 locked design).

---

## 2026-06-13 (Saturday — ⭐ TX BRING-UP COMPLETE + v0.2.3 RELEASED)

**`main` == `tx-rebuild` == HEAD `e8aafbc` (pushed).**  Tags: milestone
`tx-working-2026-06-13` (at `bd61d07`) + release `v0.2.3` (at `a5dc763`).
GitHub Release **v0.2.3 — "Transmit: SSB + digital (TCI) on the air"**
PUBLISHED with `Lyra-Setup-0.2.3.exe` (67.7 MB) attached, marked Latest.

### P4.b Wire-LIVE switchover — SHIPPED + operator HL2 bench PASSED
- `fb9ec41` P4.b-1 — Wire-LIVE RX-out gate (operator bench PASSED).
- `0b551b4` P4.b-2 — TX wire-LIVE: first modulated RF through the
  direct-port chain (operator HL2 bench).
- `9db2c50` P4.b-2.1 — TX SSB sideband fix: USB transmits USB (9100-verified).
- `84dbb12` fix(tx): TUN panadapter spike on-marker — TX-analyzer crop
  shifted by NCO−dial offset.  Verified Lyra TUN is **Thetis-exact
  zero-beat** (two-offset scheme; the bug was display-only — authoritative
  refs: console.cs:32553-32587 UpdateTXDDSFreq path, postgen 30788-30800,
  readout 22035-22051).
- §7 dead-code retirements (3 build-gated commits): `fe6a438`
  OutboundRing + Ep2SendThread, `99fd24a` txWorkerLoop + composeCC +
  buildEp2KeepaliveTemplate, `e711256` old HL2-audio-jack EP2 ring
  (pushAudio + setHl2AudioSink).
- P4.b bench gates PASSED: SSB voice both sidebands + Phase-3-EXIT
  RF-safety kill-test (RFG drops on hard-kill).

### TCI digital TX re-home — SHIPPED + ON-AIR VALIDATED
- `bd61d07` — filled the §10.3 `src/tci/TciTxBridge.{h,cpp}` skeleton
  (singleton, mono deque, I=Q=mono drain w/ zero-fill underrun,
  `queuedSamples()` for CHRONO).  Registered via
  `SendpInboundTCITxAudio(&TciTxBridge::inboundCb)` after create_cmaster;
  fed by `tci_server.cpp` handleBinaryFrame tap; gated by
  `SetTXTCIAudio(0, micSource=="tci")`.  The verbatim DSP seam (xcmaster
  case 1 use_tci_audio gate) was already present — only the host-side sink
  was missing.
- **Bench: MSHV → TCI → FT8 real on-air QSOs** — N3YDN (FM29),
  F4FLF (JN18, ~4160 mi DX), KO4OIG (EL89), W3EWL/QRP.

### Space-bar PTT toggle (#157) — SHIPPED
- `2824ebf` — `Prefs.spaceBarPttEnabled` (tx/space_bar_ptt_enabled,
  default ON), MainWindow keydown/keyup gate, Settings → Hardware →
  Transmit checkbox under the HW-PTT tickbox.  Operator-verified working.

### Release
- `e8aafbc` — version bump 0.2.2 → 0.2.3 (CMakeLists project(VERSION) +
  installer/lyra.iss AppVersion).  Clean build, ISCC installer, tag,
  GitHub release (notes + installer).

### NEXT (morning)
- **Mic-input paths discussion** (PC/VAC host audio + plain-HL2 vs HL2+).
  Key reframe: a **plain HL2 (no AK4951) has no radio-side mic at all** →
  host audio-in (#102 VAC1) is REQUIRED for those ops to TX phone, not
  optional.  All sources are interchangeable 48 k mono feeders of the same
  `InboundTCITxAudio` seam (TciTxBridge = the template).  §5.4 "single
  mic-source class, no abstraction" rule has expired now that there are
  4 concretes — introduce `ITxAudioSource`.  Capability-aware default
  (plain HL2 → PC/VAC; HL2+ → AK4951 mic).  Decide: TX-in-only vs full
  bidirectional VAC; Qt Multimedia vs WDSP rmatchV.
- Parked: #156 restart-after-hard-kill-mid-TX hang; intermittent
  menu-drag display stutter (monitoring; ruled out as a §7 regression);
  #104 HL2 Line In (codec mux); #103 VAC2.

---

## 2026-06-12 (Friday PM-4 — P4.a prep SHIPPED, both commits wire-inert)

**Branch:** `tx-rebuild`, HEAD `baef866` (pushed).

### P4.a-1 SHIPPED `a4aa8c3` — sendProtocol1Samples verbatim (DORMANT)
- NEW `src/wire/NetworkProto1.cpp` — networkproto1.c PARTIAL: the EP2 writer thread `sendProtocol1Samples` verbatim (networkproto1.c:1204-1267).  Mechanical diff IDENTICAL (whitespace-normalized, accommodations mapped back); sole structural delta = braces around the DEFERRED non-HL2 else-branch (comment-only branch needs braces to compile).  Accommodations documented in the preamble: lyra::wire namespace; the HPSDRModel enum-class mapping; `write_main_loop_hl2(prn->OutBufp)` = the FrameComposer monolithic WriteMainLoop_HL2 equivalent (#121/#122 fold); generic WriteMainLoop (non-HL2/ANAN P1) carried as DEFERRED reference text; (AVRT_PRIORITY) cast + suppress 4100 on the verbatim signature.
- `io_keep_running` global added (network.h:411 verbatim; init 0 so the loop body cannot run even if started prematurely) + the decl in RadioNet.h.
- FrameComposer `write_main_loop_hl2` tail flipped from the Step-14 `outbound_notify_consumed_pair()` translation to the verbatim `ReleaseSemaphore(prn->hobbuffsRun[0/1], 1, 0)` pair.  Dormant-safe: nothing calls write_main_loop_hl2 until P4.b starts the writer; the hobbuffsRun handles are created at the P4.b open() with the quartet.
- DORMANT: nothing `_beginthreadex`'s the function until P4.b's open() (reference start site = StartAudio, netInterface.c:66 + the quartet :68-71).  Build clean, zero new warnings.

### P4.a-2 SHIPPED `baef866` — PostGen seam entries + verbatim field types
- **wdspcalls**: SetTXAPostGen{Run,Mode,ToneMag,ToneFreq,TTMag,TTFreq} for the P4.b TUN re-home (legacy DC-injection TUN dies with the legacy EP2 packer; TUN becomes the reference TXA output-side tone generator).  Pre-cdef audit: NO header declares these — PORT-exported definition sites in wdsp/gen.c only (gen.c:784/792/800/808/817/826), signatures harvested from the definition bodies; presence + exact casing verified against the bundled wdsp.dll export table (PE scan, all 6 PRESENT of 533).  TT pair included per table rule #3 (PS committed; the PS calibration drive is the two-tone).
- **TxReadBufp**: std::vector -> verbatim `double*` (network.h:62) + the verbatim calloc at create_rnet (netInterface.c:1604, 2*sizeof(double)*720, process lifetime — the P2.b OutBufp precedent).  Ep6RecvThread's 5 `.data()` sites -> bare pointer; the vector-size guard reduces to a null check (fixed 1440-double capacity >= 4*spr for every valid nddc).
- **hWriteThreadMain**: std::thread -> verbatim HANDLE (network.h:90).  Zero users existed on the std::thread form; P4.b assigns the verbatim `_beginthreadex(sendProtocol1Samples)` handle.
- Build clean (sole warning = the pre-existing hl2_stream C4456, untouched file).  Commit-message nit: a backtick-quoted literal got eaten by bash substitution again (the P2.b class) — citation survives, no force-push.

### NEXT = P4.b (THE switchover — ONE commit, full operator HL2 bench gate)
- open(): quartet CreateSemaphore ×4 + `prn->hWriteThreadMain = _beginthreadex(sendProtocol1Samples)` (io_keep_running=1 BEFORE start).
- dispatchAudioFrame -> the asioOUT-pattern tee (HL2-jack = real audio into OutBound(0); PC = zeroed tee so EP2 pacing never starves) — the B.6.b-delicate RX switchover.
- `Inbound(inid(1,0), mic_sample_count, prn->TxReadBufp)` at the live EP6 mic-harvest site (Ep6RecvThread, the block already mirrors networkproto1.c:560-579).
- FSM re-home: TXA SetChannelState arming per the §15.25 ground truth + TUN -> SetTXAPostGen.
- Control-plane mapping per the design doc §5 table; retirements per §7 (txWorkerLoop + keepalive + DC-TUN + slew-fill, OutboundRing, Ep2SendThread, FrameComposer cv tail).
- Teardown: io_keep_running=0 -> release both hsend sems once -> join -> CloseHandle quartet -> destroy_obbuffs BEFORE destroy_xmtr.
- Bench gate per design doc §8 incl. explicit TX-state lines + FIRST VOICE through the chain.

---

## 2026-06-12 (Friday PM-3 — P2 audit complete + P2.a eer completion SHIPPED)

**Branch:** `tx-rebuild`, on top of `2897756`.

### P2 fidelity audit (the deliverable that scopes P2.b/c/P4)
- **Reference TX-out chain (HL2 P1), fully read:** `ob_main` -> `sendOutbound` (network.c:1237; P1 branch :1285-1340) — id 1 memcpys 126 complex into `prn->outIQbufp` + Release(hsendIQSem) + Wait(hobbuffsRun[0]); id 0 -> `outLRbufp` + hsendLRSem + hobbuffsRun[1] -> `sendProtocol1Samples` thread (networkproto1.c:1204-1267): WaitForMultipleObjects(both, TRUE) -> eer overwrite if `peer->run && XmitBit` -> `!XmitBit => memset outIQbufp` -> swap_audio_channels -> 16-bit BE quantize + HL2 CW-bit overlay into `OutBufp` -> WriteMainLoop_HL2 -> MetisWriteFrame -> Release both hobbuffsRun.  Resources: bufs calloc'd netInterface.c:1606-1608; semaphore quartet created in StartAudio :68-71; obbuffs rings 0/1 created in UpdateRadioProtocolSampleSize :1856-1857; eer created run=0 per xmtr (cmaster.c:212-224).
- **Lyra dormant Step-14 surface:** functionally-parallel idiom translations predating the verbatim mandate — prn buffers are std::vector (RadioNet.h:552-554), the semaphore quartet became OutboundRing's bool+cv, Ep2SendThread is the sendProtocol1Samples rewrite.  `XmitBit` is already verbatim (extern int, RadioNet.h:733).  Disposition: re-port verbatim, retire the translations at P4 Wire-LIVE.
- **Landmine found:** BOTH reference functions deref `pcm->xmtr[0].peer->run`, but P0.d had deferred the eer creation — `peer` was NULL.  That made P2.a unambiguous and first.

### P2.a SHIPPED — eer completion (verbatim)
- `wire/cmcomm.h`: opaque `eer` typedef replaced by the FULL verbatim struct (wdsp/eer.h:30-49, mechanical diff IDENTICAL); opaque `DELAY` twin typedef added (wdsp/delay.h:32-59).
- `wire/wdspcalls.{h,cpp}`: +5 seam entries — create_eer / destroy_eer / xeer / pSetEERSize / pSetEERSamplerate (signatures verbatim from eer.h:51-75; all five verified present in the bundled wdsp.dll via PE export-table scan).
- `wire/CMaster.cpp`: all four deferred eer sites RESTORED verbatim — create_xmtr `create_eer(run=0, ...)` 12-arg block (cmaster.c:212-224); destroy_xmtr `destroy_eer` (:262, after destroy_ilv per reference order); xcmaster `xeer(pcm->xmtr[tx].peer)` (no-op at run=0 with in==out — verified in eer.c:86-122); SetXmtrChannelOutrate pSetEERSamplerate/pSetEERSize.  Restored-lines verbatim-subset check PASS (18 lines).
- Runtime impact: create_xmtr now also creates/destroys one run=0 eer object per xmtr (heap + CS only); xeer in the TX pump body never executes today (stream-1 pump has no producer).  RX path untouched.  Clean build, zero warnings.

### P2.b SHIPPED (same session) — prn outbound surface verbatim
- `outLRbufp`/`outIQbufp`/`OutBufp` std::vector -> the VERBATIM reference pointer fields (network.h:64-66) + the verbatim calloc set at create_rnet (netInterface.c:1606-1608, mechanical subset PASS).  Lifetime = process (create_rnet call-once) = reference.
- Verbatim HANDLE quartet declared on prn (hsendLRSem/hsendIQSem/hsendEventHandles[2]/hobbuffsRun[2], network.h:92-95) — DORMANT/nullptr until P4's StartAudio equivalent; the Step-14 cv+flags translation stays alongside until P4 retires its consumers.
- Callers bent: OutboundRing `.data()` ×6 -> raw; Ep2SendThread pack casts -> char; `write_main_loop_hl2` parameter -> `const char*` (reference type).  Wire-inert (only create_rnet's allocation runs live).  The lone C4456 in hl2_stream.cpp:776 is PRE-EXISTING (file untouched; surfaced by the header-triggered recompile) — flagged for a separate cleanup.

### P2.c SHIPPED (same session) — sendOutbound verbatim + ob_main restore
- NEW `src/wire/Network.cpp` — network.c PARTIAL direct port: `sendOutbound` verbatim (network.c:1237-1341).  P1 branch LIVE (the outLRbufp/outIQbufp memcpy + hsendLRSem/hsendIQSem release + hobbuffsRun wait handshake, incl. the EER de-interleave sub-branch with its function-local static ptr); ETH branch carried as DEFERRED reference text (Protocol 2/ANAN = v0.4; WriteUDPFrame/udpOUT unported).  Accommodations, each documented inline: the RadioNet.h enum-class mapping on the protocol gate; suppress 4101 (ETH-only locals) + 4456 (the reference's own loop-scope `i` shadowing its function-scope `i`, network.c:1239 vs :1298).  Mechanical diff vs network.c:1237-1341 = IDENTICAL (whitespace-normalized, deferred text restored).
- `wire/ObBuffs.cpp` ob_main: the DEFERRED `sendOutbound(id, a->out)` hand-off RESTORED verbatim — the P1 TU is now reference-complete.
- Still dormant end-to-end: no create_obbuffs caller until P3/P4; the semaphore quartet is created by P4's StartAudio equivalent before the pump can deliver, the same ordering the reference relies on.  Clean build, zero warnings.

### P3 SHIPPED (same session; survived + re-verified after an operator power outage mid-arc) — netInterface registrations + obbuffs ring lifecycle
- **create_rnet moved to once-per-process** at the main.cpp QTimer block AFTER create_xmtr — the reference's C#-init ordering (create_cmaster -> create_xmtr -> create_rnet -> StartAudio).  The old per-open call re-allocated prn on EVERY stop/start (leak + wire-state reset, contradicting the close() "prn stays alive for re-open" contract — a latent pre-existing defect the P3 reference-read surfaced).  open() now asserts the reference prn-non-null contract (netInterface.c:40); if wdsp.dll failed to load, open() refuses with a qCritical (no WDSP = no radio, the reference posture).
- **SendpOutboundTx(OutBound) restored at the reference site** (netInterface.c:1761, tail of create_rnet) — pcm->OutboundTx -> xmtr[0].pilv via the verbatim no-guard SetILVOutputPointer; ordering safe by construction.  The Stage C.3-fix "registration belongs at the xmtr-open site" note SUPERSEDED (it guarded a Step-14 stub against per-open clobber; under the direct port the reference site is correct).  RX side deliberately NOT registered — WdspEngine's openRx1 owns RX dispatch in the approved hybrid until P4 (registering RX here = the B.6.b clobber = dead RX audio).
- **UpdateRadioProtocolSampleSize ported verbatim** (netInterface.c:1836-1858, mechanical diff IDENTICAL with the documented kMax*/enum-class accommodations) — called per session-open from HL2Stream::open() at the StartAudio position (:45): per-protocol spp values (USB: mic 63 / rx 63 / tx 126 / audio 126) + create_obbuffs(0/1) — TWO new ob_main pump threads per session, both idle (ring 0 has no producer until P4's RX switchover; ring 1's producer is xilv in the stream-1 cm pump, quiescent until P4 feeds Inbound(1,...)).  destroy_obbuffs(0/1) at close() per StopAudio (:112-113).
- Power-outage recheck: all 4 edited files probe-verified intact, diff-vs-HEAD = exactly the P3 hunks, forced rebuild of the touched TUs clean (sole warning = the pre-existing hl2_stream C4456), mechanical diffs re-PASSED post-outage.
- [DONE] **OPERATOR HL2 BENCH PASSED (same day)**: "rx fine" — RX working on the P3 lifecycle (once-per-process create_rnet, per-session obbuffs rings + 2 idle pump threads, TX registration live).  **P3 gate cleared; P4 Wire-LIVE unblocked.**

### ⚠ Operator-flagged near-miss + bench-discipline correction (2026-06-12)
- Operator accidentally pressed TUN a few sessions back and got REAL RF OUT, despite the rebuild-arc bench notes repeatedly saying "TX stays wire-quiescent / RX-only."  Code-verified: those statements were true of the NEW direct-port chain only.  The OLD hardware-validated TX-0c path — TUN DC-injection at the EP2 packer (hl2_stream.cpp:~2516) + the requestMox FSM + ATT-on-TX — was NEVER part of the TX rip (only the SSB voice DSP subsystem was removed) and has been LIVE on tx-rebuild the whole time.  RF required the operator's own persisted PA-enable opt-in + drive settings (the deliberate-arm gate worked as designed; it was armed from the first-RF benches and persisted).
- **Operator decision: LEAVE IT** — it is the proven path, gated behind the PA-enable opt-in; P4 replaces it under its own full bench.  (Disarm anytime: un-tick Enable PA in Settings.)
- **Discipline correction going forward:** every direct-port bench gate now includes an explicit "TX state unchanged" line (TUN/MOX behavior + PA-enable posture), and "RX-only / wire-quiescent" is reserved for statements about the WHOLE wire, not just the new chain.  Applies to the P4 gate and onward.

### P2 COMPLETE.  NEXT
1. **P3** — netInterface outbound registrations: `SendpOutboundRx(OutBound)` / `SendpOutboundTx(OutBound)` per netInterface.c:1749-1761 — MUST register AFTER create_xmtr (verbatim setters have NO null guards); plus the create_obbuffs(0/1) sites (netInterface.c:1856-1857) at the Lyra equivalent of UpdateRadioProtocolSampleSize.
2. **P4** — Wire-LIVE switchover (one commit, full HL2 bench gate): verbatim sendProtocol1Samples thread + the semaphore-quartet creation + WriteMainLoop_HL2 hand-off; retires the OutboundRing/Ep2SendThread cv translations.

---

## 2026-06-12 (Friday PM — P1 obbuffs.c verbatim direct port SHIPPED)

**Branch:** `tx-rebuild`, on top of `976062d` (the P0.d bench-PASS doc flip).

### Shipped (one commit)
- **NEW `src/wire/ObBuffs.{h,cpp}`** — reference obbuffs.{h,c} verbatim: `obb,*OBB` twin typedef + numRings/obMAXSIZE/OBB_MULT defines + the file-scope `_obpointers obp` four-alias bank + create/destroy/flush/OutBound/obdata/ob_main/SetOBRingOutsize.  The TX-OUT seam (WDSP output → ring → ob_main pump thread → sendOutbound → protocol packers); SEPARATE TU from cmbuffs (inbound-only).
- The reference's own **2014-era idioms kept as shipped** (deliberately NOT harmonized to the cmbuffs sibling): calloc/free (not malloc0/_aligned_free), destroy-time `obp.pcbuff[0]==NULL` guard, obdata WITHOUT the MW0LGE CS wrap, the csOUT enter/leave pair at the top of the ob_main loop, the `out` work buffer inside the struct.
- **Sole deferred line:** `sendOutbound(id, a->out);` in ob_main — the reference defines it at network.c:1237 (the P1 branch :1285-1340 is the outIQbufp/outLRbufp + hsendIQSem/hsendLRSem/hobbuffsRun handshake WriteMainLoop_HL2 consumes) = **P2 scope**.  Carried in place as reference text with a DEFERRED tag; decl in ObBuffs.h verbatim.
- Provenance check resolved before code: the "OutBound at network.c:1285-1340" citation in older RadioNet.cpp comments is the PROTOCOL_1 **branch of sendOutbound**, not a second OutBound — obbuffs.c's OutBound is the one registered via SendpOutboundRx/Tx (P3).
- Compiler accommodations: same set as CmBuffs.cpp (suppress 4312/4189/4311/4302 on the thread-arg casts + `(AVRT_PRIORITY) 2`), each marked inline.

### Verification
- Clean build, ZERO warnings.
- Mechanical diff vs reference: **ObBuffs.cpp IDENTICAL** (code lines, whitespace-normalized); ObBuffs.h sole delta = the reference's include-guard `#define _obbuffs_h`, replaced by `#pragma once` (documented packaging difference, same as CmBuffs.h).  Live-line-subset PASS on both.

### Wire impact
- **NONE — the TU is dormant.**  No Lyra caller of create_obbuffs exists until P3/P4 (the reference creates rings 0/1 in netInterface.c:1856-1857).  No new threads at startup, wire bytes unchanged.  An operator RX smoke at convenience is prudent (binary changed) but this touches no live path.

### NEXT
1. **P2 = sendOutbound / sendProtocol1Samples fidelity audit** of the dormant wire layer (reconcile network.c:1237-1341 + networkproto1.c sendProtocol1Samples vs Lyra's Step-14-era OutboundRing/MetisFrame/Ep2SendThread surface), then restore the ob_main hand-off verbatim.
2. P3 netInterface registration (SendpOutboundRx/Tx(OutBound) — AFTER create_xmtr, the verbatim setters have NO null guards) → P4 Wire-LIVE (one commit, full HL2 bench gate).

---

## 2026-06-12 (Friday — P0.d CmBuffs/CMaster/cmsetup verbatim direct port SHIPPED + ✅ HL2 RX-regression bench PASSED)

**Branch:** `tx-rebuild` HEAD = `afc7950`, pushed to `origin/tx-rebuild`.

**✅ OPERATOR HL2 BENCH (same day): RX-regression gate PASSED** ("RX still working") — clean RX on the new per-stream cmbuffs pump/ring layout (two cm_main threads, stream 0 idle by design).  P0.d is fully closed; **P1 (obbuffs.c verbatim port) is UNBLOCKED** and starts next session.

### Shipped (one commit, `afc7950`)
- **NEW `src/wire/cmsetup.{h,cpp}`** — reference cmsetup.{h,c} verbatim: cmMAX* sizing macros (16/4/4/2/32), rxid/txid/sp0id/stype/chid/inid/mixinid/getbuffsize, SetRadioStructure + set_cmdefault_rates.  CreateRadio/DestroyRadio carried with the unported pipe/sync calls commented.
- **`src/wire/CmBuffs.{h,cpp}` rewritten verbatim** — `cmb,*CMB` twin typedef, `#define CMB_MULT (3)`, malloc0/_aligned_free, no calloc/intptr_t/guard deviations; pcm->in[] allocation moved back to create_cmaster (reference shape).
- **`src/wire/CMaster.{h,cpp}` rewritten verbatim** — FULL `_cmaster` struct (cmaster.h:39-99, PS surface incl. out[3]/panalalloc/pgain/peer), `cmaster,*CMASTER`, raw TCI fn ptrs, `enum AudioCODEC`, `cm = {0}`.  **TxChannel RAII carve-out DELETED** (src/wdsp/TxChannel.{h,cpp} retired): verbatim no-arg create_xmtr opens the WDSP TXA channel (chid(1,0)=1) + TX analyzer (disp 1; pan analyzer = disp 0, no collision) + out[0..2] + create_ilv itself through the wdspcalls seam.  create_cmaster/destroy_cmaster verbatim per-stream loops (update[] CS + cmbuffs + in[] for all cmSTREAM streams; create_rcvr = deferred stub, RX hybrid).  xcmaster verbatim: update[] critical section restored, real stype/txid/chid, TCI-override memset restored; fexchange0 + monitor-mix xMixAudio (accept-gated, quiescent vs the 1-input WdspEngine mixer) + xilv live.  SetXcmInrate/SetCMAudioOutrate/SetRcvr-/SetXmtrChannelOutrate/SetRunPanadapter/SetAntiVOXSource* ported.  All deferred subsystem lines carried IN PLACE as reference text with DEFERRED tags.
- **`src/wire/cmcomm.h`** — opaque verbatim twin typedefs ANB/NOB/EER/VOX/TXGAIN/ANALYZERS (tags match reference headers; completing a type later is source-compatible) + the umbrella-mapping note (.cpp files include explicit family headers — an include-list umbrella would be circular under #pragma once because the family headers include cmcomm.h for the base surface).
- **main.cpp** — SetRadioStructure(2,1,1,1,0,…)/set_cmdefault_rates(48 k) config block BEFORE create_cmaster (derived ids match the live layout: chid(0,0)=0 = WdspEngine RX1 channel, chid(1,0)=1 = TX channel); create_xmtr() invoked in the QTimer block after resolve_wdsp_calls (the documented DEFERRED-CALLSITE accommodation); handler-1.5 = gated destroy_xmtr().  Consumer-side decls for the headerless PORT functions (test_ilv precedent).  RadioNet.cpp → enum AudioCODEC cases; scratch/test_ilv.cpp → verbatim `cmaster cm = {0};` globals.

### Verification
- Clean build, ZERO warnings (C4701 in getChannelOutputRate disabled function-wide with a documented verbatim-text-wins pragma — line-level suppress can't reach the code-gen-stage warning).
- scratch/test_ilv.exe ALL PASS against the new verbatim globals.
- Mechanical diff vs reference: CmBuffs.{h,cpp} / cmsetup.h / CMaster.h struct+decls+enum / all 10 fully-verbatim cmaster.c functions IDENTICAL (whitespace-normalized); cmsetup.cpp comment-only deltas; live-line-subset check PASS for the partially-deferred bodies.  Sole code accommodation: `(char *)""` at the XCreateAnalyzer call (C++ string-literal constness).

### Behavior changes at startup (for the bench)
- TWO cm_main pump threads now start (streams 0 + 1; stream 0 idles forever — no Inbound producer; the reference layout).  Stream rings sized from cmMAXInRate=384000 (in[] = 512 complex).
- TX stays wire-quiescent: no Inbound() producer yet, pcm->OutboundTx null until P3.
- destroy ordering: handler-1.5 destroy_xmtr() (gated) → handler-4 destroy_cmaster() = reference relative order.

### NEXT
1. ~~**Operator HL2 RX-regression bench** (the P0.d gate)~~ — ✅ **PASSED 2026-06-12** (RX working; gate cleared).
2. **P1 = obbuffs.c port** (TX-out seam, separate TU from cmbuffs) → P2 sendOutbound audit → P3 netInterface registration (outbounds AFTER create_xmtr) → P4 Wire-LIVE (one commit, HL2 bench gate).

---

## 2026-06-08 (Monday EOD — STAGE B aamix.c port COMPLETE + bench-validated)

**Branch:** `tx-rebuild` HEAD = `533b06b`. Pushed to `origin/tx-rebuild` (0/0 in sync). `origin/main` deliberately untouched (different arc). **Backup:** `_backups/lyra-cpp-2026-06-08-aamix-stage-B-COMPLETE.bundle` (16 MB, `git bundle --all`).

### Today's shipped commits (in order, all on `tx-rebuild`)
```
533b06b [aamix port -- Stage B.6.b-final]   strip diagnostic instrumentation
8b8e0da [aamix port -- Stage B.6.b-fix1]    remove Stage-A NO-OP SendpOutboundRx stub (THE FIX)
12369bc [aamix port -- Stage B.6.b-debug-sink] dispatchAudioFrame sink-side counters
225518c [aamix port -- Stage B.6.b-debug]   9-counter chain instrumentation
53d9e5a [aamix port -- Stage B.6.b-retry]   wire AAMix via reference-faithful path
6e773a5 Revert "[aamix port -- Stage B.6.b] wire AAMix(1-input passthrough) into RX path"
525c2f0 [aamix port -- Stage B.6.b]         wire AAMix(1-input passthrough) -- REVERTED at bench
27ac2fa [aamix port -- Stage B.6.a]         extract feedIq audio tail into dispatchAudioFrame
```

### Arc summary
- **B.0 → B.5:** AAMix.h/cpp port complete from earlier sessions (538-line header verbatim from `aamix.c` with GPL v3+ attribution to NR0V/WDSP, 1100+ lines of cpp; idiom translations from Win32 to C++23: `CRITICAL_SECTION→std::mutex`, `HANDLE semaphore→std::counting_semaphore`, `_beginthread→std::jthread`, `_Interlocked*→std::atomic` with fetch_or/fetch_and, `malloc0→std::vector` RAII).
- **B.6.a (`27ac2fa`):** Refactored `feedIq` audio tail (lines 2402-2456) into new `WdspEngine::dispatchAudioFrame(audio, nframes)` helper. Byte-identical relocation; bench-confirmed.
- **B.6.b first attempt (`525c2f0`):** Wired `aaMix_` into RX path with `create_aamix(active=0x01L)` shortcut. **Operator bench: NO audio.** Reverted (`6e773a5`).
- **B.6.b-retry (`53d9e5a`):** Reference-faithful per `cmaster.c:297-313`: `create_aamix(active=0L, ring_size=4096, slew=0/10/0/10ms)` → `SetAAudioMixOutputPointer(aaMix_, 0, outbound)` → `SetAAudioMixState(aaMix_, 0, 0, 1)` (activate via close_mixer/open_mixer slew atom). **Operator bench: STILL NO audio.**
- **B.6.b-debug (`225518c`):** Chain-side instrumentation — 9 atomic counters + 1 ENTRY beacon + producer/consumer 1-Hz log emits in `xMixAudio` / `mix_main`. **Operator bench result:** chain healthy end-to-end (`xmix=rel=wake=out=188/sec`, `nzOut`=99%, `mixmain_started=1`, `Outbound=set`) but operator confirmed STILL no audio. ⇒ defect is downstream of Outbound.
- **B.6.b-debug-sink (`12369bc`):** Dispatch-side instrumentation — 6 atomic counters in `dispatchAudioFrame` + 1-Hz log line tag `[disp-dbg]`. **Operator bench result:** `[disp-dbg]` lines NEVER fired in the entire 33-second capture. ⇒ `dispatchAudioFrame` is never entered. ⇒ the Outbound callable AAMix is calling is NOT the lambda WdspEngine wired.
- **B.6.b-fix1 (`8b8e0da`, THE FIX):** Root cause traced from bench timestamps:
  - `19:22:02.972` — `WdspEngine::openRx1` calls `create_aamix(id=0, ..., Outbound=dispatchAudioFrame_lambda)` ⇒ `paamix[0]->Outbound = real lambda` ✓
  - `19:22:02.973` — `HL2Stream::open() → create_rnet() → SendpOutboundRx(STUB)` ⇒ `pcm->OutboundRx = STUB`, then `SetAAudioMixOutputPointer(nullptr, 0, STUB)` resolves `nullptr → paamix[0]` ⇒ **`aaMix_->Outbound = STUB`, real lambda CLOBBERED** ✗
  - The Stage-A NO-OP stub at `RadioNet.cpp:272-283` was a placeholder ("Stage B aamix port wires it via SetAAudioMixOutputPointer") for the wire-up Stage B was supposed to replace. Stage B.6.b correctly wired `dispatchAudioFrame` in `openRx1` — but left the Stage-A stub registration in place, which then overwrote the wiring 1 ms later when `HL2Stream::open()` called `create_rnet()`.
  - **Fix:** delete the Stage-A stub registration. Comment block on the deleted case warns future hands that the wire-up needs the per-channel sink-routing context (`hl2Out_`, `hl2AudioPush_`, `audioRing_`) that the global `pcm` cannot reach — so the wire-up must live at `openRx1`, not `create_rnet`.
- **B.6.b-final (`533b06b`):** Strip diagnostic scaffolding. -134 lines. Brief comment in `AAMix.cpp` points future hands at commit history (`git show 225518c` for chain instrument, `git show 12369bc` for dispatch instrument).

### Operator HL2+ bench (final, 19:27:49 → 19:28:47 — 58-second run)
- HL2 jack startup: first `[disp-dbg]` post-unmute: `calls=188 muted=0 gain=0.0845 peak16=32767 hl2(push/null)=188/0 hl2pushSet=1 audioRingSet=0` — full-scale audio reaching AK4951.
- Operator-confirmed audible RX ("Yes we have audio now").
- Mute toggle via dispatch (muted counter 0→22→133→287).
- Output device switched to PC Soundcard at 19:28:17 → `hl2Out=0, audioRingSet=1, pc(push) 176→928/sec`.
- Switched back to HL2 jack at 19:28:23 → `hl2Out=1`, `hl2(push)` resumed climbing.
- AGC mode changes (med/slow/off/fast) clean.
- Chain `out=N` tracks 1:1 with dispatch `calls=N` across both sinks across the entire run — no dropped frames, no thread starvation.

### Tasks closed
- #130 [completed] Stage B.6 [DEFER] — migrate RX audio path to ported AAMix
- #132 [completed] Stage B.6.b — wire AAMix(1-input passthrough) into RX path
- Stages B.0–B.5 + B.6.a already closed in earlier sessions.

### Methodology lessons recorded (for future Stage X work)
- 2-stage instrumentation (chain + dispatch) localized the defect to a single line of code on the SECOND bench. No speculation cycle, no convergence theatre.
- Operator-empirical rule held: after first B.6.b failure I was tempted to revert the AAMix port itself; the chain counters proved AAMix was correct in isolation, redirecting the dig to the wire-up surface where the bug actually was.
- Hard rule for next port: counters first when the symptom is "nothing happens". Instrumentation cost is trivial vs the alternative (multi-round revert/speculate cycle).
- "Do as Thetis does, Lyra-Native style" continues to hold — the first B.6.b attempt's `active=0x01` shortcut cost a revert cycle; the reference's `active=0 then SetAAudioMixState(activate)` pattern worked first time on the retry.

### Memory + doc updates
- `MEMORY.md` index line for lyra-cpp-tx rewritten with "READ FIRST 2026-06-09 AM" header + branch/HEAD/backup/root-cause pointer.
- `project_lyra_cpp_tx.md` — new "▶ READ FIRST 2026-06-09 AM resume pointer" block at top, plus full "State (2026-06-08 EOD)" section. Earlier state preserved below.
- `EXECUTION_PLAN.md` — new "PROGRESS TRACKING — STAGE B (aamix.c port) [SIDE-TRACK]" section with full B.0 → B.6.b-final checklist; new row in VERIFICATION LOG; footer "Status as of 2026-06-08 EOD" updated.
- This SESSION_LOG entry.
- PDF/DOCX regenerated via `tools/sync_execution_plan.py`.

### Next session pointer
- **First action:** `git fetch origin` THEN `git status` + `git log --oneline -5 tx-rebuild origin/main` to surface divergence.
- **Next port target = OPERATOR'S CALL.** Stage B side-track done; candidates:
  - **Stage C:** `ilv.c` port (TX I/Q interleaver — pairs with the `SendpOutboundTx` stub already wired in `CMaster.cpp` Stage A)
  - **Stage D:** `xcmaster` pump body port
  - Or pivot back to Step 14 / Phase 3 wire-layer rebuild on `main` (previous-arc resume was TX-1 Component 7 — first end-to-end SSB voice TX)

### Wire status
RX audio flows live through the ported AAMix dispatcher on the `tx-rebuild` branch's `HL2Stream::open()` path. TX path unchanged from earlier `tx-rebuild` state.

---

## 2026-06-06 (Saturday LATE-AFTERNOON — §1-C FULLY COMPLETE + RE-AUDIT CLEAN)

**Branch:** `tx-rebuild` HEAD = `169f1c2`.  Build clean,
`lyra.exe` relinked at 11:10:09.  **16 commits shipped today**
+ TWO full multi-agent audit rounds.  §1-C arc COMPLETE
through Stage 4F.2 (FrameComposer dissolution).  Final
re-audit returned 37/37 PASS across 3 sections, zero remaining
§6-Q3/Q5-class candidates, zero forbidden tokens in src/wire/.

### What's reference-faithful now
- TX wire layer: 100% reference-faithful (all `_radionet`
  fields in RadioNet; all reference file-scope globals as
  TU-scope statics in correct .cpp; Router + OutboundRing
  + FrameComposer all dissolved into free functions matching
  reference; symmetric socket-binding via metis_wire_bind()
  in both Ep6RecvThread + Ep2SendThread).
- §1.1 verdict: 🔴 → ✅ PARITY (Stage 4E doc consolidation).

### What's NOT yet reference-faithful (tracked, NOT in §1-C)
- **Task #73** — pre-existing forbidden-token violations
  (Thetis / Console.cs / OpenHPSDR / PowerSDR) in 5 non-wire
  files: tci_server.{h,cpp}, mainwindow.cpp,
  hl2_stream.{h,cpp}, wdsp_native.h.  Doc-style cleanup,
  ~1 hour, no impact on shipped wire-layer paths.
- **Wire-INERT** — nothing in HL2Stream::open() calls the
  new wire-layer free functions yet (step 14 wire-up).
- **Task #114 deferrals** — EER mode, non-HL2
  WriteMainLoop_generic, PeakFwdPower/PeakRevPower helpers.
  Hardware-blocker (no ANAN hardware to bench-test).

### Step 14 scope (NOT trivial)
HL2Stream is 4203 lines total with its own monolithic
`rxWorkerLoop` + `txWorkerLoop` member methods at
`hl2_stream.cpp:921` + `:2437` (the OLD wire path).  Step 14
must REPLACE these (per Rule 7 "Delete TX, don't refactor")
with calls into the new wire-layer free functions:
1. `prn` global pointer assigned at session-open
2. `metis_wire_bind(socket, dest, dest_len)`
3. `outbound_init()` to size prn->outLRbufp/outIQbufp
4. `ForceCandC::prime(3, tx_freq, rx_freq)` synchronously
   per §7 temporal-separation contract
5. `Ep6RecvThread::start(socket_fd)` → spins RX thread
   reading from prn-> + WSAEventSelect
6. `Ep2SendThread::start(socket, dest, dest_len)` → spins
   TX thread; no more composer / ring params (Stage 4F.2)
7. Migrate HL2Stream's existing telemetry/mic/IQ callbacks
   to the new sink-based registration (Router::register_sink
   for IQ; Ep6RecvThread::set_*_sink for telemetry/mic/I2C)

**Plan-first commit (R6).**  Bench-critical (first time the
new wire layer actually emits datagrams to the radio).
Multi-hour, possibly multi-commit.  Wrong wire-up = broken
RX/TX on real HL2 hardware.

### Resume after step 14
Operator-bench verification of RX-side audio + telemetry on
real HL2+ hardware.  Then TX-side bench (CW/SSB + Palstar
power readings).  Then Task #73 cleanup (small, doc-only,
can happen anytime).

### Wire status
Wire-INERT.  All wire-layer components reference-faithful
but not connected to runtime.

---

## 2026-06-06 (Saturday AFTERNOON — §1.1 REVERT COMPLETE — historical)

**Branch:** `tx-rebuild` HEAD = `79fe4ec`.  Build clean,
`lyra.exe` relinked at 10:45:10.  **TWELVE commits shipped
today** + 6-agent comprehensive TX audit done.  ALL stages
of the §1-C sweep complete in code (§1.1 networking-
infrastructure exclusion fully reverted).  Only Stage 4E
(PARITY_CHECKPOINTS doc consolidation) remains before step 14
wire-up.

### Today's commits (newest first)
- `79fe4ec` §1-C Stage 4D — OutboundRing dissolves to free functions (§1.1 revert COMPLETE)
- `1d73bea` §1-C Stage 4C — Ep2SendThread reads from prn->OutBufp + g_fpga_write_bufp
- `f1031da` §1-C Stage 4B.1 — correct FPGAReadBufp scope (file-scope, not _radionet)
- `26acea3` §1-C Stage 4B — Ep6RecvThread reads from prn-> + WSAEventSelect
- `e157301` §1-C Stage 4A — add networking-infrastructure fields to RadioNet
- `d88ddd6` §1-C Stage 3 — OutboundRing wait-all via condition_variable
- `2b78d9e` §1-C Stage 2A — Router class → struct + free functions
- `0e2a375` §1-C Stage 1 — small reverts (wb_enable, push timeout, diag counters)
- `50b713b` §6-B nit — drop metis_write_frame null-guard
- `3bfd248` §6-B + §7 — TU-scope MetisFrame + ForceCandC populate

### §1.1 revert summary
All `_radionet` fields the original §1.1 exclusion (signed 🔴
2026-06-04) had punted to wire-layer class members now live in
RadioNet as members exactly per reference:
- Buffer pointers: `RxBuff`, `TxReadBufp`, `outLRbufp`,
  `outIQbufp`, `OutBufp` (+ `ReadBufp` declared P2-future-use)
- RX seq counters: `cc_seq_no`, `cc_seq_err` (P2-only;
  declared in RadioNet but HL2-inert)
- Thread handles: `hReadThreadMain`, `hWriteThreadMain`,
  `hKeepAliveThread` (std::thread types)
- Init semaphores: `hReadThreadInitSem`, `hWriteThreadInitSem`
  (std::counting_semaphore<1>)
- Outbound sync (cv-collapsed mirror of 4 reference HANDLE
  pairs): `cv_outbound` + `mu_outbound` + 4 bool flags +
  `outbound_stop`
- WSA event + waitable timer (Win32-only): `hDataEvent`,
  `wsaProcessEvents`, `hTimer`, `liDueTime`
- Networking config ports: `p2_custom_port_base`,
  `base_outbound_port`

Reference-FILE-SCOPE globals (NOT in `_radionet`) live as
TU-scope statics in their respective wire-layer .cpp files
per the §6-B precedent:
- `wire/MetisFrame.cpp`: `g_metis_out_seq_num` (was Ep2SendThread::out_seq_num_)
- `wire/Ep6RecvThread.cpp`: `g_metis_last_recv_seq`, `g_seq_error`, `g_seq_seen`, `g_control_bytes_in[5]`, `g_fpga_read_bufp`
- `wire/Ep2SendThread.cpp`: `g_fpga_write_bufp`

Wire-layer classes dissolved or reduced:
- `Router`: class → plain struct + free functions (Stage 2A)
- `OutboundRing`: class → namespace-scope free functions
  operating on `prn->...` (Stage 4D)
- `Ep6RecvThread`: still a class (thread orchestrator), but
  all data moved to `prn->...` or TU-scope statics
- `Ep2SendThread`: same — thread orchestrator, data moved out

### Remaining — Stage 4E (doc-only)
Consolidate the §1-C entry in `docs/architecture/PARITY_CHECKPOINTS.md`:
- Single §1-C entry covering Stages 1+2A+3+4A-D + the
  earlier §6-B + §7
- §1.1 verdict amended from 🔴 OPERATOR-APPROVED DEVIATION
  to ✅ PARITY (now genuinely byte/structure-faithful)
- Sign-off block + commit trailer

### Resume after Stage 4E
Phase 2 step 14 — wire `outbound_init()` + `metis_wire_bind()`
+ `ForceCandC::prime` + `Ep6RecvThread::start` + `Ep2SendThread::start`
into `HL2Stream::session_open()` in the correct temporal sequence.

### Wire status
Wire-INERT today.  All wire-layer components built but nothing
in `HL2Stream::session_open` calls them yet.

---

## 2026-06-06 (Saturday MID-DAY — STAGE 3 CHECKPOINT — historical)

**Branch:** `tx-rebuild` HEAD = `d88ddd6`.  Build clean, `lyra.exe`
relinked at 09:54:09.  Five commits shipped today + 6-agent
comprehensive TX audit done.  Stages 1+2A+3 of the §1-C sweep
DONE; Stages 2B+4 remain (bundled — §5.10 WSAEventSelect lives
in `_radionet` fields per reference, so it lands with §1.1
revert).

### Today's commits (newest first)
- `d88ddd6` §1-C Stage 3 — OutboundRing wait-all via condition_variable
- `2b78d9e` §1-C Stage 2A — Router class → struct + free functions
- `0e2a375` §1-C Stage 1 — small reverts (wb_enable, push timeout, diag counters)
- `50b713b` §6-B nit — drop metis_write_frame null-guard
- `3bfd248` §6-B + §7 — TU-scope MetisFrame + ForceCandC populate

### What got fixed to match reference (Stages 1+2A+3)
- §1.3: `std::atomic<long> wb_enable` → `volatile long` (matches `network.h:160`)
- §6.10: OutboundRing producer push — drop bounded 5s try_acquire_for + push_timeouts_*; unbounded `acquire()` mirroring reference Inbound/obbuffs
- §6.12: Ep2SendThread `datagrams_sent_`/`send_errors_` diagnostic counters dropped (no reference counterpart)
- §5.9: `class Router` → plain struct + free functions (`xrouter`/`register_sink`/`set_control_word`/`set_call_count`/`router_instance`) + file-scope `g_routers[]` array, mirroring reference `router.c` structure verbatim
- §6.9/6.11/6.14: OutboundRing wait-all via `std::condition_variable` + `bool` flags + single mutex (was 4 binary_semaphores + polling) — mirrors reference `WaitForMultipleObjects(2, hsendEventHandles, TRUE, INFINITE)` atomic wait-all, no polling, fully interruptible

### Remaining — Stage 4 (THE BIG ONE)
**§1.1 networking-infrastructure revert + §5.10 WSAEventSelect bundled.**

§1.1 (signed 🔴 2026-06-04) excludes from RadioNet a list of
fields that the reference puts INSIDE `_radionet`.  Under
strict "do as reference, period" §1.1 itself is a §6-Q3/Q5-
class candidate.  Stage 4 puts these fields back into RadioNet:
- Buffer pointers: `RxBuff`, `TxReadBufp`, `ReadBufp`, `OutBufp`, `outLRbufp`, `outIQbufp`
- Thread/sem/event handles: `hReadThreadMain`, `hsendEventHandles[2]`, `hobbuffsRun[2]`, etc.
- Waitable timer + WSA event: `liDueTime`, `hDataEvent`, `wsaProcessEvents`
- RX seq counters: `cc_seq_no`, `cc_seq_err`
- Networking ports: `p2_custom_port_base`, `base_outbound_port`

Then Ep6RecvThread + Ep2SendThread + OutboundRing read from
`prn->...` instead of their own members.  §5.10 (recv() →
WSAEventSelect) folds in because `hDataEvent` +
`wsaProcessEvents` are `_radionet` fields.

**Scope: multi-hour, multi-commit.**  Planned sub-stages:
- 4A: Add fields to RadioNet (additive, no behavior change)
- 4B: Ep6RecvThread refactor + WSAEventSelect mechanism
- 4C: Ep2SendThread refactor (reads buffers from RadioNet)
- 4D: OutboundRing — buffers + sync primitives move into
  RadioNet; class may dissolve into free functions
- 4E: PARITY_CHECKPOINTS §1-C entry finalized + §1.1 amendment

### Resume after Stage 4 completes
Phase 2 step 14 — wire `ForceCandC::prime` + `metis_wire_bind`
+ WSAEventSelect setup into `HL2Stream::session_open()` in the
correct temporal sequence.

### Wire status
Wire-INERT today.  All wire-layer components built but nothing
in `HL2Stream::session_open` calls them yet.

---

## 2026-06-06 (Saturday MID-DAY — AUDIT PAUSE BOOKMARK — historical)

**Branch:** `tx-rebuild` HEAD = `50b713b`.  Build clean, `lyra.exe`
relinked at 09:27:22.  All §6-B + §7 work shipped + audited
20/20 PASS this morning.

### Done this morning (2 commits, ALL like reference)
- `3bfd248` §6-B parity correction sweep + §7 ForceCandC populate
  - Hoisted `metis_write_frame` to `wire/MetisFrame.{h,cpp}` TU-scope
  - TU-scope `g_metis_out_seq_num` (was `Ep2SendThread::out_seq_num_`)
  - TU-scope socket/dest globals (were Ep2SendThread members)
  - DROPPED `Ep2SendThread::send_lock_` mutex (no reference counterpart)
  - §1.1 row split: RX seq stays / TX seq moves
  - `ForceCandC::prime` + `prime_pass` byte-for-byte from `networkproto1.c:106-139`
- `50b713b` §6-B nit — dropped `metis_write_frame` null-guard (no reference
  counterpart; audit-caught NOTE-D)
- Two parallel audits run: parity (20/10 PASS) + rule-compliance (10/10 PASS)

### Pause reason
Operator-requested **comprehensive TX audit** of ALL work since
the rebuild started (30 commits since `0348238` Phase 0 mapping
doc 2026-06-04).  Sections to audit against the reference:
- §1 RadioNet (`2171fb9`/`dcbf9d1`)
- §2 RbpFilter / RbpFilter2 (`228ffd8`)
- §3 HPSDR family + dispatch globals (`f3ccc51`/`55ae658`/`782e65d`)
- §4a/§4b-1/§4b-2/§4c FrameComposer 19-case dispatch
  (`93e1511`/`5fc4192`/`4446916`/`f06a796`)
- §5 / §5-A Ep6RecvThread + Router (`c8baa63`/`a6be425`/`b988177`/`6bbf449`)
- §6 / §6-A Ep2SendThread + OutboundRing (`4c580a1`/`89ab298`)
- §6-B + §7 already audited this morning (skipping re-audit)

### Resume point after audit
Resume here: **Phase 2 step 14 — wire-up `ForceCandC::prime` +
`metis_wire_bind` into `HL2Stream::session_open()`** in the
correct temporal sequence (bind → prime → Ep6 start → Ep2 start).

### Wire status
Wire-INERT today.  Nothing in `HL2Stream::session_open` calls
the new wire-layer components yet.  Step 14 (next commit after
audit closes) flips the wire on.

---

## 2026-06-05 (Friday EOD)

**Project:** lyra-cpp TX Wire-Layer Rebuild (Task #112) — branch `tx-rebuild`

### Done today
- **§5 EP6 Receive Path** — `Ep6RecvThread` + `Router` (`xrouter`/`twist`) populated (`c8baa63`)
- **§5-A 9-fix parity correction** (`a6be425`) including 🔴 CRITICAL RX-audio bug (IQ unpack scale 48 dB too quiet — caught before any consumer wired)
- **§5-A doc completeness** (`b988177`)
- **§6 EP2 Send Path** — `Ep2SendThread` + `OutboundRing` populated (`4c580a1`)
- **§6-A 6-fix parity correction** (`89ab298`) — MMCSS priority, per-family dispatch, CW bit-shift revert to verbatim, CW state per-sample reads, `binary_semaphore` UB guard, Rule 24 inline token
- 4 audit passes total (2 per §5 / §6) — all PASS after corrections

### Where we are
Branch `tx-rebuild` HEAD = `89ab298`. §5 + §6 of the wire-layer rebuild are **shipped + doubly-verified** (parity-audit + rule-audit + correction sweeps + re-audits, all clean). Wire-layer is still wire-inert — no Phase 1 code path instantiates the new components.

### Next up
**§7 — `ForceCandC` priming + ANY remaining wire-layer pieces.** Reference: `networkproto1.c::ForceCandCFrame` (lines 134-139) + `ForceCandCFrames` (lines 106-132). Small commit — just the 3-frame priming burst that runs at session start before the EP2 send loop begins.

After §7 completes the §1c-listed wire-layer skeletons, the next phase is **Phase 2 step 14 — wire-up**: instantiate Ep6RecvThread + Ep2SendThread + OutboundRing + Router + ForceCandC + FrameComposer into HL2Stream's session-open path.

### Pending / parked
- **Task #114** TX-policy plumbing — ATT-on-TX, panadapter offset, PA-enable safety, plus the §6 FIXMEs: `WriteMainLoop_generic` for non-HL2, EER mode (§6.4), `PeakFwdPower`/`PeakRevPower` helpers (§5 telemetry cases 0x08/0x10)
- **No hardware bench yet** — gateware-watchdog kill-test, real-antenna TX, foot-switch HW-PTT all parked until Phase 2 wire-up + verification
- **Don't push to GitHub** (standing rule)

### Commit summary (today)
```
89ab298 fix(wire): §6-A parity correction sweep — 6 fixes
4c580a1 feat(wire): populate §6 Ep2SendThread + OutboundRing
b988177 docs(parity): §5-A Lyra-native additions table — 2 audit-flagged rows
a6be425 fix(wire): §5-A parity correction sweep — 9 fixes
c8baa63 feat(wire): populate §5 Ep6RecvThread + Router
```

### Standing rules in effect
- Rule 24 "always verify against reference" — applied per commit, audit-verified
- 2026-06-05 directive "reference = make Lyra the same" — driving all Q-decisions
- §6 EER mode + non-HL2 dispatch + Peak power helpers all deferred to Task #114 with FIXMEs
- Operator hardware = HL2+/AK4951; no ANAN P1/P2 hardware available

---

*Session log started 2026-06-05.  Older entries (if any) below this line as they accumulate.*
