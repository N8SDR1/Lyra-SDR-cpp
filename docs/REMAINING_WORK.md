# lyra-cpp — Remaining Work

> **Historical snapshot, frozen at v0.7.0 (2026-06-29).** Several items below
> have since shipped (VOX, voice keyer, CW TX, BrickSDR2 Protocol 2 RX+TX,
> 2-tone, per-band PA including 11m). For the live operator picture use
> [Feature Status](wiki/Feature-Status.md), [Roadmap](wiki/Roadmap.md), and
> [KNOWN_ISSUES.md](../KNOWN_ISSUES.md). Tree version is **v0.24.1**.
>
> **Still actually open (2026-09):** RX2 / SPLIT · PureSignal · VAC2 ·
> Linux / macOS · ANAN family as first-class P2 radios · other P1 boards
> that need a tester.

What's still open as of **v0.7.0** (2026-06-29). The list keeps shrinking —
the big TX bring-up, the native DSP rack, CW transmit + decoder, Profiles,
VAC, the panadapter/tuning UI, Spots, the Tuner memory, and the FM transmit
refinements are all **shipped and on-air-confirmed**. What's left is mostly
two large arcs (RX2/Split and PureSignal) plus a handful of smaller TX and
CW follow-ons.

---

## The two big arcs (the bulk of what remains)

- **RX2 + Split** (#96–#101) — second receiver end-to-end:
  - DDC1 wire dispatch + a 2nd `WdspEngine` RxChannel + RX2 freq writer.
  - Stereo-split audio routing (RX1 hard-L / RX2 hard-R).
  - Focused-RX UI model (2nd VFO LED, focus border, SUB button,
    Ctrl+1/Ctrl+2, middle-click swap, per-RX vol/mute).
  - SPLIT semantics + per-VFO TX indicators + A→B/B→A/SWAP + per-mode
    shift-offset.
  - Persistence + per-band RX2 freq memory + TCI channel-1 dispatch.
  - Extend RIT/XIT to RX2 (was RX1-only when shipped).

- **PureSignal** (the last big TX-port pillar) — predistortion/calibration:
  - Prerequisite: TX analyzer port to reference parity (#140 / Stage E.1).
  - Then the PS calibration + auto-attenuator + coefficient persistence
    arc (Stage G/H).

---

## TX follow-ons (smaller)

- **VOX** (#91) — voice-operated TX: PttSource on the FSM + threshold +
  open/close hang + anti-VOX.
- **TX Voice Keyer / message memory** (#89).
- **DSP per-mode buffer / filter-size / latency surface** (#159) —
  Setup→DSP→Options parity.
- **#114** — post-FrameComposer TX-policy plumbing: panadapter TX offset +
  PA-enable safety (the ATT-on-TX half already shipped, §15.31/#166).
- **#118** — Step-14 Stage 2b: Wire-LIVE EP6 migration + `txWorker_`
  shared-counter + the 7-instrument re-home.
- **#119** — non-HL2 family FrameComposer branches (blocked: needs tester
  hardware for ANAN / other P1 families).
- **#123** — finish the line-by-line TX reference-fidelity audit (Pass 2
  wave 3 was paused).
- **#55** — RX/TX quick-preset chip panel (one-click named-profile recall),
  a follow-on to the shipped Profile Manager.

## CW follow-ons

- **External CW keyer support** (#171) — Winkeyer/USB: COM-port + keyer-
  device config surface.
- **CW keyer settings panel** (#186 / C-5) — break-in / hang / PTT-delay /
  sidetone settings.
- **CW decoder accuracy — Tier B onward** (#187 next = B1: independent
  dah-length tracking + the −½ln(var) term + warm-up boundary fix). The
  safe A-series shipped in v0.4.10; Tier B/C want a synthetic-CW
  regression suite + on-air A/B first; Tier D parked.

## Audio / routing

- **VAC2** (#103) — second-channel host-audio bridge (e.g. a logger on a
  device independent of the digital-mode app).

## Platform

- **Linux / macOS buildability** — planned but not yet buildable (the
  Windows-binary DSP engine + win32 wire code). Standing rule: never imply
  in docs/UI that Linux/macOS runs today.

---

## Parked / bench-gated (not active work)

- **#156** — restart-after-hard-kill clean reopen (intermittent; parked).
- FM transmit is at a **complete stopping point** as of v0.7.0 (fixes #2,
  #3a, #3b shipped); any further FM tuning is operator-bench-driven, not a
  planned arc.

## Recently CLOSED (for reference — no longer open)

TX bring-up (SSB/AM/DSB/SAM/FM + native rack) · TX EQ (#50) / RX EQ (#59) ·
Combinator (#51) · Plate reverb (#52) · TX speech suite (#88) · Profiles
(#49) · TX monitor (#90) · SWR protect + drive cap (#169/#170a) · PHROT
(#109) · ATT-on-TX (#166) · TX power model + per-band PA calibration +
auto-tuning watts cap (v0.6.0) · CW transmit (#105) · CW RX decoder (#173)
+ A-series accuracy (v0.4.10) · CW macros (#176) · waterfall callsign ID
(#175) · CTUN (#174) · RIT/XIT RX1 · VAC1 (#158/#102) · COM-port CAT + PTT
(v0.4.11) · Spots subsystem (#182, v0.5.0) · Tuner manual-ATU memory
(v0.7.0) · FM transmit refinements (v0.7.0).
