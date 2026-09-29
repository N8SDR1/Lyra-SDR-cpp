# CW Transmit — port plan (#105)

Status: **DESIGN / mapped-out 2026-06-17; TCI-keying study + re-scope
2026-06-18** (no code yet). Operator scope: paddle + TCI keying first.
**Read §10–§12 for the TCI study + the revised build order (CW-1…CW-5).**
Key result: TCI CW keying needs the native keyer engine (CW-3); the 0x0b
collision risk is resolved (§2 update).
Grounded in a 2-lane reference dive: Thetis 2.10.3.13 host side
(`Console\cwx.cs`, `console.cs`, `setup.cs`, `wdsp\TXA.c`) + the HL2 wire
(`ChannelMaster\networkproto1.c`, `network.h`) + the operator's HL2+
gateware RTL (`Y:\Claude local\_hl2src\`, variant `hl2b5up_ak4951v4`,
`CW=2` = `CW_OPENHPSDR`) + the HL2 wiki Protocol.md. All line numbers below
are reference citations (provenance only — shipped Lyra code/comments use
RF/gateware terms, not reference names; this doc is the provenance home).

---

## 0. The one fact that shapes everything

**On HL2 the CW carrier is 100 % gateware-keyed. There is NO host DSP / WDSP
synthesis of a CW carrier, and the EP2 TX-I/Q payload is BYPASSED for the RF
during CW.** The host's entire job is:

1. tell the gateware "we're in CW" (a C&C bit), and
2. deliver **key-state** — either by letting the FPGA's internal iambic
   keyer run off the physical paddle jack, or by the host asserting
   computer-CW key bits in the EP2 TX I-sample LSBs.

Gateware proof (operator's variant):
- `radio.v:1145-1146` — `tx_i = vna ? … : (tx_cw_key ? {1'b0, tx_cwlevel[18:4]} : y2_i); tx_q = (vna|tx_cw_key) ? 0 : y2_r;` → during CW the DAC is fed the **shaped envelope ramp** `tx_cwlevel`, Q=0; the normal interpolated TX-I/Q chain (`y2_i/y2_r`) is bypassed.
- `radio.v:1052-1094` — the FPGA ramps `tx_cwlevel` up/down 1 LSB/clock (rise/fall shaping) + CW hang FSM. **Envelope shaping is FPGA, not host.**
- `hermeslite_core.v:1294-1295` — `.sidetone_sel(cw_on)`, `.profile(cw_profile=tx_cwlevel)` → the **AK4951 hardware sidetone** is FPGA-generated, amplitude tracking the CW envelope.
- `control.v:783-820` — variant sets `CW=2 CW_OPENHPSDR` → instantiates an **internal iambic keyer** `cw_openhpsdr` fed `dot_key=ext_cwkey` (debounced phone tip) + `dash_key=clean_ring`, producing `cw_keydown`+`cw_ptt`. (The wiki's "no internal keyer" text describes the older `CW_BASIC=1`; this variant DOES have one. The `cw_openhpsdr.v` body is **not in the RTL dump** — its internal timing isn't verifiable, but Lyra doesn't replicate it; the gateware owns it.)

WDSP confirms the host bypass: `TXA.c:753-789` `SetTXAMode` has **no case for
CWL/CWU** — they hit `default: break;`, no modulator runs. (`TXASetupBPFilters`
still sets a bandpass, harmless/inert.)

**Consequence:** CW TX is a **protocol + keyer + UX** task, NOT a DSP-modulator
task like AM/FM/SSB. Most of the heavy DSP machinery is irrelevant.

---

## 1. Two keying paths (both end at the same FPGA carrier)

| Path | Source | Host role | Gateware role |
|---|---|---|---|
| **A — physical paddle / straight key** | Key/paddle in the HL2 jack | Send keyer **config** (speed/weight/mode/reverse via C&C 0x0b); optionally read key-down back via EP6 for display + semi-break-in trigger | Internal iambic keyer runs the element timing, produces `cw_keydown`, shapes + keys the carrier |
| **B — computer CW (CWX)** | Host keyer engine (memories, macros, a host-side iambic from PC paddle, CAT KY) | Compute element **timing**; set `cwx_enable` (C&C 0x0f bit24); drive EP2 I-sample **bit0 = cwx keydown**, **bit3 = cwx_ptt** | Keys + shapes the carrier from the host's keydown bit |

**For Lyra's software CW (the deliverable), use Path B (CWX).** Path A is
"plug a paddle into the radio and it just works" — Lyra only feeds the
gateware keyer its config and reads the key line for UI/semi-break-in.

---

## 2. What's ALREADY in tree (verbatim port, dormant) — big head start

- **EP2 CW I-LSB packer — DONE (dormant).** `src/wire/NetworkProto1.cpp:99-108`
  already has the verbatim packer:
  `if (prn->cw.cw_enable && j==1) temp = (cwx_ptt<<3 | dot<<2 | dash<<1 | cwx) & 0x0F` (HL2; `&0x07` non-HL2). Writes both I and Q of the TX sample set.
  Inert today because nothing sets `prn->cw.cw_enable` or
  `prn->tx[0].{cwx,cwx_ptt,dot,dash}`.
- **`prn->cw` struct + seed — present.** `RadioNet.cpp:80` seeds
  `prn->cw.edge_length`; the `_cw` struct is in the ported `cmcomm.h`
  (`network.h:185-210` shape: `sidetone_level/freq, keyer_speed/weight,
  hang_delay, rf_delay, edge_length`, + a `mode_control` bitfield union:
  `eer/cw_enable/rev_paddle/iambic/sidetone/mode_b/strict_spacing/break_in`).
- **EP6 dot/ptt decode — already correct.** `Ep6RecvThread.cpp:8` notes the
  reference's `dash_in/dot_in` LEFT-shift bug and uses the right shift. (The
  reference `networkproto1.c:332-334` computes `dot_in=(C0<<2)&1` which is
  always 0 — a real reference defect; Lyra already avoids it.)

**UPDATE 2026-06-18 — the C&C composer cases ARE now in tree** (the doc's
original "not yet" predates the TX wire-layer rebuild, which ported them
verbatim). Verified in `src/wire/FrameComposer.cpp`:
- **case 13 / addr 0x0f** (lines ~349-365): `cw_enable` + `sidetone_level` + `rf_delay`.
- **case 14 / addr 0x10** (~368-383): `hang_delay` split + `sidetone_freq` split.
- **case 12 / addr 0x0b** (~550-595): C1/C2 = ADC1/ADC2 `rx_step_attn`, **C3 = keyer_speed(6b)+CWMode(7:6), C4 = keyer_weight(7b)+strict_spacing(7)**.
All dispatched (switch ~926-934) and read from `prn->cw`. **The §9 "0x0b
collision" RISK IS RESOLVED:** CW keyer config is in case-12 **C3/C4**, the
step-att in case-12 **C1/C2**, and the main ATT-on-TX step-attenuator on
*different* registers entirely (case 4 / 0x1c C3 + case 11 / 0x14 C4) — no
byte overlap. The remaining C-1 work is only the operator→`prn->cw` config
plumbing (today only `edge_length` is seeded, RadioNet.cpp:80) + confirming
`CWMode` is derived from the `prn->cw` mode field.

---

## 3. Wire surface Lyra must drive (the protocol to-do)

### 3a. EP2 per-sample (already ported — just drive the inputs)
Set, from the CW keyer/FSM:
- `prn->cw.cw_enable = 1` while in CW TX.
- `prn->tx[0].cwx` = host keydown (0/1) — **this is the bit the gateware
  reads** (`dsopenhpsdr1.v:360` captures I-sample bit0 as cwx keydown, gated
  by `cwx_enable`).
- `prn->tx[0].cwx_ptt` = CWX PTT (bit3, HL2-only) — held high across an
  element group / message.
- `dot`/`dash` bits (1,2) are packed but **not consumed by CW_OPENHPSDR for
  CWX** — set 0 for the host path; they matter only to the (older) basic
  gateware.

### 3b. C&C frames to ADD to the FrameComposer (HL2 `write_main_loop_hl2`)
Byte layouts verified at `networkproto1.c` HL2 case-builder:
- **case 13 / addr 0x0f / C0 0x1e** (`:1129-1132`): `C1 = cw_enable`
  (== wiki's "enable CWX" bit24); `C2 = sidetone_level`; `C3 = rf_delay`;
  `C4 = 0`. (Gateware reads bit24=cwx_enable, `dsopenhpsdr1.v:393`.)
- **case 14 / addr 0x10 / C0 0x20** (`:1137-1140`): `C1 = hang_delay[9:2]`;
  `C2[1:0] = hang_delay[1:0]`; `C3 = sidetone_freq[11:4]`;
  `C4[3:0] = sidetone_freq[3:0]`. (Gateware: `cw_hang_time` `radio.v:952`.)
- **case 12 / addr 0x0b / C0 0x16** (`:1112-1122`): `C2[6] = rev_paddle`;
  `C3[5:0] = keyer_speed (WPM)`, `C3[7:6] = mode` (00 straight / 01 iambic-A
  / 10 iambic-B); `C4[6:0] = keyer_weight`, `C4[7] = strict_spacing`.
  (Consumed by the internal keyer module — Path A config.) **NO COLLISION —
  RESOLVED, see §2 (verified in `src/wire/FrameComposer.cpp`):** addr 0x0b /
  case-12 carries the RX ADC1/ADC2 `rx_step_attn` in **C1/C2** and the CW
  keyer config in **C3/C4** — different bytes, same register. The shipped
  ATT-on-TX step-attenuator (§15.31) is on **entirely different** registers
  (case-4 / addr 0x1c **C3** + case-11 / addr 0x14 **C4**), so there is no
  byte overlap with the CW keyer C3/C4 here. The earlier "verify before
  composing / do not clobber" caution is superseded — the cases are already
  ported verbatim from Thetis and dispatched. (Naming note: addr 0x0b =
  C0 byte 0x16 = Thetis case-12 are three names for ONE register — that
  three-way notation is what made this look like a conflict.)
- **addr 0x00 C2 bit0 = eer** (`:950`) — already in the frame-0 composer;
  CW doesn't need to change it.

### 3c. EP6 readback (already decoded)
- `ptt_in = C0 & 1` (correct), `dot/key = (C0>>2)&1`, dash = C0[1] = always 0.
  Use the existing decode for: CW key-line display + semi-break-in onset.

---

## 4. Host CW keyer engine (the real native code to write — Path B)

Port the algorithm of `Console\cwx.cs` Lyra-native (it is pure C#, no WDSP):

- **Morse table:** char → element bit pattern (marks in high bits + element
  count in low 5 bits). `cwx.cs:469 build_mbits2`, defaults `:538-601`.
  Ship a built-in table (the reference loads `morsedef.txt`; Lyra can bake a
  default table + allow an override file later).
- **Text → elements:** walk the pattern MSB-first → push `EL_KEYDOWN` per set
  bit, `EL_KEYUP` per clear bit into an element FIFO (`cwx.cs:2392-2492`).
  Dash = 3 keydown + 1 keyup; dot = 1 keydown + 1 keyup (count-based timing).
- **Timing tick:** periodic timer at `tel = 1200 / wpm` ms (PARIS,
  `cwx.cs:407-410`); each tick pops one element and sets key/PTT
  (`process_element`, `cwx.cs:~2195-2276`). Lyra-native: a dedicated
  high-resolution timer/thread (NOT a Qt main-thread QTimer — element timing
  must be steady; reuse the lessons from the EP2-cadence work) feeding the
  `prn->tx[0].cwx`/`cwx_ptt` bits via the wire layer.
- **PTT→key delay** (`pttdelay`, default 50 ms) and **drop/hang** (`ttdel`,
  default 300 ms) — assert cwx_ptt before the first keydown; hold it through
  the message; drop after the hang.
- **Memories:** 9 message slots (F1–F9), macros, repeat. UX layer.
- CWX speed is **independent** of the paddle-keyer WPM (`cwx.cs:417-423`):
  CWX default 22 WPM, range 1–99.

This engine is also the basis for a **host-side iambic keyer** if we ever
want PC-paddle keying that bypasses the gateware keyer — but v1 ships
gateware-paddle (Path A) + computer-CW (Path B); a host iambic is optional.

---

## 5. FSM + carrier convention

- **CW key = a `PttSource` on the existing PTT FSM** (`src/ptt*` — same
  funnel as MOX/TUN/HW-PTT/TCI). Keydown enters a CW-TX state that:
  - does **NOT** start the WDSP TXA channel (no SSB DSP) and does **NOT**
    inject SSB/AM I/Q — CW bypasses the modulator (§0);
  - sets `cw_enable` + drives the cwx key bits;
  - applies **semi-break-in** hang on keyup (HL2 default; full QSK is
    ANAN/Saturn-gated, not on HL2 — fall back to SEMI).
- **Carrier convention (verified `console.cs:32553-32564`):** with the FW
  keyer (default), the **TX LO is NOT pitch-shifted** — the carrier lands at
  the dial; only the RX LO shifts ±cw_pitch (CWL **+**pitch, CWU **−**pitch)
  so the received tone beats at the pitch. **TUN is the lone exception** that
  offsets TX by pitch. Lyra already centralises the RX/TUN ∓cw_pitch offset
  (`txDdsHzForTune` / the RX path) — for CW TX simply do **not** add a TX-NCO
  offset (TUN already handled). Sanity: a CW signal must zero-beat at the dial
  for a co-channel listener.

---

## 6. Sidetone

- **Hardware sidetone is FREE** — the FPGA generates it on the AK4951 output
  when `cw_on` (`hermeslite_core.v:1294`). Lyra just enables it via the C&C
  sidetone bit + sets `sidetone_freq` (= cw_pitch) and `sidetone_level`.
- **Optional host sidetone** for the PC-soundcard / monitor path — reuse the
  shipped #90 TX-monitor infrastructure (MON + Monitor slider): generate a
  keyed tone at cw_pitch in lockstep with the key bits. Defer unless wanted;
  the hardware sidetone covers the headphone-jack operator.

---

## 7. Operator control surface (Settings → TX → CW group) + defaults

| Control | Range | Default | Wire |
|---|---|---|---|
| Keyer speed (WPM) | 1–60 | **25** | C&C 0x0b C3[5:0] |
| CW pitch (= sidetone freq) | 200–2250, step 10 | **600** | C&C 0x10 sidetone_freq + RX-LO offset |
| Keyer weight | 33–66 | **50** | C&C 0x0b C4[6:0] |
| Iambic mode | A(0) / B(1) | **A** | C&C 0x0b C3[7:6] |
| Iambic enable | on/off | on | C&C 0x0b mode field (00=straight) |
| Reverse paddles | on/off | false | C&C 0x0b C2[6] |
| Strict spacing | on/off | off | C&C 0x0b C4[7] |
| Break-in | OFF / SEMI / (QSK n/a HL2) | **SEMI** | host-side hang logic |
| Break-in (hang) delay | 0–1000 ms | **300** | C&C 0x10 hang_delay (= delay + key_up_delay) |
| Key-up delay | ms | **10** | folded into hang_delay |
| Sidetone (HW) | on/off | on | C&C 0x0f/0x10 |
| Sidetone level | 0–127 | — | C&C 0x0f C2 |
| Edge length | (fixed) | **9** | C&C (reference hard-codes 9; control disabled) |
| **CWX** speed | 1–99 | **22** | host keyer (independent of paddle WPM) |
| CWX PTT delay | 50–2000 ms | **50** | host |
| CWX drop/hang | 0–5000 ms | **300** | host |

---

## 8. Suggested build order (each its own commit, RX-safe until the last)

1. **C-1 — `prn->cw` config plumbing + C&C composer cases (0x0f / 0x10 /
   0x0b).** Verify the 0x0b ATT-on-TX collision FIRST. Wire-inert until
   `cw_enable` is set. Bench gate: RX + SSB/AM TX unchanged (CW C&C frames
   carry zeros / safe defaults until driven).
2. **C-2 — operator CW control surface** (Settings → TX CW group + QSettings
   + push to the composer). Still inert (no keydown source).
3. **C-3 — native CWX keyer engine** (Morse table + element FIFO + steady
   timer) driving `prn->tx[0].cwx`/`cwx_ptt` + `cw_enable`; FSM CW PttSource;
   semi-break-in hang. **First RF-capable CW commit** — bench gate: dummy
   load, watch the carrier key cleanly, sidetone in the phones, zero-beat at
   the dial.
4. **C-4 — paddle path polish:** read EP6 key-line for display + drive the
   gateware keyer config so a physical paddle works; semi-break-in triggered
   off the key line.
5. **C-5 — UX:** message memories (F1–F9), macros, a CW keyer panel.
6. **Phase-3-EXIT safety gate** (same as the modulators): kill-test +
   stuck-carrier check on dummy load before any antenna CW.

## 9. Risks / open items

- **0x0b register collision** with the shipped ATT-on-TX step-attenuator on
  the HL2 path — MUST be resolved before composing CW keyer config (read the
  exact HL2 case for 0x0b; do not clobber `tx_step_attn`).
- **`cw_openhpsdr.v` body absent** from the RTL dump → internal iambic timing
  + exactly which C&C addrs the keyer module decodes are unverifiable. Not
  blocking (gateware owns it) but flag if paddle behavior is off.
- **Bit3 cwx_ptt is HL2-only** — keep the family gate (already in the ported
  packer).
- Steady CWX element timing must NOT ride the Qt main thread (apply the
  EP2-cadence lessons — dedicated timer/thread).
- No on-air testing possible until weather clears; C-1/C-2/C-3 are
  bench-on-dummy-load verifiable.

---

## 10. TCI as a CW keyer (study 2026-06-18, 3-source dive)

Sources: **TCI Protocol v2.0 manual** (`D:\sdrprojects\TCI Protocol.pdf`,
41 pp); **Thetis `Console\TCIServer.cs` + `cwx.cs`** (impl reference); Lyra
**`src/tci_server.cpp`** (current state). Provenance only — shipped Lyra
code uses RF terms, not reference names.

### 10.0 THE mechanism (settles the C-3-is-needed question)
TCI keys CW primarily by the client sending **morse TEXT** (`cw_macros` /
`cw_msg`); the **host application renders the morse itself and keys the
radio element-by-element.** Thetis is NOT a "send text, radio makes WPM"
black box — it is a PC app whose CWX engine (`cwx.cs`) renders text → element
FIFO → a **Win32 multimedia timer at the dot rate** → `SetCWX()` keys the
wire. **Lyra is the same kind of host app**, so **TCI CW keying REQUIRES
Lyra's own native keyer engine** (the §4 / C-3 deliverable). There is no TCI
shortcut that avoids building the keyer. **Corollary: the TCI text path and
the future keyboard/computer-CW path feed ONE shared keyer engine** → EP2
`cwx`/`cwx_ptt` bits (§3a). Build the engine once; both consume it.

A secondary discrete path, **`keyer:rx,<bool>,<prev_elem_ms>`**, bypasses the
morse engine (the client supplies element timing; the host just gates the key
line with PC-clock-accurate duration + a stuck-key watchdog). This is the
natural bridge for an external straight key / Winkeyer over TCI (#171).

### 10.1 TCI CW command surface (client→radio unless noted)
| Token | Args | Role |
|---|---|---|
| `cw_macros` | `rx,text` | free morse text to key; queues; inline `>`/`<` = ±5 WPM; `\|XX\|` run-together; `:`,`,`,`;` escaped `^`,`~`,`*` |
| `cw_msg` | `rx,prefix,call,suffix` | structured msg; `$2` repeats call; **higher priority than macros**; 1-arg form = live-correct the not-yet-sent callsign |
| `cw_macros_stop` | — | **abort** all macros/messages |
| `cw_macros_speed` | `wpm` (1–99) | macro WPM (bidir/query) |
| `cw_keyer_speed` | `wpm` | hardware/paddle keyer WPM (client→radio) |
| `cw_macros_speed_up` / `_down` | `n` | ±N WPM live |
| `cw_macros_delay` | `ms` | PTT→key delay (bidir) |
| `cw_terminal` | `bool` | stay in TX after macro (enables the callbacks below) |
| `cw_macros_empty` | `rx` | **radio→client** — last queued letter started (terminal mode) |
| `callsign_send` | `call` | **radio→client** — final keyed callsign (post-correction) |
| `mon_enable` / `mon_volume` | — | generic TX monitor (covers CW sidetone; pitch is NOT a TCI param) |

Thetis behavior to mirror (TCIServer.cs / cwx.cs): new macros **queue, no
preempt** (only `cw_msg` mid-`cw_msg` replaces the not-yet-sent callsign;
`cw_macros_stop` is the only abort); element timing on a **dedicated timer/
thread, never the UI thread**; the `keyer:` straight-key path on its own
high-priority Stopwatch thread + 3 s watchdog; `cw_macros_empty`/
`callsign_send` emitted from the keyer's char-started/segment-complete hooks.

### 10.2 Lyra TCI current state (`src/tci_server.cpp`)
- **Works:** `CW_PITCH` (RX sidetone pitch) + CW mode-token mapping
  (`CWL`/`CWU`↔`CW`); modulations list already advertises `CWL,CWU(,CW)`.
- **Silent no-ops** (the accept-and-drop block ~1452-1459): `KEYER`,
  `CW_MACROS*` (one blanket prefix — none individually parsed),
  `CW_MSG`, `CW_TERMINAL`, `CW_KEYER_SPEED`. → sending `cw_msg:0,CQ` does
  nothing on the wire today. The header even documents "cw_* acknowledged
  inactive." Root cause: no CW TX modulator yet (this task).

### 10.3 Confirmed against SDRLogger+ — the operator's daily TCI client
SDRLogger+ (sibling project, `Y:\Claude local\hamlog\main.py`) is a TCI
*client*; its CW keyer emits EXACTLY (main.py:2181-2202):
- `cw_macros_speed:<wpm>` — set WPM
- `cw_macros:0,<text>` — send morse text (client-side expands `{MYRIG}`/
  `{MYGRID}` etc. BEFORE sending → Lyra receives plain text + inline
  `>`/`<`/`|XX|` controls only; no macro-var expansion needed server-side)
- `cw_macros_stop` — abort
It does NOT use `cw_msg` or the discrete `keyer:` path for TCI (it has
separate WinKeyer-serial + HamLib CW backends). **→ The CW-4 interop MVP for
the operator's own logger is just these three handlers** wired to the keyer
engine (`cw_macros_speed` → WPM; `cw_macros:0,text` → render+key;
`cw_macros_stop` → abort). The fuller §10.1 surface (`cw_msg`, callbacks,
terminal, speed_up/down) is for broader client compatibility. SDRLogger+ also
has a CW **decoder** (RX-side: received CW → text) — out of scope for #105 TX,
noted as a possible future Lyra RX feature.

---

## 11. TCI handshake + Network-tab settings — gaps (audit 2026-06-18)

### 11.1 Handshake (`sendInit`, tci_server.cpp ~923-981) — present vs gap
Advertises: `protocol Lyra,1.9` · `device HermesLite2` · `receive_only false`
· `trx_count 1` · `channel_count 1` (+legacy `channels_count`) · `vfo_limits
10000,2147483647` (RF dial / Xvtr; not native-HL2 IF) · `if_limits ±rate/2` · `modulations_list
USB,LSB,CWU,CWL,AM,SAM,DSB,FM,DIGU,DIGL(,CW)` · audio-stream params · `ready`.
**Gaps:** advertises **1.9** though it speaks v2.0 binary streams (consider
2.0); **no CW params echoed** at connect (`cw_macros_speed`/`_delay`/
`cw_keyer_speed` — clients init their CW panel from these); **`tx_enable`
only reactive** (sent on query, not advertised — MSHV wants the echo to permit
TX); **`vfo_limits` hardcoded** (not band/HL2-derived).

### 11.2 Network-tab TCI settings (settingsdialog.cpp ~638-827)
Present: bind host, port, rate-limit, send-initial-state, "add CW to
modulations", emulate-ExpertSDR3, emulate-SunSDR2, RX-out gain, TX-in gain,
master enable + status. **Missing (the operator's "settings we still need"):**
- **CW group** (lands with the keyer): keyer WPM default, keyer mode
  (straight / iambic A / B), reverse paddles, weight, strict-spacing,
  break-in OFF/SEMI, hang/PTT-delay, HW-sidetone enable+level, CWX speed +
  PTT-delay + drop/hang. (These map to §7's control table — the Settings→TX
  CW group and the TCI CW params are the SAME state, just two surfaces.)
- **Advertised protocol version** selector (locked 1.9).
- **TX-enable / receive-only** operator toggle (hardcoded today).
- **Audio/IQ stream params** (rate / channels / packet / buffering — all
  hardcoded constants; no operator surface).
- **Max-clients / allowed-clients** (binds + accepts any).

---

## 12. Revised build order (operator scope 2026-06-18: paddle + TCI first)

Supersedes §8 for the chosen scope. The keyer engine is the shared core that
both TCI-keying and (later) keyboard-CW need, so paddle goes first for the
fastest on-air win, then the engine, then TCI rides the engine.

- **CW-1 — Foundation (shared):** `Cw` `PttSource` on the PTT FSM
  (`hl2_stream.*`) → CW-TX state that sets `cw_enable`, does NOT start the
  WDSP TXA modulator, holds carrier at the dial (no TX-NCO offset), SEMI
  break-in hang. + operator→`prn->cw` config plumbing (composer already
  consumes it, §2 update) + **Settings → TX → CW group** (§7 defaults).
  Wire-inert until keyed → RX/voice-TX byte-identical. Bench: dummy load.
- **CW-2 — Paddle / gateware iambic → FIRST CW ON AIR.** Push keyer config
  (0x0b case 12 C3/C4) so a physical paddle/key in the HL2 jack works (FPGA
  keys + makes HW sidetone); read EP6 key-line for display + semi-break-in
  trigger. Small — no host engine. Bench: paddle → clean keying, sidetone,
  zero-beat at dial.
- **CW-3 — Native keyer engine (the core).** Morse table → element FIFO →
  steady **off-Qt-thread** timer (EP2-cadence discipline) → drive
  `prn->tx[0].cwx`/`cwx_ptt` + `cw_enable`; integrate the FSM `Cw` source;
  queue + `stop` abort + inline ±5 WPM. Port the algorithm of `cwx.cs`
  Lyra-native. This unlocks BOTH TCI keying and keyboard CW.
- **CW-4 — TCI keying (rides CW-3).** Wire the silent-no-op handlers to the
  engine: `cw_macros`/`cw_msg`/`cw_macros_stop`/`cw_macros_speed`/`_up`/
  `_down`/`cw_keyer_speed`/`cw_terminal`; emit `cw_macros_empty` +
  `callsign_send`; queue/priority/abort per §10.1; advertise CW params +
  (optionally) bump protocol to 2.0 + advertise `tx_enable`.
- **CW-5 — Network-tab + Settings→TX CW surface** (§11.2 list; the CW
  controls are shared state with CW-1's group). Non-CW TCI polish (protocol
  selector, stream params, tx-enable toggle, max-clients) can be its own
  small task.
- Later: keyboard-CW UX (type + F1–F9 memories) on CW-3; external USB keyer
  (#171) via the `keyer:` discrete path + a COM-port surface.
- **Phase-3-EXIT safety gate** before antenna CW: kill-test + stuck-carrier
  check on dummy load (same as the voice modulators).
