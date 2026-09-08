# BrickSDR2 — Protocol‑2 TX: Solo Build Plan

**Status:** plan / ready to execute · **Author:** Rick (N8SDR) with Claude · **Date:** 2026‑08‑15
**Context:** contingency if the PR #1 / RX‑gain coordination with Jerry (KD4YAL) stays silent past the 1‑week line. This is the path to **Brick TX on our own**, using assets already in hand.

---

## 0. The reframe — you are NOT starting from zero

The waiting made Brick TX feel blocked. It isn't. Three things are already done:

1. **The P2 TX engine already exists in our branch.** Jerry's `g2‑p2‑tx` (our Brick branch is based on it) ships a complete, modular, **fail‑closed** P2 transmit subsystem — `P2TxSafetyGate` (pure evaluator, fails closed on any unhealthy input), `P2TxPump` (192 kHz DUC‑IQ FIFO producer), `P2TxWriter` (paced port‑1029 writer), `P2TxPackets` (encoders), `P2Session::buildHighPriorityPacket(run)` with a golden‑packet test seam, and a two‑call control surface (`setTxOperatorArmed` / `setTransmitIntent`). It is GPL, published on his public fork, and physically in our tree.

2. **The Brick RX + model + capabilities are done and hardware‑confirmed** (commits `30b3b96` / `426ee1c` / `c9e1e91`): first‑class `BRICK‑SDR2` model row, capability flags (`hasAlex=false` / `hasApolloFilter=true` / `p2Tx=true`), the operator filter‑board picker, and RX proven on your real Brick2 (`00:1C:C0:A2:22:5C`). The Saturn‑Alex trap is provably avoided.

3. **The Brick TX wire is fully captured** (your bench, 2026‑08‑01, dummy load, every band 6M→160M). We know **byte‑for‑byte** what a Brick keydown looks like — and it is *simpler* than the G2.

So "solo Brick TX" = **the last leg**: point the existing TX engine at the Brick's (trivial, fixed) TX pattern, un‑grey the arm ourselves, wire the FSM, keep the fail‑closed gate. Not a Thetis port; not a from‑scratch P2 stack.

**The one decision this requires:** building on Jerry's `g2‑p2‑tx` means overriding his "don't build on my pre‑release TX yet" courtesy. It's GPL‑published and he's gone quiet — so it's your call, and this plan assumes you've made it (the 1‑week line). We keep his authorship intact and stay ready to reconcile if he re‑engages.

---

## 1. Assets in hand (what we build FROM, not build)

| Asset | Where | State |
|---|---|---|
| `P2TxSafetyGate` / `P2TxPump` / `P2TxWriter` / `P2TxPackets` | `src/wire/` (g2‑p2‑tx base) | Jerry's, complete, in our branch |
| `P2Session::buildHighPriorityPacket(run)` + `diagnosticHighPriorityPacket` (golden‑packet test seam) | `src/wire/P2Session.{h,cpp}` | complete |
| `setTxOperatorArmed(armed)` + `setTransmitIntent(on, paRequested, drive)` | `P2RxBridge` → `P2Session` | complete |
| `txHardwareSupported() = (p2ProfileForModel(modelKey) != nullptr)` | `P2RxBridge.cpp:683` / `P2Session.cpp:95‑100` | **gates the arm; today only ANAN‑G2** ← the one hook to open |
| `alexTxWord(hz, trxAnt)` front‑end word functor (per‑model) | `P2Session` profile | reuse pattern; Brick = a constant |
| Brick `BRICK‑SDR2` model row + capability flags + RX | main + our Brick commits | shipped, HW‑confirmed |
| **Brick TX wire capture** (fixed pattern, all bands) | `brick_p2_sniff*.log` / `brick_p2_hp_diff.log` | ground truth |

---

## 2. The Brick TX wire contract (captured ground truth)

Host→Brick P2 port map (captured): `1024/60`=General · `1026/60`=TX‑specific/DUC cfg · **`1027/1444`=High‑Priority** · `1028/260`=mic/TX‑audio · **`1029/1444`=DUC/TX‑IQ**.

**A Brick keydown asserts a FIXED, band‑independent pattern** (identical on 6M and 160M — nothing varies by band):

