# P2 identity-layer reconciliation + wire-facts diff

Call-prep for the N8SDR ↔ KD4YAL P2 collaboration (2026-07-20). Two
parts: (1) fold Jerry's `HardwareCatalog`/`RadioProfile` into our
`RadioCapabilities`/`RigRegistry` (agreed: one system, ours); (2) diff
his proven `P2Session` wire facts against our stubbed `ETH` branches.

Both trees local: ours `Y:/Claude local/SDRProject/lyra-cpp`, his fork
`Y:/Claude local/SDRProject/lyra-cpp-p2saturn`.

---

## Part 1 — identity/capability layers

### The two stacks side by side

| Layer | Jerry (fork) | Ours (multi-rig arc) | Granularity |
|---|---|---|---|
| Model/capability | `HardwareCatalog` + `HardwareModelDescriptor` (data table, per **marketed model** "ANAN-G2") | `RadioCapabilities` + `capabilitiesFor(family)` (per **family** enum) | **his is finer** |
| Per-device identity | `RadioProfile` (per-MAC, JSON) | `RigProfile` + `RigRegistry` (per-MAC, QSettings) | equivalent |
| Station config | `StationProfile` (JSON: band mem, layout, audio, CAT) | `RigScope` (`rig/<rigId>/…` QSettings) | equivalent |
| Operating (TX/RX) | keep existing `ProfileManager` | keep existing `ProfileManager` | same — untouched |

### The core mismatch: family vs model

Our `RadioCapabilities` is **per-family** (5 enum values: Hl2 / AnanP1 /
AnanP2 / BrickP2 / Unknown) and **thin** — protocol, adcBits, LNA range,
audio path, PS-mod flag. His `HardwareCatalog` is **per-marketed-model**
and **rich** — per-band PA-gain tables, RX meter/display cal offsets,
volts/amps telemetry + conversion constants, ADC count, MkII BPF, LR
audio swap, P2 PS default peak, RX2 stepped atten. Transcribed 1:1 from
Thetis `clsHardwareSpecific.cs` (authoritative).

Thetis itself separates `comboRadioModel` (marketed model) from the
discovery board id — because several products share a gateware family
but need different PA/cal/mic/relay data. **His model granularity is the
correct one for the calibration data; our family enum is too coarse to
hold it.** So the reconciliation is *not* "his table folds into our
struct" — our capability layer has to grow a model tier.

### Recommended unified shape (one system, ours)

1. **Keep `RigRegistry`/`RigProfile` as the identity authority** (per-MAC,
   QSettings — consistent with the whole app + the backup engine).
2. **Add a model tier**: adopt his `HardwareCatalog` as the model-detail
   table (it's just data, transcribed from Thetis — the expensive part is
   already done and bench-anchored). Our `RadioFamily` stays as the coarse
   gateware/board bucket; the **model key** ("ANAN-G2") is the fine key.
