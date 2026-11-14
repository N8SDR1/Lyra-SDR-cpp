# #171 — External CW keyer support (Winkeyer / K1EL WKmini)

**Status:** **DROP extra K1EL host driver** (operator re-look 2026-10-16 vs
Thetis + DeskHPSDR). Neither app implements the Winkeyer USB protocol.
Thetis: Setup → DSP Keyer **Connections** Primary = **Radio** (or a COM
paddle on DSR/CTS) and Secondary **SetCWX** on a COM key/PTT pin.
DeskHPSDR: **CW handled in Radio** (`cw_keyer_internal`); FPGA iambic on
the KEY jack, else host `iambic.c` from radio dot/dash, plus CAT/MIDI CW.
Zero `winkey` / `K1EL` hits in either tree.

Lyra already matches that: KEY jack + **Iambic OFF** (Winkeyer KEY out),
and **Settings → CW → CW key over COM port** (`SerialCwKey`, CTS/DSR →
CWX). Do **not** add a Lyra-owned WKmini COM driver (steals the logger
port; serial echo is not a keying stream). Grounded 2026-06-22 against the **K1EL WKmini
v1.3 manual** + the **"KE1L winkeyer info" interfacing note**
(`Y:\Claude local\SDRProject\docs\`), the operator's SDRLogger+ K1EL
driver (`Y:\Claude local\hamlog\main.py:1931-2132`), Thetis 2.10.3.13,
and lyra-cpp's existing CW/EP6 code.

**Scope:** let the operator key CW through Lyra from their **K1EL
WKmini** (paddle and/or logger-fed text), reusing the Winkeyer's own
hardware timing. Operator confirms this already works with Thetis and
with SDRLogger+; this doc pins exactly what Lyra needs.

---

## 1. How the WKmini actually keys a radio (manual-confirmed)

The WKmini **is** the keyer — it does the iambic/timing in hardware
(the whole reason it exists, per the manual's opening: software Morse
on multitasking PCs is badly timed). It emits CW on a **physical,
opto-isolated KEY output**, driving the rig **"in the same manner as
a straight key"** (manual pp.7-8): two leads, ground + key. Two
outputs (KEY1, PTT1/KEY2, 60 V @ 200 mA SSR). No host involvement in
the keying itself.

**Serial echo is display-only, NOT a keying stream.** The WKmini
sends data back to the host for four reasons (manual p.10): status
change, speed-pot/pushbutton change, status-request reply, and
**"Echo Morse as it's being sent by message or by paddles."** That
echo is **completed characters** (p.6: paddle echo → "letters
displayed"), after the fact — there is no real-time dit/dah edge
stream over serial. A host that re-keyed from the echo would lag a
full character and lose the timing. **So Lyra must not try to
reconstruct keying from the serial port.**

Other manual facts that matter:
- **No sidetone** in the WKmini (p.6) — the operator relies on the
  rig/SDR sidetone. Lyra has CW sidetone (#105).
- **Paddle break-in/QSK is inside the WKmini** (p.11 "Paddle Input
  Priority"): paddle interrupts host data, clears the serial buffer,
  resumes after one word-space. Lyra gets QSK for free.
- **Serial:** 1200 baud (alt 9600 by cmd) 8N2, FTDI USB-serial,
  host-mode handshake (the SDRLogger+ driver implements it exactly).

---

## 2. The keying path for an HL2/Lyra (the real one)

```
WKmini KEY out ──(straight-key signal)──▶ HL2 KEY jack
                                              │
                                       HL2 gateware (CW mode)
                                              │ keys the carrier
                                              ▼
                                       on-air CW
```

This is Path A — the same path the operator uses with Thetis. The
WKmini does the timing; the HL2 keys the carrier from the key line.
**The host SDR app's job is minimal: be in CW mode and let the
gateware-keyed carrier transmit.** (SDRLogger+ feeding the WKmini
text over serial doesn't change this — the WKmini still keys via its
physical output into the HL2 jack.)

The earlier "TCI `keyer:` bridge" and "Lyra reads keying over serial"
ideas are both superseded by the manual: the keying is a hardware key
line, and the serial echo can't carry real-time timing. (The TCI
`keyer:` discrete path stays valid for a *different* case — a logger
relaying its OWN computed element timing — but it is **not** how a
Winkeyer's hardware timing reaches the radio.)

---

## 3. The one thing to verify before building (HL2 gateware)

Because the operator already keys the WKmini through Thetis on this
HL2+, the wiring + gateware path **works** — the open question is
purely **what Lyra must do to pass the HL2-key-jack-keyed carrier
on-air**, i.e. how the HL2 gateware surfaces an external key on the
KEY jack:

- **(a) Gateware keys the carrier directly** off the key line when in
  CW mode → Lyra needs ~nothing for keying (just CW mode + don't
  fight it). Most likely, given Thetis "just works."
- **(b) Gateware surfaces the key as EP6 `dot_in`/`ptt_in`** and
  expects the host to drive `cwx` → Lyra must consume it.

**Grounding for the verify:** the HL2+ `ak4951v4` gateware RTL is at
`Y:\Claude local\_hl2src\` (the repo the operator pointed to for the
PA/telemetry work). Check how the KEY-jack pins map to `cwx_keydown`
/ the EP6 dot/dash/ptt status. **Lyra-side caveat:** lyra-cpp decodes
`ptt_in`/`dot_in`/`dash_in` into `lyra::wire::prn`
([Ep6RecvThread.cpp:889-891](src/wire/Ep6RecvThread.cpp)) but only
`ptt_in` is *consumed* (the opt-in HW-PTT forwarder), and there's a
noted reference defect: the `(cc[0]<<1)&0x01` / `(cc[0]<<2)&0x01`
dot/dash formulas "always evaluate to 0." So if path (b) is the
reality, the dot/dash decode must be fixed first — but path (a) is
the likely answer and needs no decode at all.

A one-agent HL2-RTL read settles (a) vs (b) before any code.

---

## 4. What Lyra builds — two separable pieces

### Piece A — the keying (required for the WKmini to work at all)
Whatever the §3 verify shows:
- If (a): confirm Lyra's CW-mode TX path doesn't suppress an
  externally-keyed carrier; add a Settings note. Near-zero code.
- If (b): fix the EP6 dot/dash decode + drive `cwx`/`cwx_ptt` from
  the gateware key line (or feed Lyra's internal iambic from
  dot/dash — but the WKmini already did the iambic, so a straight
  key-down → `cwx` mirror is correct, not a re-iambic).

This is the part that puts the operator's WKmini on the air through
Lyra. It is **independent of the K1EL serial protocol** — it's HL2
key-jack handling.

### Piece B — Lyra-resident K1EL host driver [CONSIDERED, DROPPED]
**Dropped by operator decision 2026-06-22 — it buys almost nothing
for real cost.** Its only value would be routing *canned* macros
through the WKmini's hardware timing (negligible vs the internal
keyer for pre-baked Morse) and configuring WPM/mode from Lyra (a
set-once thing the operator already does via SDRLogger+/WK3tools).
Against that: it steals the WKmini COM port from SDRLogger+
(single-host) and is a whole serial-driver to port + maintain. Lyra
already sends CW via the internal keyer (#105) + the #176 macro
chips, so B adds no capability the operator wants. **Not built.**

For the record, the port would have been a clean lift of the
SDRLogger+ driver (`main.py:1931-2132`), the standard host-mode
sequence:
- Open 1200/8N2, DTR asserted → Admin:Close (`00 03`) → 4× `13` sync
  → Admin:Open (`00 02`, read firmware version byte).
- Configure: PINCFG `09 <cfg>` (KEYPORT1/2 + PTT), Mode `0E <mode>`
  (iambic A/B / ultimatic / bug), PTT lead/tail `04 <lead> <tail>`
  (×10 ms), WPM `02 <wpm>`.
- Send: ASCII text in ≤32-byte chunks; WPM on-the-fly `02 <wpm>`;
  abort = Clear Buffer `0A`.
- Lyra-native via `QSerialPort` (the Qt equivalent of `pyserial`);
  port picker via `QSerialPortInfo::availablePorts()`.
- Optionally read the **character echo** to *display* sent CW in
  Lyra's CW console (display only — never to re-key).

Piece B does **not** generate on-air keying — the WKmini still keys
via its physical output into the HL2 jack (Piece A). B just lets
Lyra's macro/keyboard buttons drive the WKmini instead of SDRLogger+.

### What we are NOT doing
- **No re-keying from the serial echo** (it's lagging char-level —
  §1).
- **No CAT/Kenwood emulation** (Thetis's `KY;` path) — out of scope.
- **No re-deriving iambic from a Winkeyer-keyed line** — the WKmini
  already did the timing; the host mirrors key state, never re-times.

---

## 5. Settings surface
- **Piece A:** a CW-key-input note/toggle in Settings → Hardware (the
  HL2 key jack is the source; nothing to configure if §3 = (a)).
- **Piece B (if built):** a "K1EL Winkeyer" group — Enable, COM port
  (picker), WPM, keyer mode, key-output (KEY1/KEY2/both), PTT +
  lead/tail — mirroring the SDRLogger+ panel
  (`hamlog/index.html:1171-1242`).

---

## 6. Build / verify plan
1. **HL2-RTL read** (`Y:\Claude local\_hl2src\`): KEY-jack → gateware
   keying map; decide §3 (a) vs (b). *Gates everything.*
2. **Piece A** per the verify (CW-mode pass-through, or EP6 dot/dash
   decode fix + `cwx` drive). Operator bench: WKmini paddle → on-air
   CW through Lyra (the same test that works on Thetis).
3. **Piece B** (if wanted): port the K1EL host driver to a
   `wire/WinKeyer.{h,cpp}` `QSerialPort` class + Settings panel +
   route Lyra's CW-console/macro text to it; optional echo→display.
4. Clean build (`lyra.exe ... [QML]`).

---

## 7. Locked scope (operator decision 2026-06-22): Piece A only

#171 is **Piece A only** — make the WKmini's paddle key on-air
through Lyra (KEY line → HL2 jack → gateware), exactly like Thetis.
Piece B (Lyra hosting the WKmini over serial) was considered and
**dropped** (§4.2) — it adds no capability the operator wants and
costs the single-host port conflict. No Winkeyer serial driver in
Lyra.

### 7.1 The two on-air CW paths (both already shipped or trivial)
| Source | Route |
|---|---|
| Paddle | → WKmini (hardware iambic + break-in, manual p.11) → HL2 jack → air **(Piece A)** |
| Lyra macro chip / keyboard / TCI `cw_macros` | → internal software keyer (#105) → `cwx` bits → air |

Two independent paths, no conflict: the operator's paddle keying runs
through the WKmini's hardware timing (via the HL2 jack), and Lyra's
own macro/keyboard CW runs through the shipped internal keyer. The
existing CW speed control applies to the internal-keyer path. The
WKmini stays owned by SDRLogger+ (or stand-alone) — Lyra never opens
its COM port.

### 7.2 Macro chips → tracked as #176
Lyra-native named click-to-send macros in the existing CW console
(edit-mode to save a typed string as chip XX; click → `sendCw`).
Builds on the internal keyer (#105) + the shipped speed control;
inline `>`/`<` speed-step + `|XX|` prosign-combine already parse.
Independent of #171 — works with the internal keyer alone, no
Winkeyer needed. (CW sibling of #89 Voice Keyer.) See #176.

## 8. Sequencing
1. **Piece A, gated on the §3 HL2-RTL verify** — decides near-zero
   code (gateware keys the carrier directly off the KEY jack) vs the
   EP6 dot/dash decode fix. This is the whole of #171.
2. **#176 macro chips** — independent small UI feature on the
   internal keyer; does not wait on the §3 verify.
3. **#173 RX CW decoder** — separate, independent, niche popup;
   later/possibly; NOT part of #171.