- **MOX/PTT** — High‑Priority header **byte 4, bit 1 (`0x02`)**: `0x01→0x03` on key.
- **OC output** — High‑Priority **~byte 1402 = `0x04`** on key (T/R relay / PA‑key line).
- **Alex/relay enable** — High‑Priority **~bytes 1428‑1433** → a constant `08 04 01 00 … 08 04` on key (NOT a per‑band ladder — a single fixed value).
- **TX I/Q** — port **1029** goes nonzero (the DUC stream).
- **General(1024)** — does **not** change on key (PA‑enable is already a static General bit at rest; Apollo is not a wire bit here).
- **DUC/TX phase (freq)** — High‑Priority **[329..332]** (already tracked in RX).
- **Drive** — High‑Priority **[345]** (per Jerry's P2Session layout).

**Decisive consequence:** the Brick needs **no per‑band TX filter math** — RX or TX. Its onboard LPF self‑selects in gateware from the tuned TX frequency it already has. So the Brick's TX front‑end word is a **compile‑time constant**, far simpler than the Saturn per‑band `alexTxWord`.

### 2.1 — Authoritative wire offsets (deskHPSDR + Thetis cross‑check, DONE 2026‑08‑15)

An Explore pass over **deskHPSDR** (`new_protocol.c`) and **ramdor‑Thetis** (`ChannelMaster/network.c` + `netInterface.c`) confirmed our capture and pinned every offset. **Both reference trees emit byte‑identical P2 TX packets**, and a Brick emits **byte‑identical packets to a plain Hermes** — deskHPSDR has **zero** Brick‑specific branches in its P2 TX builders (the only `hermes_mode`/Brick hits are RX DDC‑mirroring gated on `NEW_DEVICE_ANGELIA`, never TX). Implement the **generic Hermes P2 TX path** (device=Hermes, DDC base index 0); no Brick‑conditional TX code.

**High‑Priority (port 1027, 1444 B) — `new_protocol_high_priority()` `new_protocol.c:956‑1642`:**

| Field | Offset | Notes |
|---|---|---|
| seq (BE) | 0‑3 | |
| RUN / **MOX** | **byte 4** | bit0=RUN, **bit1 `0x02`=MOX/TX** (`:997`,`:1014`). Thetis `packetbuf[4]=ptt_out<<1\|run`. **Matches your capture exactly.** |
| CW/CWX | byte 5 | 0 for host‑streamed; cwx/dot/dash if gateware‑keyed |
| RX DDC phase words | 9 + n·4 | Hermes/Brick base ddc=0 → words at 9,13. phase=`freq·(2³²/122.88e6)` |
| **DUC/TX phase (freq)** | **329‑332** (BE) | matches Jerry's `[329..332]` |
| **TX drive** | **byte 345** | `drive_level & 0xFF`, 0 if out‑of‑band. matches Jerry's `[345]` |
| **OC output** | **byte 1401** | `OC<<1`. Your capture read `0x04` at ~1402 → reconcile to **1401** (0x04 = OC 2<<1) from the log |
| XVTR/audio‑amp | byte 1400 | |
| **Alex TX words** | **ALEX1 1428‑1431**, **ALEX0 1432‑1435** | TX‑antenna + TX‑LPF + TX_RELAY bits. Your Brick capture = a **fixed constant** here (no per‑band LPF — Apollo/onboard select) |
| step atten | ADC1 1442 / ADC0 1443 | forced 31 on TX‑with‑PA (the ATT‑on‑TX equivalent) |

**DUC‑IQ / TX samples (port 1029, 1444 B) — `new_protocol_txiq_thread()` `new_protocol.c:2195‑2269`:** 4‑byte BE seq header + **240 I/Q pairs × 6 B = 1440 B**; each pair = **24‑bit signed big‑endian, I then Q**; **800 packets/s, one per 1250 µs** at 192 kHz; producer‑paced by a FIFO‑fill model + semaphore (mic/DSP pushes 240 → `sem_post`). This is exactly what Jerry's `P2TxPump`(192 kHz FIFO)→`P2TxWriter`(port 1029) already implements.

**General (port 1024, 60 B) — `new_protocol_general()`:** **byte 58 bit0 = PA‑enable** (static; matches "PA‑enable is a static General bit at rest" from the capture); bit1 = Apollo tuner; byte 59 = Alex‑board enable (Alex only). No Brick branch.

**Brick knobs (plain config, NOT protocol forks):** `have_alex_att=0` (don't OR ALEX‑att bits) + `alex_attenuation=0`, and the TxSpecific **byte‑50** mic flags (`mic_ptt_enabled=1`, bias off). A generic Hermes user could set both identically.

**Sequencing (deskHPSDR `radio.c` `rxtx()`):** RX→TX = stop RX → `tx_on` (DUC‑IQ on 1029 starts flowing) → **then** the HP packet with MOX bit4/0x02 is scheduled — so **TX I/Q leads the MOX bit**, and the TX freq word is already in every HP packet (100 ms RX timer), so **freq precedes MOX by construction**. **No host TR‑sequencing delay** — the FPGA owns TR; the host only applies a **post‑TX RX‑mute window** (16 ms SSB / 31 ms AM‑FM‑TUNE / 0 CW). TX→RX clears MOX after `tx_off`. **Internal‑CW note:** deskHPSDR deliberately does **not** set MOX for internal CW (FPGA keys); Lyra's Brick CW path decision (host‑streamed I/Q like TUN vs a P2 CW‑key channel) is a build‑time pin — §7.

**Net:** every byte in §2 is now offset‑authoritative and matches your capture. The Brick TX profile = the generic‑Hermes P2 TX packet with `alex_att=0` and the captured constant TX‑word — which is precisely what Jerry's engine already builds; we only supply the constant + the model‑key recognition.

---

## 3. The build — staged, each stage RF‑inert until the final opt‑in key

Build in the Lyra‑P2 worktree (g2‑p2‑tx base — has both Jerry's TX engine and our Brick model work). Each stage is independently revertable. **Nothing emits RF until S‑T3 + operator opt‑in + a physical key.**

### S‑T0 — Un‑grey the Brick TX arm (RF‑inert)
The exact thing we asked Jerry to do — we do it ourselves. Make `p2ProfileForModel(modelKey)` return a **Brick `P2HardwareProfile`** for the `BRICK‑SDR2` key (today it returns non‑null only for `ANAN‑G2`/`ANAN‑G2‑1K`, `P2Session.cpp:95‑100`). That flips `txHardwareSupported_` true (`P2RxBridge.cpp:683`), which un‑greys "Arm P2 TX" (`settingsdialog.cpp:3448‑3451`). **Still no RF** — the arm only *enables the checkbox*; transmit needs the safety gate + intent + a key.

### S‑T1 — Brick TX front‑end word = the captured constant (RF‑inert)
The Brick `P2HardwareProfile` provides its TX front‑end word as the **fixed captured value** (OC `0x04`, Alex/relay `08 04 01 00 … 08 04`) — NOT `saturnAlexTxWord`. No per‑band function; a constant the HP encoder writes at [~1402]/[~1428‑1433] when transmitting. This is the whole of the Brick's "filter" TX story (Apollo/onboard‑select = `hasAlex=false`). Verify the exact bytes against the capture + the deskHPSDR cross‑check before it's on the wire.

### S‑T2 — Wire the FSM → transmit intent (the keying path)
Route the CW/MOX/TUN keydown/keyup through the existing `setTransmitIntent(on, paRequested, drive)` for the P2/Brick path (mirroring how the P1/HL2 FSM drives its wire). `drive` comes from the operator TX‑power %; `paRequested` from the PA‑enable toggle. Reuse the shipped MOX‑edge sequencing discipline (freq‑before‑MOX, MOX‑off after the faded tail). The DUC‑IQ stream (`P2TxPump`→`P2TxWriter`, port 1029) is already primed/paced in RX and simply carries real TX samples once `inject` flips.

### S‑T3 — Safety (keep Jerry's fail‑closed gate; add the Brick interlocks)
Do **not** weaken `P2TxSafetyGate` — it fails closed on `!operatorArmed || !sessionRunning || !iqPrimed || !telemetryHealthy || faultLatched`. Add for the Brick: the **operator‑armed interlock** (transient, default OFF), the **default‑OFF "Enable Brick P2 TX"** capability (no‑inert‑UI, ships with behavior), auto‑disarm on `force_release_all` / TX‑timeout, and the P2 **stale‑telemetry → TX‑fault** latch already in `P2Session`.

### Bench gates (before any real antenna)
- **Dummy load, NO external amp, low drive** — bare Brick2 output is benign.
- **Golden‑packet test first:** `diagnosticHighPriorityPacket(run=true)` must equal the captured keydown pattern (MOX bit, OC `0x04`, the constant Alex bytes) **byte‑for‑byte** — a pure unit check, no radio, before keying anything.
- **Kill‑test (P2 watchdog):** `taskkill /F` mid‑TX into the dummy → the Brick's P2 stale‑stream watchdog must drop TX within N sec (P2 has a real timer keepalive, unlike the HL2 P1 path — a genuine safety win here).
- **PA‑current observability:** if the Brick exposes PA current in its EP6/HP status telemetry, decode + display it (as we did for the HL2) so the kill‑test is observable; if not, the external watt‑meter is the instrument.
- **2nd‑rig confirm:** LB3AG's Brick2 as an independent check the TX profile isn't over‑fit to your unit.

---

## 4. Why this is small (effort honesty)

| Piece | Effort | Because |
|---|---|---|
| S‑T0 un‑grey arm | ~1 hr | one `p2ProfileForModel` case + a Brick profile struct |
| S‑T1 fixed TX word | ~1‑2 hr | a constant, not a per‑band function (the capture proved it) |
| S‑T2 FSM→intent wire | ~½ day | reuse the shipped MOX‑edge discipline + existing pump/writer |
| S‑T3 safety + UI + tests | ~½‑1 day | gate exists; add interlock + default‑OFF toggle + golden‑packet test |
| Bench + kill‑test + 2nd rig | operator bench | dummy‑load, watt‑meter, kill‑test |

The hard 80% (DUC pump, paced port‑1029 writer, HP encoder, fail‑closed gate, session lifecycle) is **already written** — Jerry's, GPL, in our branch. We are adding a Brick profile + a constant + an FSM wire + a default‑OFF safety toggle.

---

## 5. The Jerry / reconciliation posture
- Keep his `P2Tx*` modules and their authorship **intact** — we add a Brick profile alongside, we don't rewrite his G2 path.
- If he re‑engages: our Brick profile + `p2ProfileForModel` Brick case + the constant TX word are additive and mergeable; the RX‑gain naming/auto question is separate and still his `ActiveFrontEndModel` call.
- If he doesn't: this is a clean, self‑contained solo path with a working reference (deskHPSDR + his published G2 engine) and full bench discipline.

---

## 6. References to work from
- **deskHPSDR** `D:/sdrprojects/deskhpsdr` — P1+P2, Brick‑first‑class (`HERMES_MODE_BRICK`, `radio.h:119‑123`); `new_protocol.c` = the authoritative generic‑Hermes P2 TX packet builders (Brick emits identical packets — zero Brick TX branches).
- **ramdor‑Thetis** `D:/sdrprojects/ramdor-Thetis` — `ChannelMaster/network.c` / `netInterface.c` P2 TX path (secondary cross‑check).
- **Your captures** — `brick_p2_sniff*.log`, `brick_p2_hp_diff.log` (the ground truth; §2).
- **Jerry's `g2‑p2‑tx`** — the working P2 TX engine we reuse.
- **`brick_p2_buildout_plan.md`** (sibling doc) — the RX/model/capability half + the full §0.5 capture writeup.

---

## 7. Open items to pin during the build (verify‑don't‑guess)
1. ~~Fold in the deskHPSDR/Thetis offset dossier~~ **DONE 2026‑08‑15 (§2.1)** — MOX bit4/0x02, DUC‑IQ port‑1029 (240×24‑bit‑BE pairs, 800 pkt/s), drive HP[345] all confirmed vs the capture.
2. **Read the exact Brick TX Alex/relay constant off `brick_p2_hp_diff.log`** (the `08 04 01 00 … 08 04` at 1428‑1435) + **reconcile the OC byte to 1401** (capture read ~1402; authoritative offset is 1401, value `0x04`). Confirm byte‑for‑byte before it's on the wire; the golden‑packet test enforces it.
3. **CW‑keying mode for Brick P2** — pin whether Brick CW transmits as **host‑streamed I/Q** (TUN‑style, MOX set) or a **P2 CW‑key channel** (FPGA‑keyed, MOX *not* set, like deskHPSDR internal CW). Decides the CW path in S‑T2. (SSB/TUN are unambiguous: host‑streamed I/Q + MOX.)
4. Confirm whether the Brick exposes **PA‑current telemetry** on P2 (for the observable kill‑test); if not, watt‑meter it.
5. Decide the operator‑facing **arm labels** (Brick vs G2) so both read correctly.