3. **`RigProfile` gains a `hardwareModelKey` field** (his `RadioProfile`
   already has it; ours doesn't — this is the one real schema gap).
   Discovery seeds it from `defaultModelForBoard(board, protocol2)`;
   operator can override.
4. **`capabilitiesFor()` becomes model-aware** — either derived from the
   catalog row, or kept as the coarse family fallback when no model is
   set. Existing HL2 callers keep working (HL2 = family Hl2 = model
   "HERMES-LITE", metadata-only per his retention rule).

### Field map: his `RadioProfile` → our `RigProfile`

| Jerry `RadioProfile` | Our `RigProfile` | Action |
|---|---|---|
| `mac` | `mac` | same stable key (ours also derives `rigId = rig_<mac>`) |
| `nickname` | `label` | same role — rename on import |
| `lastKnownIp` | `lastIp` | same |
| `protocol` (1\|2) | (encoded in `family` + `RadioCapabilities.protocol`) | keep ours; drop his explicit field or keep as convenience |
| `hardwareModelKey` | **(missing)** | **ADD to RigProfile** — the schema gap |
| `trxAntenna` (1..3) | (missing) | P2-only → per-rig config (`RigScope`) |
| `audioRoute` ("hl2"/"pc"/"") | machine-local audio (design: per-PC) | reconcile: his is per-profile transient; ours is per-PC. Keep ours; his "seed P2 = pc" rule is sound |
| `firstSeen`/`lastSeen`/`schemaVersion` | (missing) | optional metadata — cheap to add |
| — | `rigId`, `family` | ours; his uses mac-as-key + model.expectedBoard |

**Storage divergence:** his = JSON docs (`profiles/radios/<uuid>.json`);
ours = QSettings (`rigs/<rigId>/`). Unify on **QSettings/RigRegistry**.
Worth adding a **JSON export/import** later for his Thetis-database
importer (maps `comboRadioModel`, PA gains, cal, antennas, atten, PS,
mic, OC, audio devices) — a genuinely useful onboarding feature, not
core.

**Reserved ordinals (do not reuse):** his catalog reserves upstream
Thetis `ANAN_G2E = model 16`, board `HermesC10 = 20`. Honor these if we
extend the enums.

---

## Part 2 — wire-facts diff (his proven vs our stub)

### Our side: essentially nothing proven

`src/wire/Network.cpp` carries **one** `ETH` branch — the P2 arm of the
`sendProtocol1Samples` loop (`network.c:1237-1341`) — as **commented
DEFERRED reference text**, tagged "Protocol 2 / ANAN — v0.4 scope." That
is the *entire* P2 footprint in our tree. No P2 discovery, no control
packets, no recv path, no phase-word, no port map. P2 is 0% implemented;
the stub is a placeholder marking where the send-loop branch will go.

### His side: full RX bring-up, bench-verified on a live G2

From `P2Session.h` (both ends of the wire — his radio-side p2app +
Thetis client — cross-referenced):

- **Discovery** — dual P1+P2 sweep.
- **Session/controller lease** — radio captures client addr from the
  general packet (`reply_addr`); HP `run=1` → active, `run=0` → release;
  ~1 s inactivity timeout (hardware watchdog).
- **4 control packets** — General (60 B → :1024), DDC-specific,
  DUC-specific, High-Priority (1444 B → :1027). Full byte offsets
  documented in the header.
- **HP status parse** — 60 B ← :1025, 200 ms cadence RX / 1 ms TX /
  immediate on PTT; PTT bits, ADC overflow, fwd/rev power, FIFO
  depths, ADC peaks (fw ≥ 27), supply volts, user I/O.
- **DDS phase-word encoding** — `freq_word = Hz × 2^32 / 122.88 MHz`
  (radio hardcodes phase-word decode). ⚠ the single most important
  P2-vs-P1 frequency fact.
- **Port map** — 1024 general (out), 1025 HP-status (in), 1026 mic (in),
  1027 HP (out), 1035+n DDC-n IQ (in). One socket; demux by the radio's
  **source** port.
- **Control seq ALWAYS ZERO** — only data streams increment; decoders
  must not dedupe control.
- **Front-end (Saturn)** — Alex TX/RX words in the HP packet; the CLIENT
  owns the RF front end. ⚠ **all-zero Alex words = radio connects but is
  DEAF** (bench trap 2026-07-19).
- **Bench safety** — RX-only by construction: transmit bit never set,
  drive 0, PA-enable 0, hardware-watchdog bit on (radio idles ~1 s after
  client death).

### RX-path compatibility (the good news)

His `P2RxBridge` feeds DDC0 IQ into **router 0 — the identical seam our
P1 EP6 path dispatches to** (`router_instance(0)` port 0 →
`WdspEngine::feedIq`), and keeps `HL2Stream` as the tuning/state
authority (bridge mirrors `rx1FreqChanged` → DDC0). P1/P2 are mutually
exclusive (single-feeder-thread contract). So **everything downstream —
WDSP, panadapter, S-meter, audio — is unchanged**, exactly as our
un-defer-`ETH` plan intended. The DSP/UI seam matches; only the wire
back-end differs.

### The one open architecture question for the call

His proven RX lives in a standalone `P2Session` (QThread + QUdpSocket).
Our wire layer is native WinSock + dedicated OS threads + the
ChannelMaster free-function port. **His own doc says the final should
complete the ChannelMaster `ETH` branches, not carry a parallel path** —
i.e. re-home his proven facts into `write_main_loop_p2` / the `ETH`
branches. Two routes:

- **(A) Adopt `P2Session`/`P2RxBridge` largely as-is** — fastest, it
  works today, RX on the bench now. Divergent from the wire layer's
  native-thread/ChannelMaster discipline.
- **(B) Port his proven facts into the `ETH` branches** — consistent
  with the wire layer + his stated end-goal; slower.

Recommendation to discuss: **(A) as an immediate bench-working RX for the
Brick, then converge to (B)** once the facts are locked and the harness
is green — using his byte-level regression harness as the safety net for
the port. The wire facts are identical either way; only the plumbing
moves.

---

## Suggested call agenda

1. Confirm unified identity shape: `RigRegistry` authority + adopt
   `HardwareCatalog` as the model tier + add `hardwareModelKey` to
   `RigProfile`.
2. Route decision: (A)-then-(B) vs straight-(B) for the P2 wire path.
3. Regression harness: how his Saturn-repo capture harness plugs into
   our client tests (the strict-p2app property as our correctness gate).
4. Board coverage: G2 (his) + Brick (Rick) — the Brick has no Saturn
   front end, so catalog needs a Brick/Hermes-class row (no Alex words).
5. Process: PR-with-review; where P2 lands (branch/upstream cadence).
