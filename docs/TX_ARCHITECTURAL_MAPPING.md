# TX Architectural Mapping — Lyra Does as the Reference Does

**Project:** lyra-cpp (C++23 / Qt6 / Vulkan / WDSP cffi)
**Status:** Phase 0 draft — operator sign-off pending
**Operator:** Rick Langford (N8SDR)
**Implementor:** Claude (Anthropic)
**Started:** 2026-06-04
**Reference:** working open-source GPL-v3+ SDR codebase, operator-
local path.  All file:line citations below are relative to the
reference source tree (e.g. `network.h:56-291`, `networkproto1.c:
614-1267`).  Per Rule 2 the reference application's name is kept
out of shipped code, code comments, commit messages, operator-
visible UI strings — and out of these tracked docs as well
(clarified 2026-06-05).
**Governing rules:** `docs/RULES.md` (22 rules, local-only)

---

## Scope of this document

This is the architectural spec for the new lyra-cpp TX path. The existing
TX code is being archived to `tx-rip-archive` and deleted from `main`
(Rule 7); this document is the build sheet for what gets written in its
place. Lyra mirrors the reference's architecture and measurable behaviour
(Rule 1); variations require explicit Rick + Claude agreement BEFORE code
(Rule 11) and are logged in §11 of this document.

C# → C++23 / Win32 → Qt6 / `Thread` → `QThread`-or-`std::thread` are
**language ports**, not deviations. A deviation is something where the
*architecture* or *behaviour* differs — not the syntax. Section 11 logs
the latter only.

This document is the synthesis. The raw reading notes (file:line citations,
verbatim observations from the reference) live in `docs/refs/`. The
mapping doc references them; the doc itself never duplicates them.

---

## 1. Overview & Scope

This document is the architectural specification for the new
lyra-cpp TX path. Phase 0 of the TX rip-and-port arc (task #112).

**The rip's core thesis:** the reference's TX architecture is one
HL2 hardware clock driving N producer-paced semaphore-blocked
threads and one wire-send thread on a two-event AND wait. NO
polling. NO `Thread.Sleep(N)` on the audio path. The 32-stage
WDSP TXA chain (xtxa) processes everything in-place on midbuff;
bp0 is the always-on SSB sideband selector; ALC is the always-on
splatter protection; sip1 is the TX panadapter siphon (`TXASetSipDisplay`), not the calcc feed (`pscc` from radio IQ).

Lyra mirrors this verbatim (Rule 1). Variations require explicit
Rick + Claude agreement BEFORE any code is written (Rule 11) and
are logged in §11. No silent variations.

**In scope (this document):**

- The reference's full TX path end-to-end, mic input → wire (§3-§9).
- The TCI TX-audio side — the FT8-symptom origin (§7).
- The MOX/PTT state machine + keydown/keyup ordering (§5.1, §5.2, §5.3).
- ATT-on-TX policy + the HL2 `31-x` wire encoding (§5.5).
- TR sequencing delays (§5.6).
- DDC routing on (MOX, PS_armed) state product (§5.4; sub-read pending).
- PureSignal forward-compat hooks — sip1 tap, calcc thread, iqc
  application stage (§9). PS code lands in v0.3; topology built to
  receive it without rework (Rule 10).
- The new file structure that implements all of the above (§10).

**Out of scope (this document):**

- RX path. RX is solid (Rule 8), untouched by the rip.
- HL2-specific quirks beyond what the TX path requires.
- Operator-facing UI design — that's task-list level work post-Phase 2.

**Key Phase 0 findings (each cites the reference file:line):**

1. **The reference TX service thread is `cm_main`, semaphore-blocked.**
   `cmbuffs.c:151-168`. Producer-paced by the HL2 wire clock via
   EP6 mic samples reaching `Inbound(tx_stream_id, ...)`
   (`networkproto1.c:413`). The prior "every 2 ms" Lyra framing was
   wrong; the only 2 ms tick is a supervisory C# thread for TCI
   protocol (§7).

2. **TXA chain order is locked at 32 stages.** `TXA.c:557-592`. bp0
   is always-on; bp1 is NEVER configured by Lyra (the §15.23 trap
   was calling `SetTXABandpassRun` thinking it toggled bp0); sip1
   is always-on for PS forward-compat.

3. **HL2 PA enable = case-10 C2 bit 3** (`ApolloTuner = 0x08`).
   `networkproto1.c:1079-1080`. Confirms the gateware RTL finding;
   Lyra commit `cbba63a` already correct.

4. **HL2 step-attn wire encoding = `31 - signed_dB`.**
   `console.cs:19165`. Lyra commit `73a459b` already correct.

5. **Three I/Q layouts feed pcm->in[stream]** (§11 structural
   detail #1): HL2 codec mic (I=mic, Q=0), TCI mono (I=Q=mono),
   TCI stereo (after L+R averaging → I=Q=mono). All work because
   TXA's `panel` stage uses `inselect=2` and reads only I.

6. **The keyup sequence requires the wire MOX bit to clear ONLY
   AFTER the TX-DSP blocking flush + mox_delay + ptt_out_delay.**
   `console.cs:30350-30407`. The "bristle broom sweep" Lyra hit
   was the symptom of not following this order; commit `4ce07b9`
   now correct.

**Status:** Phase 0 first-pass complete. End-to-end mapping +
detailed reference notes in `docs/refs/` (5 files).
Pending operator sign-off (§13). After sign-off, Phase 1 begins
(archive + delete + create empty file skeleton per §10).

**Document navigation:**
- §2 — single-page top-level diagram (the picture)
- §3-9 — per-subsystem detail
- §10 — the implementation map (the file list Phase 1 creates)
- §11-12 — deviations + open questions log
- §13 — sign-off

---

## 2. Top-Level Architecture

The reference's entire TX path, in one diagram. Each box is a
component; each arrow is data flow with the buffer and thread
boundary named. Lyra mirrors this verbatim (Rule 1).

```
                                ╔═══════════════════════════════════════╗
                                ║      HL2+ hardware (one clock)        ║
                                ║   48 kHz crystal, AK4951 codec,       ║
                                ║   AD9866 ADC/DAC, gateware            ║
                                ╚═══════════╤═════════════════════╤═════╝
                                            │ EP6 UDP             │ EP2 UDP
                                            │ (RX1 IQ + RX2 IQ    │ (TX I/Q + LR audio)
                                            │  + mic + telemetry) │ ▲
                                            ▼                     │
              ┌────────────────────────────────────────┐           │
              │ MetisReadThreadMain (wire-recv)        │           │
              │ netInterface.c:61                      │           │
              │ MMCSS Pro Audio                        │           │
              │ recvfrom + parse each EP6 frame:       │           │
              │   Inbound(RX1, samples) → RX1 cmbuffs  │           │
              │   Inbound(RX2, samples) → RX2 cmbuffs  │           │
              │   Inbound(TX,  mic[i]) → TX  cmbuffs   │           │
              └────────────────────────────────────────┘           │
                                            │ ┌───────────────┐   │
              ┌─────────────────┐           │ │ TCI client    │   │
              │ TCI listener    │           │ │ (MSHV/JTDX)   │   │
              │ (per WebSocket  │           │ └──┬─────▲──────┘   │
              │  client)        │           │    │     │ TX_CHRONO│
              │ TCIServer.cs    │◄─────────────┘    │     │          │
              │ handleBinaryFr. │  TX_AUDIO_STREAM  │     │          │
              │  → m_txAudioQ   │                   │     │          │
              └───┬─────────────┘                   │     │          │
                  │ m_txAudioQueue (drop-OLDEST)    │     │          │
                  ▼                                 │     │          │
              ┌─────────────────────────────────────┴─────┴───┐     │
              │ TCITxThreadProc (C# supervisory thread)       │     │
              │ cmaster.cs:1243; AboveNormal; 2 ms poll       │     │
              │ ServiceTCITxProtocol:                          │     │
              │   dequeue m_txAudioQ → queueTciTxAudio:        │     │
              │     L+R/L/R per stereoMode → mono              │     │
              │     resample → m_tciTxSampleQueue              │     │
              │   compute CHRONO pull formula                  │     │
              │   send SendTxChrono frames (bounded)           │     │
              └─────────────────────────────────────────────────┘     │
                                                                      │
       ┌──────────────────┬────────────────────┐                     │
       ▼                  ▼                    ▼                     │
  ┌─────────┐        ┌─────────┐         ┌──────────────────┐        │
  │ RX1 cmb │        │ RX2 cmb │         │ TX cmbuffs ring  │        │
  │ ring +  │        │ ring +  │         │ Sem_BuffReady    │        │
  │ Sem.    │        │ Sem.    │         │ producer-paced   │        │
  └────┬────┘        └────┬────┘         └────────┬─────────┘        │
       │                  │                       │ wake             │
       │ wake             │ wake                  │                  │
       ▼                  ▼                       ▼                  │
  ┌────────────┐  ┌────────────┐         ┌──────────────────────┐   │
  │ cm_main RX1│  │ cm_main RX2│         │ cm_main TX           │   │
  │ MMCSS Pro  │  │ MMCSS Pro  │         │ MMCSS Pro Audio @ 2  │   │
  │ Audio @ 2  │  │ Audio @ 2  │         │ semaphore-blocked,   │   │
  │ blocked    │  │ blocked    │         │ INFINITE             │   │
  │ on Sem.    │  │ on Sem.    │         │ ┌──────────────────┐ │   │
  │            │  │            │         │ │ xcmaster case 1: │ │   │
  │ xcmaster   │  │ xcmaster   │         │ │  asioIN (no-op   │ │   │
  │ case 0:    │  │ case 0:    │         │ │   for HL2)       │ │   │
  │  RXA       │  │  RXA       │         │ │  if use_tci_aud: │ │   │
  │  → aamix   │  │  → aamix   │         │ │   InboundTCITx…  │ │   │
  └────┬───────┘  └────┬───────┘         │ │     (writes I=Q  │ │   │
       │               │                 │ │      =mono into  │ │   │
       └───┬───────────┘                 │ │      pcm->in)    │ │   │
           ▼                             │ │  fexchange0      │ │   │
       ┌──────────────────┐              │ │   ↓              │ │   │
       │ Global aamix     │              │ │  WDSP TXA chain  │ │   │
       │ Outbound:        │              │ │  (32 stages,     │ │   │
       │  LR audio →      │              │ │   §6) — runs     │ │   │
       │  outLRbufp       │              │ │   internally     │ │   │
       │  signal LR event │              │ │   on midbuff     │ │   │
       └────────┬─────────┘              │ │  xilv → Outbound │ │   │
                │                        │ └─────┬────────────┘ │   │
                │                        └───────┼──────────────┘   │
                │                                │ IQ event          │
                ▼                                ▼                   │
       ┌─────────────────────────────────────────────────────┐      │
       │ sendProtocol1Samples (wire-send thread)             │      │
       │ networkproto1.c:1204; MMCSS Pro Audio @ 2           │      │
       │ WaitForMultipleObjects(2, LR+IQ, AND, INFINITE)     │      │
       │ if (!XmitBit) memset(outIQ, 0, …)  ← MOX gate       │      │
       │ CW state bits in TX I-LSBs if cw_enable             │      │
       │ Quantize float → int16 round-to-nearest             │      │
       │ Per USB frame:                                       │      │
       │   sync bytes + C0 (MOX bit) + C&C round-robin        │      │
       │   (0-18) + 504 byte LRIQ payload                    │      │
       │ MetisWriteFrame(0x02, outbuf)                       │──────┘
       └─────────────────────────────────────────────────────┘
                  Wire-send thread → UDP EP2 → HL2 → antenna
```

**One HL2 clock. One wire-recv thread. N cm_main consumers
(one per stream). Three TCI threads (Server WebSocket I/O,
TCI RX supervisory, TCI TX supervisory). One wire-send thread.
Zero polling on the audio path. Zero `Thread.Sleep(N)` on the
audio path.**

The Lyra rip mirrors this exactly. Section 10 lists the new
file structure that implements it.

---

## 3. Threading Model

**Reading status:** `cmaster.c` ✓, `cmbuffs.c` ✓; `cmasio.c` pending,
`netInterface.c` pending, `TCIServer.cs` pending.

**Architecture in one line:** the reference uses N+ dedicated threads,
each MMCSS "Pro Audio" priority where it matters, communicating through
counting semaphores and ring buffers. **NO polling, NO `Thread.Sleep(N)`
busy loops on the DSP path.** (This was a misunderstanding in Lyra's
prior TX work and the reason the patches all failed.)

### 3.1 Thread inventory (TX-path-relevant)

| Thread | Reference spawn | Priority | Wake mechanism | Cadence | Lyra-native target |
|---|---|---|---|---|---|
| `cm_main` per stream (1× RX1, 1× RX2, 1× TX, ...) | `cmbuffs.c:31` `_beginthread(cm_main, 0, (void*)id)` from `create_cmbuffs` | MMCSS "Pro Audio" priority 2 (cmbuffs.c:154-156) | `WaitForSingleObject(Sem_BuffReady, INFINITE)` (cmbuffs.c:163) | producer-paced | one `std::thread` per stream, MMCSS-pinned on Windows |
| `MetisReadThreadMain` | `netInterface.c:61` `_beginthreadex(NULL, 0, MetisReadThreadMain, ...)` | TBD | TBD | EP6 socket recv | one dedicated wire-recv thread, `recvfrom` loop |
| `sendProtocol1Samples` | `netInterface.c:66` `_beginthreadex(NULL, 0, sendProtocol1Samples, ...)` | TBD | TBD | TBD | one dedicated wire-send thread, EP2 packer + `sendto` |
| `KeepAliveMain` | `netInterface.c:83` (P2 only — not HL2 P1) | TBD | TBD | TBD | TBD |

Full per-thread detail in `docs/refs/cm_thread_mechanism.md`.

### 3.2 The TX cm_main thread — the load-bearing one

This is the thread that runs the WDSP TXA chain for the TX stream.

- **Spawned per-stream** by `create_cmbuffs(id, ...)` (cmbuffs.c:35-58),
  itself called by `create_cmaster()` (cmaster.c:273) inside a
  `for i = 0 to cmSTREAM` loop. So there's a cm_main per RX1, RX2,
  TX, etc. — typically 3 cm_main threads for an HL2 dual-RX-plus-TX
  setup.
- **Loop body** (cmbuffs.c:161-167):
  1. `WaitForSingleObject(Sem_BuffReady, INFINITE)` — blocks
  2. `cmdata(id, pcm->in[id])` — drains the ring into the stream's
     input buffer
  3. `xcmaster(id)` — runs the stream's DSP pipeline (the per-stream
     dispatch at cmaster.c:339-405)
- **Producer side** (`Inbound()` at cmbuffs.c:88-121): callers memcpy
  N samples into the ring; when accumulated samples cross the
  `r1_outsize` threshold, `ReleaseSemaphore(Sem, n, ...)` releases
  N counts. The cm_main thread then processes N output-blocks back
  to back.
- **TX DSP pipeline** (xcmaster case 1, cmaster.c:377-398):

  ```
  asioIN          ← ASIO mic samples (overwritten by TCI if use_tci_audio)
  ↓
  xpipe           ← wave recorder/player tap
  xdexp           ← VOX (DEXP)
  fexchange0      ← WDSP TXA — the actual DSP, 96 kHz internal rate
  xsidetone       ← sidetone (CW)
  xpipe           ← second wave tap
  xMixAudio       ← monitor audio
  xtxgain         ← Penelope gain + amp protect
  xeer            ← EER transmission
  xilv            ← interleave, call Outbound() → wire send thread
  ```

  See `docs/refs/cm_thread_mechanism.md` for line-by-line citations.

- **Block param 13 to OpenChannel for the TX channel is 1** (block
  until output available), per cmaster.c:190 — this is the parameter
  Lyra previously had wrong.

### 3.3 The critical correction

Earlier work in this project (CLAUDE.md §15.18 + the now-superseded
audit dossier) referenced the reference's "TX service loop with
`Thread.Sleep(2)`". **This is wrong.** The reference's TX service
thread is `cm_main`, blocked on `WaitForSingleObject(..., INFINITE)`.
It runs producer-paced, not on a 2 ms timer. The mapping doc, all
new Lyra code, and any task descriptions citing the old framing are
corrected here.

The `Thread.Sleep(2)` reference may still exist somewhere in the
reference (TCIServer.cs CHRONO pull loop is a likely candidate) —
parked as Open Question #3 in §12.

### 3.4 Lyra-native threading model (first-pass)

Subject to revision after `cmasio.c` and `netInterface.c` reads.

- **N cm_main equivalents** — one C++ `std::thread` per stream:
  `RxThread1`, `RxThread2`, `TxThread`. Each owns its own input
  buffer, its own SPSC ring (producer release count → consumer wake),
  and runs a per-stream DSP pipeline. MMCSS "Pro Audio" priority 2
  via `AvSetMmThreadCharacteristics` on Windows.
- **WireRecvThread** — `recvfrom` loop on EP6 socket, dispatches
  samples per-DDC to the cm threads via their `Inbound()`-equivalent.
- **WireSendThread** — pace TBD until `sendProtocol1Samples` read
  completes; will own EP2 packing + `sendto`.
- **Qt main thread** — UI only. Signals/slots only. **Never blocks
  on audio.**

Critical-section equivalent for the per-stream config-vs-DSP race
(`pcm->update[stream]` at cmaster.c:343 in the reference): a per-stream
`std::mutex update_;` held briefly by the DSP thread at the top of
its loop iteration, and held by any setter coming from Qt main that
mutates the stream's config. Same pattern as the reference.

### 3.5 Wake mechanism — why semaphore beats timer

The reference's semaphore-blocked design has three properties Lyra
needs:

1. **Zero CPU when idle.** No samples → no wake → no cycles.
   Timer-poll wastes CPU.
2. **Producer-paced.** The DSP thread runs at exactly the rate samples
   arrive — no drift between producer and consumer clocks. Timer-poll
   creates two independent clocks that need rate-matching.
3. **MMCSS interaction.** A semaphore-blocked thread released by a
   non-MMCSS producer still gets the MMCSS dispatch boost when it
   wakes; a timer-polled thread has to compete for its wakeups against
   the scheduler.

A `std::counting_semaphore<>` (C++20) gives Lyra the same primitive
in standard form. On Windows, MMCSS via `AvSetMmThreadCharacteristics`
is wrapped in a small RAII helper.

### 3.6 Closed open issues (resolved by `cmasio.c` + `networkproto1.c` reads)

**Closed: What releases the TX cmbuffs semaphore?**
**Answer:** the wire-recv thread (`MetisReadThreadMain` →
`MetisReadThreadMainLoop_HL2`) calls `Inbound(inid(1, 0), N,
TxReadBufp)` at networkproto1.c:413 after extracting mic samples
from each EP6 frame. The HL2+ AK4951 codec sends mic samples back
to the host alongside the RX IQ in every EP6 frame; the wire-recv
thread parses them and feeds the TX cmbuffs. **The HL2 hardware
clock IS what paces the TX cm_main thread**, via the wire. Not
ASIO. Not a timer. Not a sleep.

(cmasio.c is the ASIO-codec path — used when `audioCodecId != HERMES`.
For HL2 operators it's a no-op: `asioIN()` early-returns when
`audioCodecId != ASIO`.)

**Closed: EP2 wire-send cadence.**
**Answer:** `sendProtocol1Samples` (networkproto1.c:1204-1267) is
MMCSS Pro Audio priority 2, body is
`WaitForMultipleObjects(2, hsendEventHandles, TRUE, INFINITE)` —
two-event AND wait, infinite timeout. Wakes when BOTH the LR audio
producer (global aamix Outbound) AND the TX I/Q producer (TX
channel xilv → OutboundTx) have signaled. At HL2 48 kHz with 126
samples per EP2 frame, advances every 2.625 ms — but it's
*producer-paced*, not timer-paced. The "every 2 ms" notion in
earlier Lyra docs traces to this 2.625 ms cadence — but it's the
HL2 wire clock, not a software timer.

### 3.7 Full reference threading model (closed)

```
                  ┌──────────────────────────────────────────────────────┐
                  │  HL2 hardware: 48 kHz crystal, AK4951 codec, EP6+EP2 │
                  └────────────┬─────────────────────────────────────────┘
                               │ UDP EP6 datagrams (RX1 IQ + RX2 IQ + mic)
                               ▼
                  ┌──────────────────────────────────┐
                  │  MetisReadThreadMain (wire-recv) │
                  │  netInterface.c:61               │
                  └────────┬─────────┬──────────────┬┘
                  Inbound(RX1)  Inbound(RX2)  Inbound(TX) ← mic from EP6!
                           ▼         ▼               ▼
                  ┌─────────┐  ┌─────────┐  ┌─────────┐
                  │ RX1 cmb │  │ RX2 cmb │  │ TX cmb  │  (cmbuffs ring + semaphore)
                  └────┬────┘  └────┬────┘  └────┬────┘
                       │ wake       │ wake        │ wake
                       ▼            ▼             ▼
                  ┌─────────┐  ┌─────────┐  ┌─────────────┐
                  │ cm_main │  │ cm_main │  │  cm_main TX │  (MMCSS Pro Audio @2)
                  │ RX1     │  │ RX2     │  │  xcmaster   │
                  │ xcmaster│  │ xcmaster│  │  case 1     │
                  │ ↓       │  │ ↓       │  │  ↓ TXA      │
                  │ RXA     │  │ RXA     │  │  ↓ xilv     │
                  │ ↓       │  │ ↓       │  │  ↓ Outbound │──┐
                  │ aamix   │  │ aamix   │  └─────────────┘  │
                  └────┬────┘  └────┬────┘                   │ IQ event
                       │            │                        │
                       └────┬───────┘                        │
                            ▼                                │
                  ┌───────────────────┐                      │
                  │  Global aamix     │ LR event             │
                  │  Outbound         │──────────────┐       │
                  └───────────────────┘              │       │
                                                    ▼       ▼
                  ┌─────────────────────────────────────────────┐
                  │  sendProtocol1Samples (wire-send)          │
                  │  WaitForMultipleObjects(LR, IQ, AND, ∞)    │
                  │  MOX-gate IQ to zero if !XmitBit           │
                  │  CW state bits in TX I-LSBs if cw_enable   │
                  │  Quantize float→int16                      │
                  │  WriteMainLoop_HL2(outbuf)                 │
                  └────────────────────────┬────────────────────┘
                                           │ UDP EP2 datagram
                                           ▼
                                  HL2 hardware: TX wire
```

**One HL2 hardware clock**, **one wire-recv thread + N stream
producers**, **N cm_main consumer threads**, **one wire-send thread
driven by AND of two events**. No polling. No sleep. No timer.
Just semaphores and Win32 events.

### 3.8 Lyra-native threading model (closed, first pass)

| Reference thread | Lyra-native target |
|---|---|
| `MetisReadThreadMain` (1×) | `lyra::wire::EP6RecvThread` — `std::thread` on a `recvfrom` loop. Parses each EP6 datagram, dispatches to per-stream rings. MMCSS Pro Audio. |
| `cm_main` per stream (3× — RX1, RX2, TX) | `lyra::dsp::StreamThread` × 3 instances — `std::thread` each, blocked on `std::counting_semaphore<>` released by EP6RecvThread. MMCSS Pro Audio. |
| `sendProtocol1Samples` (1×) | `lyra::wire::EP2SendThread` — `std::thread`, blocked on two-condition wait (Win32 `WaitForMultipleObjects` equivalent: `std::condition_variable` with two flags, AND-wait). MMCSS Pro Audio. |
| Qt main | `QApplication::exec()` — UI only, signals/slots only, NEVER blocks on audio. |

Per-stream ring buffer: a `SpscRing<complex<double>, N>` matching
the reference's cmbuffs layout (single producer = EP6RecvThread,
single consumer = StreamThread). Capacity = N × output-block-size
where N is the reference's `CMB_MULT` (to be measured next read).

Per-stream `update_mutex` matches the reference's `pcm->update[stream]`
critical section — Qt main thread acquires it briefly when mutating
stream config; StreamThread acquires at the top of each iteration.

### 3.9 Remaining open issues

**Open: Where does the `Thread.Sleep(2)` reference live?**
Not in cmaster.c, not in cmbuffs.c, not in cmasio.c, not in
`sendProtocol1Samples`. Possible locations: TCIServer.cs CHRONO
pull loop; possibly nowhere. Verify in §7 (TCI) read.

**Open: TCI inbound audio format vs HL2 mic format.**
HL2 codec mic at networkproto1.c:404-407 writes I=mic, Q=0. The
reference task #67 framing claims "match the reference I=Q=mono"
— this contradicts the file:line. Verify in TCIServer.cs read by
finding
where TCI binary frames get unpacked and written into the buffer
that `InboundTCITxAudio` exposes.

Full provenance in `docs/refs/wire_threads_and_mic_producer.md`.

---

## 4. Queue / Buffer Topology

Captured from the §3 + §7 reads. The reference uses six logical
buffer/queue surfaces in the TX path; the dual-queue model for TCI
gets its own subsection.

### 4.1 Per-stream cmbuffs ring (the cm_main wake mechanism)

| | |
|---|---|
| Name | `cmbuffs` / per-stream `r1_baseptr` ring |
| File:line allocated | `cmbuffs.c:50` |
| Element type | `double[2]` complex (interleaved I/Q at stream's input rate) |
| Capacity | `CMB_MULT × r1_size` samples (CMB_MULT not yet measured; likely small N) |
| Producer | `MetisReadThreadMain` → `Inbound(stream_id, n, samples)` |
| Consumer | `cm_main` for that stream (one thread per stream) |
| Wake mechanism | counting semaphore `Sem_BuffReady` (init 0, max 1000); ReleaseSemaphore on producer side when `r1_unqueuedsamps >= r1_outsize`; consumer `WaitForSingleObject(..., INFINITE)` |
| Overrun policy | not enforced — the wraparound at line 117-118 means stale samples get overwritten if producer outruns consumer; in practice this never happens because the consumer wakes on every release |
| Underrun policy | N/A — consumer blocks indefinitely if no samples |
| Lyra-native target | `lyra::dsp::StreamRing<complex64>` per stream — SPSC, capacity CMB_MULT × outsize, kernel semaphore via `std::counting_semaphore<>` |

There is **one such ring per stream** — RX1, RX2, TX, etc. For
the TX stream specifically, the producer puts HL2 codec mic
samples (read out of every EP6 frame at networkproto1.c:413).

### 4.2 Per-stream pcm->in[stream] input buffer

| | |
|---|---|
| Name | `pcm->in[stream]` |
| File:line allocated | `cmaster.c:285` |
| Element type | `double[2]` complex (interleaved I/Q at stream's input rate) |
| Capacity | `getbuffsize(pcm->cmMAXInRate) * sizeof(complex)` — sized for max stream input rate |
| Producer | `cmdata(id, pcm->in[id])` called from `cm_main` (drains cmbuffs into this buffer) |
| Consumer | `xcmaster(stream)` — for TX, immediately overwritten by `asioIN()` or `InboundTCITxAudio` per use_tci_audio |
| Wake | not a queue — single-buffer handoff inside the cm_main thread iteration |
| Lyra-native target | Per-stream `std::vector<complex64> in_buf_` — allocated at construction; reused per iteration |

### 4.3 TXA internal buffers (in-channel DSP buffers)

| | File:line | Size | Role |
|---|---|---|---|
| `txa[channel].inbuff` | TXA.c:36 | `1 × ch[channel].dsp_insize` complex | DSP input — written by fexchange0 receive side |
| `txa[channel].midbuff` | TXA.c:38 | `2 × ch[channel].dsp_size` complex | **In-place chain buffer** — every TXA stage reads + writes midbuff |
| `txa[channel].outbuff` | TXA.c:37 | `1 × ch[channel].dsp_outsize` complex | DSP output — read by fexchange0 send side |

These are private to the WDSP TXA implementation. Lyra accesses
them only via cffi `fexchange0(...)`.

### 4.4 obbuffs rings (outbound, wire-send side)

| | |
|---|---|
| Name | `prn->hobbuffsRun[0]` (LR audio) + `prn->hobbuffsRun[1]` (TX IQ) |
| File:line | `obbuffs.c:61` (each) |
| Element type | int16 stereo pairs (post-quantize) |
| Producer side | global aamix Outbound (LR) + xilv→OutboundTx (IQ) |
| Consumer | `sendProtocol1Samples` (wire-send thread) |
| Wake mechanism | two Win32 events ANDed via `WaitForMultipleObjects(2, hsendEventHandles, TRUE, INFINITE)` (networkproto1.c:1220); producers signal their event when frame ready; consumer wakes when BOTH ready |
| Lyra-native target | `lyra::wire::OutboundLrRing` + `lyra::wire::OutboundIqRing` (companion SPSC rings) + a `std::condition_variable` AND-wait on the consumer side |

### 4.5 TCI m_txAudioQueue (per-listener raw stereo)

| | |
|---|---|
| Name | `m_txAudioQueue` |
| File:line | TCIServer.cs (per-TciListener) |
| Element type | `TCIQueuedTxAudio` (encapsulates complex `double[]` at client's sample rate + channels + sampleType) |
| Producer | `handleBinaryFrame()` on receipt of TX_AUDIO_STREAM binary frame (over WebSocket) |
| Consumer | `TCITxThreadProc` via `TryDequeueTxAudio` (cmaster.cs:1299) |
| Capacity | bounded by `MAX_TX_AUDIO_QUEUE_BLOCKS` and `MAX_TX_AUDIO_QUEUE_COMPLEX_SAMPLES` (constants — values pending follow-up read) |
| Overrun policy | **drop-OLDEST** — `Dequeue()` from front to make room (TCIServer.cs:5689-5702) |
| Lyra-native target | `std::deque<TciQueuedTxAudio>` per-listener with `std::mutex`, capacity-enforced drop-oldest |

### 4.6 TCI m_tciTxSampleQueue (global, mono float at TXA input rate)

| | |
|---|---|
| Name | `m_tciTxSampleQueue` |
| File:line | cmaster.cs (global, in C# bridge layer) |
| Element type | `float[]` mono (rate-converted to `m_tciTxInputRate`, typically 48000) |
| Producer | `queueTCITxAudio()` (cmaster.cs:1382) — averages stereo to mono per `TXStereoInputMode` + WDSP `xresampleFV` resample |
| Consumer | `OnTCITxAudioInSamples(int nsamples, double* data)` (cmaster.cs:1774) — called by C++ cm_main TX via `InboundTCITxAudio` cffi |
| Capacity | unbounded (Queue<float[]>) — backpressure via the CHRONO pull formula keeps it within `targetQueuedSamples` |
| Overrun policy | none required (producer rate is bounded by chrono request rate) |
| Underrun policy | **zero-fill** in `OnTCITxAudioInSamples` (cmaster.cs:1806-1810) |
| Lyra-native target | `std::deque<std::vector<float>>` global with `std::mutex`, paired with offset state for partial reads |

### 4.7 sip1 ring (PureSignal TX I/Q tap)

| | |
|---|---|
| Name | `sip1` siphon buffer |
| File:line | `TXA.c:394-403 create_siphon(...)` |
| Element type | complex (TX I/Q at DSP rate, post-everything pre-output-resample) |
| Capacity | 16384 samples |
| Producer | xtxa step 28 (`xsiphon(sip1, 0)` at line 586) — copies midbuff into ring |
| Consumer | calcc thread (v0.3; inert in v0.2) |
| Wake | not a queue — circular buffer always-being-written |
| Lyra-native target | `lyra::wdsp::SipRing` (cffi wraps WDSP's siphon — no Lyra ring needed; just don't disable sip1) |

### 4.8 Summary table

```
                      Producer                Consumer            Wake
─────────────────────────────────────────────────────────────────────────────────
cmbuffs (per stream)  MetisRead (EP6 mic)     cm_main (stream)    Semaphore
pcm->in[stream]       cm_main (drain)         xcmaster (immediate) (none — same thread)
TXA inbuff/midbuff/   fexchange0              xtxa stages         (none — same thread)
  outbuff
obbuffs LR + IQ       aamix/xilv Outbound     sendProtocol1Sam.   Two Win32 events AND
m_txAudioQueue (TCI)  handleBinaryFrame       TCITxThreadProc     2 ms poll (supervisory)
m_tciTxSampleQueue    queueTCITxAudio         OnTCITxAudioInSamp. (called sync from cm_main)
sip1 (PS)             xsiphon (per tick)      calcc thread (v0.3) (calcc thread's own sema)
```

**Wake mechanism inventory:**
- Kernel semaphores (counting): cmbuffs `Sem_BuffReady` (one per
  stream); presumably obbuffs has similar
- Win32 events ANDed: `sendProtocol1Samples`'s two-event wait
- 2 ms poll: ONLY the C# supervisory `TCITxThreadProc` —
  not on any audio path
- Same-thread handoff (no wake): in-thread buffer reuse

**Zero `Thread.Sleep(N)` on the audio path. Zero polling on the
audio path. Every audio thread blocks on a kernel object with
INFINITE timeout until the producer says go.**

---

## 5. State Machines

**Reading status:** `console.cs:30058-30420` (the MOX FSM) ✓;
`console.cs:19140-19200` (ATT-on-TX setter) ✓;
`UpdateDDCs` / `HdwMOXChanged` / `AudioMOXChanged` / `UpdateAAudioMixerStates`
pending sub-reads. Full reference notes in
`docs/refs/mox_fsm_and_wire_frame.md`.

### 5.1 MOX/PTT FSM — single funnel

`console.cs:30058 chkMOX_CheckedChanged2(object sender, EventArgs e)`
is **the single funnel through which every MOX transition flows**.
SW MOX, HW PTT, CAT, TCI all set `chkMOX.Checked`, which fires this
handler. There is no parallel keydown/keyup logic per input source —
a single state is shared.

**Lyra-native:** `lyra::tx::PttFsm` with a `PttSource` enum
{SW_MOX, HW_PTT, CAT, TCI, VOX, CW_KEY, TUN}, the FSM holds a
`set<PttSource>`, a resolver maps the held set → exactly one state.
Resolver precedence: MOX_TX > CW_TX > VOX_TX > TUN_TX > RX. Sources
share state (not OR'd at the wire bit level — the wire MOX bit is
touched exactly twice per transmission, by the resolver's edge
detector).

### 5.2 Keydown sequence (RX → TX) — exact reference order

Per `console.cs:30058-30348` (full source dump in `docs/refs/`):

```
1. SendHighPriority(1)                  immediate C&C frame push
2. _mox = true                          internal flag, used downstream
3. compute TX freq                      VFO + XIT + out-of-band guards + CW pitch offset
4. pause display thread                 no artifact flashes
5. SetChannelState(RX1, OFF, 1)         BLOCKING flush — drains RX downslew
   SetChannelState(RX2, OFF, 1) (cond)  same for RX2 if shutdown-on-TX policy
6. m_bATTonTX path                      force tx-att-for-band (or 31 if PS-off + !ATU-bypass + non-CW)
                                        SetupForm.ATTOnTX = txAtt → writes C&C
7. UpdateAAudioMixerStates()            flip AAmixer routing TX-active
8. UpdateDDCs(rx2_enabled)              flip DDC routing (MOX, PS_armed product)
9. HdwMOXChanged(tx, freq)              writes wire MOX bit + TX freq registers
10. Display.MOX = tx
11. psform.Mox = tx
12. cmaster.Mox = tx                    router bit
[non-CW path:]
13a. Sleep(rf_delay)                    settle — amp hot-switch protection
14a. AudioMOXChanged(tx)                audio.cs MOX = true
15a. SetChannelState(TX, ON, 0)         TX DSP start (non-blocking; WDSP cos² up-ramp)
[CW path:]
13b. AudioMOXChanged(tx)                CW handles its own key state via separate path
```

**Critical invariants per file:line:**
- RX DSP stop is `dmode=1` (blocking flush) — `console.cs:30281`, `:30289`
- RX-DSP-stop happens BEFORE wire MOX bit set (so RX filters see no TX-coupled IQ)
- TX-freq written INSIDE `HdwMOXChanged(tx, freq)` before TX-DSP start
- `rf_delay` ONLY between `HdwMOXChanged` and TX-DSP-start (non-CW only)
- TX channel starts with `dmode=0` (non-blocking; WDSP's cos² ramp handles the soft-start internally)

### 5.3 Keyup sequence (TX → RX) — exact reference order

Per `console.cs:30350-30407`:

```
1. Sleep(space_mox_delay)              default 0 (export: 13)
2. _mox = false                        internal flag
3. psform.Mox = tx                     PS form sees keyup
4. SetChannelState(TX, OFF, 1)         BLOCKING flush — TX cos² down-ramp completes,
                                        faded TX I/Q tail still going to wire (MOX still on)
[non-CW path:]
5a. Sleep(mox_delay)                   default 10 (export: 15); in-flight samples drain
[CW path:]
5b. Sleep(key_up_delay) if !cw_fw_keyer  default 0 (export: 10)
6. UpdateDDCs(rx2_enabled)              flip DDC routing back to RX
7. UpdateAAudioMixerStates()            flip AAmixer back to RX
8. AudioMOXChanged(tx)                  audio.cs MOX = false
9. HdwMOXChanged(tx, freq)              CLEARS wire MOX bit + restores RX freq
10. Display.MOX = tx
11. cmaster.Mox = tx                    router bit cleared
12. Sleep(ptt_out_delay)                default 0 (export: 5); HW T/R relay switches
13. SetChannelState(RX1, ON, 0)         RX1 DSP restart (after relay settled)
    SetChannelState(RX2, ON, 0) (cond)  RX2 DSP restart
    SetChannelState(SUB, ON, 0) (cond)  sub-RX restart
14. HL2: AutoTuningHL2(Idle)            stop auto-tune
15. ATT-on-TX restore                   updateAttNudsCombos() → RX1/RX2 step-attn from saved values
```

**Critical invariants per file:line:**
- **TX DSP stop is blocking** (`dmode=1`) — `console.cs:30357` — the wire MOX bit STAYS ON during the cos² downslew flush; the faded tail completes on the wire
- **The wire MOX bit clears ONLY AFTER the TX-DSP-stop returns + `mox_delay`** — never before
- **`ptt_out_delay` sits between MOX-bit-clear and RX-DSP-restart** — the T/R relay must physically switch before RX filters reopen, else they grind the transition
- **RX DSP restart is the LAST step** — `console.cs:30379-30383`

**This is the §15.25 / §15.26 keyup ordering, source-verified.**
Lyra commit `4ce07b9` (RX channel stop-on-keydown / restart-after-
ptt_out-settle) and `47ae18d` (non-blocking keydown RX-stop) align
with this. The "bristle broom sweep" Lyra hit was the symptom of
NOT following this ordering — earlier attempts to "fade audio" or
"reset AGC" were chasing the wrong layer.

### 5.4 DDC routing on (MOX, PS_armed) state product

Driven by `UpdateDDCs(rx2_enabled)` (called at console.cs:30330 on
keydown and 30370 on keyup). **Pending sub-read** — captured the
call sites but not the routing matrix yet. CLAUDE.md §3.8 + §15.26
have a working description; need source-verification of
`UpdateDDCs`.

Lyra-native target: `protocol::ddc_map(DispatchState state) →
RouteTable` where `DispatchState = {mox, ps_armed, rx2_enabled,
family}`. Same dispatch axes as CLAUDE.md §6.7 discipline #6.

### 5.5 ATT-on-TX policy + wire encoding (CLOSED)

`console.cs:19148-19178 m_bATTonTX` setter. Default value: **ON**
(line 19148: `private bool m_bATTonTX = true;`).

**Wire encoding (the §15.26 PS-entangled answer, source-verified):**

```cs
if (m_bATTonTX) {
    int txatt = getTXstepAttenuatorForBand(_tx_band);
    if (HL2) NetworkIO.SetTxAttenData(31 - txatt);    // <-- HL2 inversion
    else      NetworkIO.SetTxAttenData(txatt);
} else {
    NetworkIO.SetTxAttenData(0);
}
```

- **HL2: `wire = 31 − signed_dB`** (line 19165). Lyra commit
  `73a459b` already correct.
- Non-HL2: `wire = signed_dB` directly.

**Keydown force-31** (console.cs:30293-30327):
```cs
if (m_bATTonTX) {
    int txAtt = getTXstepAttenuatorForBand(_tx_band);
    if ((!chkFWCATUBypass.Checked && _forceATTwhenPSAoff) || isCW)
        txAtt = 31;            // force min RX gain (max attenuation in HL2's inverted axis)
    SetupForm.ATTOnRX1 = getRX1stepAttenuatorForBand(rx1_band);  // save RX1 value for restore
    SetupForm.ATTOnTX = txAtt;                                    // applies via the setter above
}
```

**Lyra-native:** `lyra::tx::AttOnTxPolicy` — operator-toggleable
master (default ON), per-band TX-att value (default 31 to force max
RX-ADC protection), keydown forces 31 in non-PS-armed mode + CW,
keyup restores saved per-band RX1 step-attn. Single writer:
`stream::set_tx_step_attn_db(signed_db)`. The `31-x` encoding lives
ONLY in the wire composer (`_compose_frame_4` C3 + `_compose_frame_11`
C4 MOX-gated TX branch) — v0.3 PureSignal will swap the SOURCE
(operator → PS auto-att FSM) without touching the encoding.

### 5.6 TR sequencing delays (CLOSED)

| Knob | Default | Operator export | Where applied | Purpose |
|---|---|---|---|---|
| `rf_delay` | varies | **50** | console.cs:30342-30343 (keydown, between HW-MOX-set and TX-DSP-start) | T/R relay settle for external amp hot-switch protection |
| `mox_delay` | 10 | **15** | console.cs:30367-30368 (keyup, between TX-DSP-stop and HdwMOXChanged) | In-flight samples drain |
| `space_mox_delay` | 0 | **13** | console.cs:30352-30353 (keyup, start of sequence) | Pre-keyup pause |
| `ptt_out_delay` | 0 (added wcp 2018-12-24) | **5** | console.cs:30377-30378 (keyup, between MOX-clear and RX-DSP-restart) | HW T/R switch time |
| `key_up_delay` | 0 | **10** | console.cs:30362-30363 (CW keyup only, if !cw_fw_keyer) | CW key-up settle |

Operator values come from the reference-app DB export on the
operator's local disk under `Y:\hold\screenshots\` (filename
preserved as-on-disk; not reproduced here per Rule 2).

**Lyra-native:** capability-driven `TrSequencing` struct in
`lyra::protocol::Capabilities` (currently exists per CLAUDE.md
§15.26 / commit `5c8e6b5`). Operator-tunable via Settings → TX → TR
Sequencing (already shipped in Lyra). The rip carries these forward
unchanged.

---

## 6. TXA (WDSP) Chain

**Reading status:** `wdsp/TXA.c:1-680` (constructor + destructor +
flush + `xtxa()` + rate setters) ✓. Per-stage setter functions
(SetTXAMode, SetTXAALCMaxGain, etc.) pending — capture as needed
during the Phase 2 per-component port.

Full reference notes in `docs/refs/txa_chain.md`.

### 6.1 The chain — `xtxa()` execution order

`wdsp/TXA.c:557-592` — **THIS is the canonical TX DSP order.**
Lyra's TX path uses the WDSP TXA chain via cffi (Rule 1 — port WDSP
directly per the project's License posture; everything else is
Lyra-native). The Lyra rip does NOT re-implement these 32 stages —
it wraps them via a single `fexchange0` cffi call. But the
constructor order, run-state defaults, and parameter defaults MUST
match the reference exactly.

The 32 stages in execution order:

```
 1. rsmpin     input resampler           (48k → 96k DSP rate)
 2. gen0       input signal generator    (test/tune at input)
 3. panel      includes MIC gain          ALWAYS RUNS — inselect=2 (I-as-input)
 4. phrot      phase rotator              (PHROT — peak reduction for SSB)
 5. micmeter   MIC meter                  ALWAYS RUNS (read-only)
 6-7. amsq     AM-squelch capture+action
 8. eqp        pre-EQ (10-band)
 9. eqmeter    EQ meter
10. preemph    FM pre-emphasis option 0
11. leveler    Leveler (wcpagc mode 5)    off by default
12. lvlrmeter  Leveler meter
13. cfcomp     Continuous Frequency Compressor
14. cfcmeter   CFC meter
15. bp0        PRIMARY BANDPASS / SSB SIDEBAND SELECTOR — ALWAYS RUNS
16. compressor COMP compressor
17. bp1        aux bandpass — only with COMP
18. osctrl     CESSB Overshoot Control
19. bp2        aux bandpass — only with CESSB
20. compmeter  COMP meter
21. alc        ALC (wcpagc mode 5) — ALWAYS RUNS, load-bearing splatter protection
22. ammod      AM Modulator
23. preemph    FM pre-emphasis option 1 (same object, two call sites)
24. fmmod      FM Modulator
25. gen1       OUTPUT signal generator (TUN + Two-tone) — POST-ALC
26. uslew      Up-slew (5 ms cos² ramp on TX channel start)
27. alcmeter   ALC meter
28. sip1       Siphon — PURESIGNAL FEEDBACK TAP — ALWAYS RUNS
29. iqc        PureSignal correction (pre-distortion apply) — off by default
30. cfir       Compensating FIR (Protocol 2 only — HL2 no-op)
31. rsmpout    Output resampler          (96k → 48k wire rate)
32. outmeter   Output meter
```

Plus `calcc` (created but not in xtxa() — runs on its own
semaphore-driven thread per §9 of this doc).

### 6.2 Critical insights from the chain order

**bp0 is always-on; bp1/bp2 are conditional.** The §15.23 trap was
calling `SetTXABandpassRun` thinking it toggled bp0 — it actually
toggles bp1. Lyra commit `86ac228` ships the correct mechanism
(bp0 configures via `SetTXAMode`/`SetTXABandpassFreqs` only).

**gen1 is POST-ALC.** Tune-carrier amplitude is governed by
`gen1.tone_mag` directly, NOT by ALC. Lyra commit `9cef8fc`
(`_TUNE_TONE_MAG` 0.5→0.95) is correct against this order.

**sip1 + iqc are the PS hooks** (§9).

**preemph appears twice** (call sites 10 and 23) with `position=0`
and `position=1` — operator-selectable FM emphasis placement.

### 6.3 Run-state defaults that matter

| Component | Default run | Lyra responsibility |
|---|---|---|
| panel | 1 (always) | constructor only — never toggle |
| micmeter, alcmeter, eqmeter, lvlrmeter, cfcmeter, compmeter, outmeter | 1 (always; some follow their target's run) | constructor only |
| **bp0** | **1 (always)** | **constructor only — NEVER call SetTXABandpassRun on bp0** |
| **alc** | **1 (always)** | always-on; operator tunes `max_gain` only (default 1.0); attack/decay 1ms/10ms |
| **sip1** | **1 (always — PS forward-compat)** | constructor; v0.2 ships it as a no-op consumer; v0.3 PS calcc thread reads it |
| leveler | 0 (operator opt-in) | toggle via SetTXAlevelerRun |
| compressor | 0 (operator opt-in) | toggle via SetTXACompressorRun — also auto-runs bp1 |
| osctrl | 0 (operator opt-in) | toggle via SetTXAosctrlRun — also auto-runs bp2 |
| phrot | 0 (operator opt-in) | task #109 |
| eqp | 0 (operator opt-in) | task #50 |
| ammod, fmmod | 0 (mode-driven) | enabled by SetTXAMode for AM/FM |
| gen0 | 0 (test only) | not operator-exposed normally |
| gen1 | 0 (operator-toggleable for TUN) | SetTXAPostGenRun + ToneFreq/Mag |
| iqc | 0 (PS, v0.3) | SetTXAiqcRun + coeffs |
| rsmpin, rsmpout | toggled by TXAResCheck | based on rate triple |
| cfir | 0 (Protocol 2) | never run on HL2 P1 |

### 6.4 Parameter defaults that matter

ALC (`wdsp/TXA.c:311-334`):
- mode = 5 (wcpagc mode 5)
- env-detection mode = 1 (envelope, not max(I,Q))
- max_gain = **1.0** (Lyra ships SetTXAALCMaxGain(3.0) per task #38 — this is a Lyra deviation, see §11)
- tau_attack = **0.001** (1 ms)
- tau_decay = **0.010** (10 ms)
- out_targ = 1.0

Leveler (`wdsp/TXA.c:158-181`):
- mode = 5
- env-detection mode = 0 (max(I,Q))
- max_gain = 1.778 (~5 dB)
- tau_attack = 0.001 (1 ms)
- tau_decay = 0.500 (500 ms)
- hangtime = 0.500 (500 ms), hang_thresh = 2.0
- run = **0** (operator opt-in)

bp0 (`wdsp/TXA.c:239-251`):
- run = **1** (always)
- f_low / f_high initially `(-5000.0, -100.0)` — LSB default; overwritten by SetTXAMode
- coefficients = max(2048, dsp_size)
- gain = 2.0
- wintype = 1

sip1 (`wdsp/TXA.c:394-403`):
- run = 1 (always)
- position = 0
- buffer = 16384 samples (the PS feedback ring)
- fft = 16384

phrot (`wdsp/TXA.c:71-78`):
- run = 0
- corner = 338.0 Hz (1/2 of phase frequency)
- n_stages = 8

uslew (`wdsp/TXA.c:369-377`):
- delay = 0
- slew_time = 0.005 (5 ms cos² ramp on AM/FM/gens start)

### 6.5 Lyra-native target

```
lyra::wdsp::TxChannel    // C++ wrapper around the WDSP TXA cffi

constructor:
  OpenChannel(in_rate=48k, dsp_rate=96k, out_rate=48k, type=1,
              state=0, tdelayup=0.000, tslewup=0.010,
              tdelaydown=0.000, tslewdown=0.010, block=1)
  then per-stage setters to match the reference's create_txa() defaults
  (constructor walks every stage; sip1 ALWAYS-ON for Rule 10 PS
  forward-compat).

process(in[insize], out[outsize], &err):
  fexchange0(channel_id, in, out, &err)
  // WDSP runs the 32-stage xtxa() internally; Lyra does not walk the chain.

setters (Lyra wrapper methods, each calls exactly one WDSP cffi):
  set_mode(SsbMode m)             → SetTXAMode + bandpass refresh
  set_passband(low_hz, high_hz)   → SetTXABandpassFreqs (calls
                                    TXASetupBPFilters → bp0 update)
  set_mic_gain_linear(g)          → SetTXAPanelGain1
  set_phrot_run/freq/nstages      → SetTXAPHROTRun / Corner / Nstages
  set_eq(coefs)                   → SetTXAEQRun + EQ coefs
  set_leveler_run/maxgain/decay   → SetTXAlevelerRun / MaxGain / Decay
  set_alc_max_gain(linear)        → SetTXAALCMaxGain (always-on layer)
  set_compressor_run/gain         → SetTXACompressorRun
                                     (auto-runs bp1 internally via WDSP)
  set_cessb_run                   → SetTXAosctrlRun (auto-runs bp2)
  set_tune_gen(run, freq, mag)    → SetTXAPostGenRun / Tone* (gen1)
  set_iqc_run/coeffs              → SetTXAiqcRun + coeffs (v0.3)
  get_meter(MeterType t) → dB     → GetTXAMeter
```

**FORBIDDEN:** `SetTXABandpassRun` is never called from Lyra. bp0
runs always (no setter needed); bp1/bp2 are managed internally by
the compressor/CESSB run paths. Touching bp1.run is the §15.23 trap.

---

## 7. TCI TX-Audio Path

**Reading status:** `TCIServer.cs:5510-5703`, `5339-5362`, `1680-1707` ✓;
`cmaster.cs:1131-1830` (TCI bridge) ✓. Full reference notes in
`docs/refs/tci_tx_audio_path.md`.

### 7.1 The two-queue / two-thread model

```
Client (MSHV/JTDX/WSJT-X)
  │ WebSocket TX_AUDIO_STREAM binary frame
  ▼
TCIServer.cs handleBinaryFrame
  │ convertStreamSamplesToComplex(samples, channels)
  │   mono   → I=Q=mono complex
  │   stereo → I=L, Q=R complex
  ▼
m_txAudioQueue (per-listener; raw complex; client's sample rate; drop-OLDEST)
  ▲ (Thread.Sleep(2) supervisory thread polls)
  │
cmaster.cs TCITxThreadProc (C# QThread @ AboveNormal, 2 ms poll)
  │ ServiceTCITxProtocol:
  │   1. dequeue m_txAudioQueue
  │   2. queueTCITxAudio:
  │        TXStereoInputMode.Both    → mono[i] = (L+R)*0.5      ← L+R AVERAGE
  │        TXStereoInputMode.Left    → mono[i] = L
  │        TXStereoInputMode.Right   → mono[i] = R
  │      then resample mono → targetRate via WDSP xresampleFV
  │   3. enqueue into m_tciTxSampleQueue
  │   4. compute predictedPacketSamples + targetQueuedSamples
  │      futureSamples = queued + outstanding * predicted
  │      requestsNeeded = ceil((target - future) / predicted)
  │   5. send up to requestsNeeded TX_CHRONO frames (bounded by
  │      TCI_TX_MAX_OUTSTANDING)
  ▼
m_tciTxSampleQueue (global; mono float[] at TXA input rate)
  ▲ (cm_main TX thread, semaphore-paced by HL2 wire clock)
  │
cm_main TX (C++ MMCSS Pro Audio @ 2)
  │ xcmaster → if (use_tci_audio):
  │   InboundTCITxAudio(insize, pcm->in[stream])  ← C++→C# callback
  │   = OnTCITxAudioInSamples:
  │     data[2*i]   = sample      // I = mono
  │     data[2*i+1] = sample      // Q = mono  ← I=Q=mono confirmed
  │     (underrun: zero-fill)
  ▼
fexchange0 (WDSP TXA)
```

### 7.2 Critical findings

**Q#1 closed:** `Thread.Sleep(2)` lives at `cmaster.cs:1253` in
`TCITxThreadProc`. **NOT** the DSP path. The DSP path is
`cm_main` semaphore-blocked. The 2 ms tick is a C# supervisory
poll cadence for the CHRONO pull state machine — most ticks
find nothing to do and return. **The audio path itself never
polls; never sleeps; never blocks except on producer events
(EP6 mic arrival or WebSocket binary frame arrival).**

**Q#2 closed (Task #67 framing was correct for TCI side):** TCI
inbound writes I=Q=mono into `pcm->in[stream]` (cmaster.cs:1791-
1792 `OnTCITxAudioInSamples`). The HL2 codec mic path writes
I=mic, Q=0 (networkproto1.c:407). Three input shapes, three I/Q
layouts entering the same buffer — all work because TXA's panel
stage (inselect=2) reads I and discards Q. Logged in §11 as a
declared structural detail.

**L+R averaging confirmed at `cmaster.cs:1414`:**
`mono[i] = (float)((left + right) * 0.5)` when
`TXStereoInputMode.Both` (the default). Lyra's R-FT8 fix from
earlier today (`8cddfa5`) is reference-faithful.

### 7.3 The CHRONO pull formula (`cmaster.cs:1313-1359`)

```
txBlock                 = GetBuffSize(targetRate)     ; = 64 * targetRate / 48000
predictedPacketSamples  = max(txBlock,
                              ceil(requestSamples * targetRate / requestRate))
targetQueuedSamples     = max(txBlock * 4,
                              ceil((bufferingMs + EXTRA_BUFFER_MS) * targetRate / 1000))
futureSamples           = queuedSamples + outstanding * predictedPacketSamples
requestsNeeded          = (futureSamples < targetQueuedSamples)
                          ? ceil((targetQueuedSamples - futureSamples) / predictedPacketSamples)
                          : 0
```

`outstanding` is reset to 0 if the most recent chrono is older than
`max(250, bufferingMs * 4)` ms (deadlock-prevention timeout).
`requestsNeeded` is bounded by `TCI_TX_MAX_OUTSTANDING` per
iteration.

Lyra Task #64 already implemented this formula in its prior
incarnation. The new TX architecture mirrors it in
`lyra::tci::TciTxSupervisorThread::serviceTciTxProtocol()`.

### 7.4 The CHRONO request (`TCIServer.cs:5515-5533`)

```csharp
int requestLength = useModernLengthSemantics
    ? samples * Math.Max(1, channels)        // modern
    : samples;                                // legacy (JTDX-old)
sendBinaryFrame(buildStreamPayload(
    receiver, sampleRate, sampleType,
    requestLength,
    TCIStreamType.TX_CHRONO,
    channels,
    Array.Empty<byte>()));
```

Modern vs legacy length: client signals modern by sending any
`audio_stream_*` setup command (`m_seenModernTxAudioNegotiation`
flips). Lyra commit `939e35d` (end-to-end parity fix) shipped
this; verified against source.

### 7.5 Defaults and constants

| | Value | File:line |
|---|---|---|
| `bufferingMs` clamp | min 50 | cmaster.cs:1297 |
| `requestSamples` default | 480 | cmaster.cs:1296 |
| `requestRate` default | 48000 | cmaster.cs:1295 |
| `TXStereoInputMode` default | `Both` (L+R avg) | TCIServer.cs:6113 |
| `m_seenModernTxAudioNegotiation` initial | false | TCIServer.cs:788 |
| `TCI_TX_EXTRA_BUFFER_MS`, `TCI_TX_MAX_OUTSTANDING` | constants in cmaster.cs (need follow-up read) | — |
| `MAX_TX_AUDIO_QUEUE_BLOCKS`, `MAX_TX_AUDIO_QUEUE_COMPLEX_SAMPLES` | constants in TCIServer.cs (need follow-up read) | — |

### 7.6 Lyra-native target

```cpp
namespace lyra::tci {

class Server : public QObject {
    QWebSocketServer m_server;
    QList<TciListener*> m_listeners;
    // ... (existing TciServer infrastructure)
};

class TciListener : public QObject {
    // Per-client state
    Deque<TciQueuedTxAudio> m_txAudioQueue;       // raw complex, drop-OLDEST
    std::mutex m_txQueueMutex;
    bool m_seenModernTxAudioNegotiation = false;

    int m_audioSampleRate = 48000;
    int m_audioStreamChannels = 2;
    int m_audioStreamSamples = 480;
    SampleType m_audioSampleType = SampleType::FLOAT32;
    int m_txStreamAudioBufferingMs = 50;
    StereoInputMode m_txStereoInputMode = StereoInputMode::Both;

    void handleBinaryFrame(const QByteArray &payload);  // decode TX_AUDIO_STREAM
    void sendTxChrono(int receiver);                    // emit TX_CHRONO
    bool tryDequeueTxAudio(TciQueuedTxAudio &out);
    // ...
};

class TciTxBridge : public QObject {
    // Global bridge between TciListener queue and the WDSP TXA input
    std::deque<std::vector<float>> m_tciTxSampleQueue;  // mono float, TXA rate
    size_t m_tciTxSampleQueueOffset = 0;
    long m_tciTxQueuedSamples = 0;
    int m_tciTxChronoOutstanding = 0;
    int m_tciTxInputRate = 48000;
    std::mutex m_stateMutex;

    void queueTciTxAudio(const TciQueuedTxAudio &queued, int targetRate,
                         StereoInputMode stereoMode);
    void onTciTxAudioInSamples(int nsamples, double *data);
                                      // writes I=Q=mono into data;
                                      // zero-fills on underrun
};

class TciTxSupervisorThread : public QThread {
    void run() override {
        while (m_running) {
            serviceTciTxProtocol();
            QThread::msleep(2);                          // matches reference
        }
    }
    void serviceTciTxProtocol();    // dequeue+queue+chrono formula+send
};

}  // namespace lyra::tci
```

### 7.7 Open items remaining for §7

1. The `audio_stream_*` handshake handlers (TCIServer.cs around
   5930, 5946, 5963, 5996) — which set `m_seenModernTxAudioNegotiation`.
2. `TCI_TX_EXTRA_BUFFER_MS` and `TCI_TX_MAX_OUTSTANDING` constants.
3. `MAX_TX_AUDIO_QUEUE_*` constants.
4. The `Audio.MOX` ↔ `TCITxThreadProc.ServiceTCITxProtocol` coupling
   (resetTCITxState on MOX edge).

---

## 8. HL2 EP2 Wire Path

**Reading status:** `networkproto1.c:869-1098` (WriteMainLoop_HL2
cases 0-10) ✓; cases 11+ and `ForceCandCFrame` pending.
Full reference notes in `docs/refs/mox_fsm_and_wire_frame.md`.

### 8.1 Frame layout

Per UDP datagram: 2 × 512-byte USB frames.

Per USB frame (`networkproto1.c:881-883`):

| Bytes | Content |
|---|---|
| 0-2 | sync bytes `0x7f 0x7f 0x7f` |
| 3-7 | C0..C4 control bytes (5 bytes) |
| 8-511 | payload (504 bytes = 63 LRIQ tuples × 8 bytes) |

Per LRIQ tuple (8 bytes):
| Bytes | Content |
|---|---|
| 0-1 | L audio (BE int16, signed) |
| 2-3 | R audio (BE int16, signed) |
| 4-5 | TX I (BE int16, signed; or CW state bits if `cw_enable && j==1`) |
| 6-7 | TX Q (BE int16, signed) |

### 8.2 MOX bit lives in C0 of every frame

`networkproto1.c:896`: `C0 = (unsigned char)XmitBit;`

Every C&C frame carries the live MOX bit as C0 bit 0. The C&C
round-robin's case-specific bits OR additional bits into C0 (e.g.
case 1 ORs `0x02` for the TX-VFO register address).

### 8.3 Hermes-II TX-edge re-prime (HL2 no-op)

`networkproto1.c:885-891`: on `XmitBit != PreviousTXBit` edge,
**if nddc==2** jump `out_control_idx = 2` so the next frame
carries the RX1 VFO (DDC0) freq update — this re-routes DDC0 to
TX freq for PS feedback on Hermes-II. **HL2 has nddc=4, so this
branch is a no-op.** HL2 relies on persistent `tx[0].frequency` +
the duplex bit (set in C&C case 0 C4 bit 2).

Lyra's existing TX-freq writer (§15.18 Phase 1 commit) pre-emits
0x02 + 0x08 + 0x0a atomically on `_set_tx_freq` — correct for HL2.

### 8.4 C&C round-robin cases (HL2-relevant subset)

| Case | C0 OR | Reg | Carries (HL2) |
|---|---|---|---|
| 0 | — | General settings | SampleRate(C1); EER+OC(C2); ATT+preamp+dither+random+RX1_out+XVTR/RX1/RX2_in(C3); ANT(C4 low)+`0x04` duplex(C4 bit2)+(nddc-1)<<3(C4 bits 3-6)+diversity(C4 bit7) |
| 1 | `0x02` | TX VFO | `prn->tx[0].frequency` big-endian C1-C4 |
| 2 | `0x04` | RX1 VFO (DDC0) | `rx[0].frequency` normally; TX freq if (nddc==2 && XmitBit && PS) — HL2 no-op |
| 3 | `0x06` | RX2 VFO (DDC1) | `rx[1].frequency` normally |
| 4 | `0x1C` | ADC + TX-ATT | C3 = `adc[0].tx_step_attn & 0x1F` — **5-bit, this is the encoding Lyra fixed in commit `73a459b`** |
| 5 | `0x08` | RX3 VFO (DDC2) | **TX freq on HL2** (PS feedback DDC) |
| 6 | `0x0a` | RX4 VFO (DDC3) | **TX freq always** (PS feedback DDC) |
| 10 | `0x12` | HL2 hardware | C1 = `drive_level` (top 4 bits = HL2 16-step DAC); **C2 = mic_boost\|line_in<<1\|ApolloFilt(0x04)\|ApolloTuner(0x08)\|ApolloATU(0x10)\|ApolloFiltSelect(0x20)\|0x40** ; C3 = HPF flags + `pa<<7` (legacy, HL2+ ignores); C4 = LPF flags |
| 11 | `0x14` | Step-attn + PS | (pending read — confirmed-correct in CLAUDE.md §15.26 + commit `73a459b` for the MOX-gated TX/RX branch + the 0x40 bit + `(31-x) & 0x3F` HL2 encoding) |

### 8.5 PA enable — the HL2+ ground truth

`networkproto1.c:1079-1080` (C&C case 10 / reg 0x12):

```c
C2 = ((prn->mic.mic_boost & 1) | ((prn->mic.line_in & 1) << 1) | ApolloFilt |
      ApolloTuner | ApolloATU | ApolloFiltSelect | 0b01000000) & 0x7f;
```

With:
- `ApolloFilt = 0x04` → C2 bit 2 (when ON, tr_disable=0, T/R relay can fire)
- `ApolloTuner = 0x08` → **C2 bit 3 = PA enable** (HL2+ ak4951v4 RTL
  `control.v:209-220` reads this as `pa_enable<=data[19]`)
- `ApolloATU = 0x10` → C2 bit 4
- `ApolloFiltSelect = 0x20` → C2 bit 5
- `0x40` → C2 bit 6 always set

**PA-on configuration (HL2+ with operator's chkApolloTuner=True +
chkApolloFilter=True):** `C2 = 0x40 | 0x08 | 0x04 = 0x4C`. Lyra
commit `cbba63a` already correct.

`networkproto1.c:1084` C3 bit 7 = `(prn->tx[0].pa & 1) << 7` is the
**legacy PA path** that HL2+ gateware does NOT decode (cross-
verified via the gateware RTL read in CLAUDE.md §15.26 + Lyra
commit `d577d2d` `tr_disable` polarity fix).

### 8.6 MOX gating — double layer

Two independent layers ensure no stray TX RF:

1. **C0 bit 0 = MOX** — every C&C frame carries it
   (`networkproto1.c:896`). Gateware reads per frame.
2. **TX I/Q zero-fill on `!XmitBit`** — `networkproto1.c:1227`:
   `if (!XmitBit) memset(prn->outIQbufp, 0, sizeof(complex) * 126);`
   Even if MOX-bit-in-C0 went off but a buffer had stray I/Q,
   this zeroes it before quantize/wire.

Both must be honored by Lyra's wire-send thread.

### 8.7 CW state bits in TX I-LSBs

`networkproto1.c:1247-1259`: when `prn->cw.cw_enable && j == 1`
(j==1 = TX I/Q half of each tuple), the TX I bytes are
**overwritten** with CW state:
- HL2: `temp = (cwx_ptt<<3 | dot<<2 | dash<<1 | cwx) & 0x0F`
- Non-HL2: `temp = (dot<<2 | dash<<1 | cwx) & 0x07`

This is HL2-specific and lands in Lyra's v0.2.2 CW modulator (task
#105).

### 8.8 Drive level — top 4 bits

`networkproto1.c:1078`: `C1 = prn->tx[0].drive_level;`

Per the HL2 wiki: `0x09[31:28]` (= C1's top 4 bits) is the active
drive-level field. The HL2 drive DAC has **16 coarse steps**, not
256. Lyra commit `8813a5d` already correct (operator TX% → `i =
round(255 * pct / 100)` → C1 top nibble).

### 8.9 Lyra-native EP2 packer

`lyra::wire::Ep2Packer` (planned). One `std::thread`,
`AvSetMmThreadCharacteristics("Pro Audio") @ 2`. Blocked on a
two-condition wait equivalent to `WaitForMultipleObjects(LR, IQ,
AND, INFINITE)` — `std::condition_variable` with two flags AND'd.

Per-frame logic mirrors `WriteMainLoop_HL2` byte-for-byte:
sync bytes → C0 with live MOX bit → C&C round-robin case dispatch
→ payload pack (with MOX-zero-fill of IQ if `!mox`; CW LSB
override if `cw_enable`; quantize float→int16 round-to-nearest).

Single writer per C&C register state (Rule 1 / single-actuator
discipline carried from CLAUDE.md §15.23/§15.26 lessons).

### 8.10 C&C cases 11-18 (closing pass)

Full C&C round-robin is 19 cases (0-18); wraps at 18
(`networkproto1.c:1180-1183 if (out_control_idx < 18)
out_control_idx++; else out_control_idx = 0;`).

| Case | C0 OR | Reg | Carries (HL2) |
|---|---|---|---|
| 11 | `0x14` | Preamp + MOX-gated step-attn + PS | C1 = MOX-gated `(tx_step_attn or rx_step_attn) & 0x3F \| 0x40` (6-bit, MOX-gated, enable bit 0x40) ; mic_trs/mic_bias/mic_ptt ; line_in_gain (low 5b) + **`puresignal_run` (bit 6)** ; user_dig_out (low 4b) |
| 12 | `0x16` | RX2/RX3 step-attn + CW config | C1 = if XmitBit then 0x1F (force RX2 LNA min) else `adc[1].rx_step_attn` + bit 5 enable ; ADC[2] step-attn ; CW iambic/mode_b/keyer_speed/weight |
| 13 | `0x1e` | CW basic | cw_enable / sidetone_level / rf_delay |
| 14 | `0x20` | CW hang+sidetone-freq | hang_delay (10b) / sidetone_freq (12b) |
| 15 | `0x22` | EER PWM | epwm_min (10b) / epwm_max (10b) |
| 16 | `0x24` | BPF2 + PS again | Apollo-Filter-2 HPF/LPF flags ; xvtr_enable + **`puresignal_run` (bit 6)** |
| 17 | `0x2e` | **TX latency + PTT hang** | C3 = ptt_hang (5b) ; C4 = tx_latency (7b) — the §15.7 register Lyra commit `9d270e3` already wired |
| 18 | `0x74` | **reset_on_disconnect** | C4 = reset_on_disconnect — the §15.20 commit `aef0106` / `ec636d7` register; Lyra default OFF |

**Case 11 is the §15.26 / Commit-C MOX-gated step-attn register**
— source-verified at `networkproto1.c:1099-1102`:
```c
if (XmitBit)
    C4 = (prn->adc[0].tx_step_attn & 0b00111111) | 0b01000000;
else
    C4 = (prn->adc[0].rx_step_attn & 0b00111111) | 0b01000000;
```
6-bit, MOX-gated, 0x40 enable bit. **Confirms CLAUDE.md §15.26
Commit-C** (Lyra `73a459b`). The `(31 − tx_db)` operator-axis
inversion happens in the C# layer (`console.cs:19165
NetworkIO.SetTxAttenData(31 - txatt)` for HERMESLITE), so what
arrives at `prn->adc[0].tx_step_attn` is already the wire value.

**Case 12 forces RX2 LNA min (C1=0x1F) when XmitBit** — the
RX-ADC protection on the second ADC. Operator's ATT-on-TX policy
is wire-correlated across both ADCs.

**`puresignal_run` appears TWICE on the wire** (case 11 C2 bit 6,
case 16 C2 bit 6). Redundant write — gateware reads from either.

### 8.11 Frame assembly + wire send

`networkproto1.c:1186-1200`:
```c
txbptr[3] = C0;    // C0-C4 follow the 3 sync bytes
txbptr[4] = C1;
txbptr[5] = C2;
txbptr[6] = C3;
txbptr[7] = C4;
// (then 504 bytes payload — done in the per-USB-frame loop's continuation)

memcpy(FPGAWriteBufp + 8,   bufp,       8 * 63);  // USB frame 1 payload
memcpy(FPGAWriteBufp + 520, bufp + 504, 8 * 63);  // USB frame 2 payload

MetisWriteFrame(0x02, FPGAWriteBufp);             // UDP send
ReleaseSemaphore(prn->hobbuffsRun[0], 1, 0);     // signal LR producer "ok send more"
ReleaseSemaphore(prn->hobbuffsRun[1], 1, 0);     // signal IQ producer "ok send more"
```

After sending, the two `ReleaseSemaphore` calls notify the
output producers (LR audio + TX IQ) that they can write the next
chunk. This closes the producer/consumer loop on the wire-send
side — the producers are `obbuffs` rings (separate from `cmbuffs`),
which is the OUTBOUND companion to the inbound rings.

### 8.12 Open items remaining for §8

1. `ForceCandCFrame` priming sequence (`networkproto1.c:134`) —
   sends N C&C frames at startup before normal operation.
2. `XmitBit` global write site (which C# call writes it).
3. The two `obbuffs` ring producers (LR + IQ) — separate from
   `cmbuffs` (covered in §3); need brief read of `obbuffs.c` for
   the production side.

---

## 9. PureSignal Forward-Compat Hooks

**Reading status:** `wdsp/calcc.c:1-100` (structure + state) ✓;
`wdsp/iqc.c:1-130` (interface + 5-state FSM) ✓; `wdsp/TXA.c` sip1
+ calcc + iqc registration ✓; `PSForm.cs` (operator-facing FSM)
deferred to v0.3 implementation (not Phase 0 blocker — the
integration POINTS are what Phase 0 needs to lock).

### 9.1 The three hooks (Rule 10 — PS-shaped from day one)

**(1) sip1 TX I/Q tap** — `TXA.c:394-403`, ALWAYS-ON
(`run=1` at construction). Position in xtxa() is step 28
(line 586), after ALC + uslew + alcmeter, before iqc + cfir +
output resampler. Captures 16384-sample TX I/Q ring for the
calcc thread to read. Must be present from v0.2 (Rule 10) —
no consumer until v0.3, but the buffer + the call site exist
so v0.3 doesn't have to re-validate every TX sub-mode.

**(2) calcc thread + state** — `TXA.c:405-422 create_calcc(...)`.
The full state structure (`calcc.c:30-81 size_calcc`):
```
env_TX, env_RX        — TX and feedback envelope buffers
x, ym, yc, ys         — input + spline output (magnitude/cos/sin)
cat                   — calibration table (4 × nsamps)
t, tmap               — time + mapping arrays (ints+1)
cm, cc, cs            — magnitude/cos/sin spline coefficients (ints × 4)
cm_old                — previous coefficient set (for smooth crossover)
rxs, txs              — RX feedback + TX sample buffers (nsamps complex)
ccbld                 — cubic-spline builder
ctrl.{cpi,sindex,sbase}  — per-interval control state
disp.{x,ym,yc,ys,cm,cc,cs}  — display copies (operator UI)
util.{pm,pc,ps}       — utility coefficients (4 × util.ints)
```
Default config (TXA.c:405-422): channel-bound, run=1, input
buffer 1024, sample rate = `ch[channel].in_rate`, **ints=16**,
**spi=256**, hw_scale=1.0/0.4072, mox_delay=0.1, loop_delay=0,
ptol=0.8, **pin_mode=1, map_mode=1**, stbl_mode=0,
pin_samples=256, alpha=0.9.

The calcc THREAD itself (separate from the data struct) runs
on its own semaphore-driven loop — needs deeper read in v0.3
implementation; for Phase 0 the integration point is just
"create_calcc(...) at channel open, destroy on close, run
inert until v0.3 wires it to a real PS dialog FSM."

**(3) iqc application stage** — `iqc.c:87-100 create_iqc(...)` +
`iqc.c:122+ xiqc(...)`. Created at `TXA.c:424-432` with
`run=0, ints=16, tup=0.005, spi=256`. Position in xtxa() is
step 29 (line 587), right after sip1.

The **IQC FSM** is a 5-state machine (`iqc.c:113-120`):
```c
enum _iqcstate {
    RUN = 0,    // applying correction normally
    BEGIN,      // smooth crossover to new coefficients (using cup raised-cosine)
    SWAP,       // atomic swap to new coef set
    END,        // smooth crossover off
    DONE        // idle (no correction applied)
};
```
With `tup=0.005` (5 ms changeover time), `ntup = 5ms * rate`
samples for the smooth crossover. Lookup table `cup[i] = 0.5 *
(1.0 - cos(theta))` builds a raised-cosine envelope so
coefficient-set transitions don't click.

`xiqc()` applies the coefficients in-place on midbuff: reads
I+Q, computes envelope, indexes into spline coefficient table,
applies pre-distortion magnitude/phase to the signal. Per-sample
operation; runs every TXA tick.

### 9.2 The PS DDC routing — (MOX, ps_armed) state product

(See §5.4 — pending sub-read of `UpdateDDCs`.)

Per CLAUDE.md §3.8 (corrected) + the HL2+ ak4951v4 RTL reads:
during HL2 PS+TX, gateware re-routes **DDC0+DDC1** to the PA
coupler (cntrl1=4); RX1's actual band isn't being received in
this state; DDC2+DDC3 stay gateware-disabled on HL2 (those EP6
slots are zeros). The Lyra rip's `protocol::ddc_map(state)`
returns the dispatch table for this state product.

The `puresignal_run` bit appears in C&C case 11 C2 bit 6 + case
16 C2 bit 6 (both source-verified §8.10 above).

### 9.3 Lyra-native target

```cpp
namespace lyra::wdsp {

class TxChannel {
    // ... (per §6)
    // sip1 always-on at construction
    // calcc instance constructed at channel open, run=0 until v0.3
    // iqc instance constructed, run=0 until v0.3
};

}  // namespace lyra::wdsp

namespace lyra::tx::ps {     // v0.3 implementation

class PsFsm {
    enum class State {
        Off,
        Armed,          // operator opted in; not yet calibrating
        Calibrating,    // calcc thread running
        Applying,       // iqc running with current coefficients
        Recalibrating,  // periodic refresh
    };
    // Drives the operator-facing PS dialog
    // Coordinates with the DDC routing (state-product on MOX edge)
    // Manages auto-attenuator state machine for HL2
    // Coefficient persistence to disk
};

class PsCalcThread {     // v0.3
    // Host feed only: accumulate EP6 DDC0/DDC1 IQ pairs and call
    // pscc(txid, n, tx, rx). WDSP owns the calc thread internally.
    // sip1 is the TX panadapter (TXASetSipDisplay) — NOT the calcc feed.
    // Do not call SetTXAiqc* from the host (deskHPSDR never does).
};

}  // namespace lyra::tx::ps
```

### 9.4 Phase 2 deliverables (v0.2) for PS forward-compat

Per Rule 10, Phase 2 of the rip must include:
1. TXA channel open already constructs WDSP calcc/iqc internally
   (`run=0` until `pscc`/`SetPSRunCal`). Host does not wrap iqc.
2. sip1 always-on at construction = TX panadapter, not PS feedback.
3. DDC routing: `ddc_map(mox, ps_armed, rx2, family)` — live HL2
   MOX+PS = Thetis `cntrl1=4` + DDC0/DDC1 → `pscc`. See
   `docs/architecture/puresignal_hl2.md`.
4. C&C case 11 C2 bit 6 + case 16 C2 bit 6 `puresignal_run`
   from `set_ps_armed(bool)` (attestation default false).

These four items are **prerequisites for Phase 1 sign-off** —
v0.3 PS work otherwise re-validates every TX sub-mode.

### 9.5 Open items remaining for §9 (defer to v0.3)

1. The calcc thread wake mechanism (semaphore + producer).
2. `PSForm.cs` operator FSM (PS dialog states + auto-att FSM).
3. Auto-attenuator state machine for HL2 (CLAUDE.md §3.8:
   `FeedbackLevel > 181 || (FeedbackLevel <= 128 && cur_att >
   -28)` recalibrate trigger).
4. Coefficient persistence file format.
5. `iqc.c` `xiqc()` full body — the per-sample apply math
   (defer to v0.3 implementation).
6. `calcc.c` full math (size_calcc captured; the `calc()`
   function that does the actual cubic-spline regression
   defers to v0.3).

---

## 10. Reference → Lyra-Native Implementation Map

This section maps every reference component identified in §3-9 to
the new Lyra-native target file. **This is the file list Phase 1
creates as empty files**, and Phase 2 fills in bottom-up.

### 10.1 What gets archived (rip side)

Current Lyra TX code goes to a `tx-rip-archive` branch and is
deleted from `main` (Rule 7). The current files:

| File | Lines | Disposition |
|---|---|---|
| `src/tx_channel.cpp` | 516 | archive + delete |
| `src/tx_channel.h` | 227 | archive + delete |
| `src/tx_dsp_worker.cpp` | 372 | archive + delete |
| `src/tx_dsp_worker.h` | 369 | archive + delete |
| `src/tx_ring.cpp` | 117 | archive + delete |
| `src/tx_ring.h` | 140 | archive + delete |
| `src/tci_mic_source.cpp` | 90 | archive + delete |
| `src/tci_mic_source.h` | 177 | archive + delete |
| **Total** | **2008** | |

**Surgical extractions (not whole-file deletions):**
- `src/tci_server.cpp/.h` — RX side stays; TX-audio side gets
  ripped + replaced per §7.
- `src/hl2_stream.cpp/.h` — RX path stays; TX-side EP2 packing
  + C&C round-robin gets ripped + replaced per §8.
- `src/ptt.cpp/.h` — current FSM gets replaced by new one per
  §5.1 (the new FSM lands by mirroring the reference's single-
  funnel `chkMOX_CheckedChanged2` design).
- `src/radio.cpp/.h` — TX-related glue (MOX flag, set_mox,
  force_release_all, etc.) gets reviewed + reshaped per §5.

### 10.2 What gets built (Lyra-native target file list)

Organized by namespace / subsystem.

#### `lyra::dsp` — per-stream DSP consumer threads (§3)

| New file | Mirrors | Notes |
|---|---|---|
| `src/dsp/StreamThread.h / .cpp` | `cm_main` (cmbuffs.c:151) | std::thread, MMCSS Pro Audio @ 2, blocked on counting semaphore. One instance per stream. Implements the cm_main loop body: sem.acquire() → ring.drain() → channel.process(). |
| `src/dsp/StreamRing.h` | cmbuffs.c ring | SPSC ring buffer, kernel-semaphore-paired, complex64 elements, capacity CMB_MULT × outsize. Lyra has `tx_ring` today; this replaces and generalizes. |
| `src/dsp/StreamRegistry.h / .cpp` | cmaster.c stream creation | Maps stream-id → StreamThread + StreamRing. Construction order matches create_cmaster(). |

#### `lyra::wdsp` — TXA / RXA channel wrappers (§6)

| New file | Mirrors | Notes |
|---|---|---|
| `src/wdsp/TxChannel.h / .cpp` | TXA.c create_txa + xtxa | Constructor calls OpenChannel + per-stage setters matching every reference default. `process(in, out, &err)` calls fexchange0. **bp0 always-on. ALC always-on. sip1 always-on (Rule 10).** |
| `src/wdsp/TxaCffi.h` | TXA.c API | cffi declarations for SetTXAMode, SetTXABandpassFreqs, SetTXAPanelGain1, SetTXAPHROT*, SetTXAEQRun, SetTXAlevelerRun, SetTXAALCMaxGain, SetTXACompressorRun, SetTXAosctrlRun, SetTXAPostGen*, SetTXAiqcRun, GetTXAMeter |
| `src/wdsp/ChannelLifecycle.h` | WDSP OpenChannel/CloseChannel/SetChannelState | Wrappers for the lifecycle calls used by keydown/keyup. **`OpenChannel(..., block=1)`** — the previously-wrong parameter, now correct. |

#### `lyra::tx` — MOX/PTT FSM + keydown/keyup + ATT-on-TX + TR delays (§5)

| New file | Mirrors | Notes |
|---|---|---|
| `src/tx/PttFsm.h / .cpp` | chkMOX_CheckedChanged2 + single shared MOX state | Single-funnel FSM, `PttSource` enum {SwMox, HwPtt, Cat, Tci, Vox, CwKey, Tun}, source set, resolver. Edge-detector + the keydown/keyup sequencer hooks. |
| `src/tx/KeydownSequence.h / .cpp` | console.cs:30269-30348 | The exact 16-step keydown order. Calls StreamThread.requestStop(blocking) for RX; AttOnTxPolicy.applyKeydown; UpdateDdcs; UpdateAamixerStates; HdwMoxChanged; (rf_delay); AudioMoxChanged; TxChannel.start. |
| `src/tx/KeyupSequence.h / .cpp` | console.cs:30350-30407 | The exact 16-step keyup order. (space_mox_delay); TxChannel.stop(blocking); (mox_delay); UpdateDdcs; UpdateAamixerStates; AudioMoxChanged; HdwMoxChanged; (ptt_out_delay); StreamThread.start for RX. |
| `src/tx/AttOnTxPolicy.h / .cpp` | console.cs:19148-19178 + force-31 logic | Save per-band RX1 step-attn on keydown; force tx-att-for-band (31 if non-PS + non-ATU-bypass + non-CW); restore on keyup. Master default ON. |
| `src/tx/TrSequencing.h` | console.cs operator settings | rf_delay (50), mox_delay (15), space_mox_delay (13), ptt_out_delay (5), key_up_delay (10). Operator-configurable; capability-sourced. Already in `Capabilities`. |
| `src/tx/MoxEdgeFade.h / .cpp` | uslew + WDSP cos² ramps | The cos² up-ramp + down-ramp wrapper. Internal to the keydown/keyup sequencers. |

#### `lyra::wire` — HL2 EP6 receive + EP2 send (§3, §8)

| New file | Mirrors | Notes |
|---|---|---|
| `src/wire/Ep6RecvThread.h / .cpp` | MetisReadThreadMain | std::thread, MMCSS, recvfrom loop on EP6 socket. Parses each datagram, dispatches via DdcMap to per-stream rings. |
| `src/wire/Ep2SendThread.h / .cpp` | sendProtocol1Samples | std::thread, MMCSS Pro Audio @ 2. Two-condition AND wait for LR + IQ. MOX-gate-zero on `!XmitBit`. CW LSB packing. Quantize. Per-frame composer call. |
| `src/wire/FrameComposer.h / .cpp` | WriteMainLoop_HL2 | The 19-case C&C round-robin (cases 0-18) per §8. ALL bits source-verified. |
| `src/wire/RadioNet.h / .cpp` | `prn->*` fields | The C&C register state struct (drive_level, step_attn for ADC[0..2], pa, mic_*, BPF/LPF flags, puresignal_run, tx[0].frequency, rx[0..1].frequency, ptt_hang, tx_latency, reset_on_disconnect, etc.). Single writer per field — caller discipline (Rule 1). |
| `src/wire/DispatchState.h` | `XmitBit` global + dispatch axes | `{mox, ps_armed, rx2_enabled, family}` struct + atomic accessors. Read by the wire-send thread to gate MOX bit + DDC routing. |
| `src/wire/DdcMap.h / .cpp` | UpdateDDCs + cntrl1 routing matrix | The §5.4 DDC routing — given `DispatchState`, returns which DDC samples route to which consumer (RX1, RX2, PS feedback, drop). |
| `src/wire/OutboundRing.h` | obbuffs | SPSC ring for LR audio + TX IQ. Paired Win32-event / `std::condition_variable` AND-wait on consumer side. |
| `src/wire/ForceCandC.h / .cpp` | ForceCandCFrame priming | Sends N C&C frames at startup before the EP2-send-thread is allowed to start the normal loop. |

#### `lyra::tci` — TCI server + TX-audio path (§7)

| New file | Mirrors | Notes |
|---|---|---|
| `src/tci/TciServer.cpp/.h` | (existing; RX side stays; TX side rewrite) | TX_AUDIO_STREAM binary frame handler (`handleBinaryFrame`) + handshake parsers + per-listener `m_txAudioQueue`. |
| `src/tci/TciListener.cpp/.h` | per-client state in TCIServer.cs | `m_txAudioQueue` with drop-OLDEST overflow; per-listener handshake state including `m_seenModernTxAudioNegotiation`. |
| `src/tci/TciTxSupervisorThread.h / .cpp` | TCITxThreadProc (cmaster.cs:1243) | QThread or std::thread, 2 ms poll cadence (`msleep(2)`). Runs serviceTciTxProtocol: dequeue → queueTciTxAudio → chrono formula → send chronos. **The ONLY 2 ms poll in the rip's TX path** — supervisory, not audio. |
| `src/tci/TciTxBridge.h / .cpp` | queueTciTxAudio + OnTciTxAudioInSamples | The two functions that bridge between m_txAudioQueue (raw stereo) and m_tciTxSampleQueue (mono float at TXA rate). L+R averaging per `TXStereoInputMode`. WDSP `xresampleFV` resample. Underrun → zero-fill. |
| `src/tci/TciTxStereoMode.h` | TCITxStereoInputMode | enum {Both, Left, Right} with default Both. |

#### `lyra::ps` — PureSignal hooks (§9, inert in v0.2)

| New file | Mirrors | Notes |
|---|---|---|
| `src/ps/CalcCffi.h` | calcc.c API | cffi declarations for create_calcc, destroy_calcc, calc() (the calibration math entry point). |
| `src/ps/IqcCffi.h` | iqc.c API | cffi declarations for create_iqc, destroy_iqc, xiqc(), SetTXAiqcRun, the 5-state FSM transitions. |
| `src/ps/PsCalcThread.h / .cpp` | (calcc thread — defer to v0.3) | Empty placeholder in v0.2; v0.3 fills with the calcc semaphore consumer loop. |
| `src/ps/PsFsm.h / .cpp` | PSForm.cs operator FSM | Empty placeholder in v0.2; v0.3 fills with the operator-facing PS dialog states + auto-attenuator state machine. |
| `src/ps/IqcLifecycle.h / .cpp` | iqc.c 5-state FSM wrapper | Wraps the SetTXAiqcRun toggles to manage RUN/BEGIN/SWAP/END/DONE transitions. Defer to v0.3. |

#### `lyra::protocol::capabilities` — capability struct (§5, §6, §8)

| Existing file | Extensions per Phase 0 |
|---|---|
| `src/protocol/capabilities/Capabilities.h` | Add `tx_step_attn_range = (-28, 31)`, `tx_step_attn_wire_encoding = HL2_31_MINUS_X`, `pa_enable_bit = ApolloTuner_C2_bit_3`, `cw_state_bits_count = 4` (HL2), `nddc = 4`. TR sequencing already there. |

### 10.3 Empty-file creation order (Phase 1)

Phase 1 creates these files as **empty C++ skeletons** (header
with class shell + `.cpp` with includes only). No logic. Just
the file tree. The compile target builds (empty TX module
compiles into an empty library) with `lyra.exe` running RX-only.
Phase 2 then fills bottom-up:

1. `protocol::capabilities` (extend Capabilities) — needed by everything
2. `wire::RadioNet`, `wire::DispatchState` — needed by composer + dispatch
3. `wire::FrameComposer` — wire-correct C&C frame builder, no thread yet
4. `wdsp::TxChannel` (wire-inert: no Ep2SendThread yet, just channel construct/destruct)
5. `dsp::StreamRing`, `dsp::StreamThread`
6. `wire::Ep6RecvThread`, `wire::Ep2SendThread` (TX paths still wire-inert)
7. `tx::PttFsm`, `tx::AttOnTxPolicy`, `tx::TrSequencing`, `tx::MoxEdgeFade`
8. `tx::KeydownSequence`, `tx::KeyupSequence`
9. `tci::TciTxBridge`, `tci::TciTxSupervisorThread` — TCI inbound path
10. `tci::TciServer` TX-audio side rewrite
11. `wire::DdcMap` — routing matrix
12. `ps::*` empty skeletons (Rule 10 — present from v0.2)
13. **Phase 2 wire-inert activation:** turn on TX channel, verify all paths bench-correct (build OK, log shows correct C&C state, no spurious wire writes).
14. **Phase 2 wire-active activation:** flip the bit that allows EP2SendThread to actually send TX IQ. Bench → operator HL2 verify → continue.

### 10.4 Branch strategy

```
main           ← (where RX lives; never gets broken)
  │
  ├── tx-rip-archive  ← old TX code preserved here as a permanent
  │                     reference; never merged back
  │
  └── tx-rebuild      ← Phase 1 creates empty files; Phase 2 fills
                        bottom-up; bench-validated at each step.
                        Merges to main when wire-active + full chain
                        passes operator HL2 bench.
```

The `tx-rip-archive` branch lets a future agent or future-Claude
look up "how did the broken thing work" without breaking the
working RX in `main`. The `tx-rebuild` branch is where Phase 2+
happens.

---

## 11. Declared Deviations (Rule 11 stop-and-ask log)

Each entry follows this format:

**Deviation #N — \<short title\>** (§-of-doc affected)
- **Reference behaviour:** \<what the reference does, file:line\>
- **What forces a variation:** \<language idiom mismatch, threading
  primitive mismatch, Qt-vs-Win32 model gap, …\>
- **Pros / cons of the variation:** \<as discussed with Rick\>
- **Resolution:** \<the agreed Lyra-native behaviour\>
- **Sign-off date:** \<YYYY-MM-DD\>

Catch-myself rule (Rule 17): if I find myself about to write something
that diverges and *doesn't* have an entry here, STOP, raise it with
Rick, log it, then proceed.

---

### Structural detail #1 (NOT a deviation — declared for clarity)

**Three valid I/Q layouts feed the same TX channel input buffer** (§6, §7, §8)

Three input shapes converge at `pcm->in[stream]` for fexchange0:

| Path | I | Q | Reference file:line |
|---|---|---|---|
| HL2 codec mic via EP6 | mic sample | **0.0** | networkproto1.c:404-407 |
| TCI mono input (post-decode + post-supervisor + post-OnTCITxAudioInSamples) | mono sample | **mono sample (= I)** | TCIServer.cs:5347-5350 ; cmaster.cs:1791-1792 |
| TCI stereo input (post-L+R-average via TXStereoInputMode + post-OnTCITxAudioInSamples) | mono (averaged) | mono (= I) | cmaster.cs:1414 ; 1791-1792 |

All three work because **TXA's `panel` stage uses `inselect=2`
(use I for input)** at `TXA.c:68`, so Q is read and discarded.
The downstream chain operates on I only; Q reappears post-bp0 (the
SSB sideband selector) as the analytic complement, not as input
data.

**Lyra implication:** the rip must handle both formats correctly:
HL2-codec-mic path writes I=mic, Q=0; TCI path writes I=Q=mono.
Both are correct against the reference. Lyra Task #67's framing
("match the reference I=Q=mono") is reference-faithful for the
TCI path specifically — was previously ambiguous but now source-
verified.

Not a deviation; declared here because future code must not break
either layout assuming the other is "the only" correct one.

*(no Lyra-vs-reference behavioural deviations yet — log them here
when surfaced during the reads.)*

---

## 12. Open Questions

The four original Phase-0 open questions (Q#1 `Thread.Sleep(2)`
location, Q#2 TCI I/Q format, Q#3 `UpdateDDCs` routing, Q#4
`WriteMainLoop_HL2` cases 11+) are all resolved or appropriately
deferred. See §13 for the per-section sub-read backlog (parked
for when each Phase 2 component lands).

**Operator answers (Rick, 2026-06-04):**

1. **Branch strategy (§10.4) — APPROVED.** `tx-rip-archive` +
   `tx-rebuild` model locked. Archive branch permanent; rebuild
   merges to main only when wire-active + full chain passes
   operator HL2 bench.

2. **Empty-file creation order (§10.3) — APPROVED as written.**
   Follow the 14-step sequence in order.

3. **§15.27 toggle cleanup — DEFER.** Operator decision: "Keep
   the existing configuration toggles during the rewrite so we
   don't change too many variables at once. We will run a
   dedicated cleanup pass to prune the obsolete toggles once
   the new TX path is verified working." Phase 1+ does NOT
   touch operator-facing toggles ("DSP Threading (advanced)",
   etc.); they stay as-is until a dedicated post-rip cleanup
   commit.

4. **Older legacy-app cross-reference — SKIP.** Operator decision:
   "the current reference is modern and contains everything we
   need as a reference model." Phase 0+ proceeds against the
   current reference source only.

5. **Phase-3-EXIT kill-test gate — bench-verify when applicable.**
   Operator: "Kill Test we bench verify!" Carried forward as a
   live gate; not Phase-0-blocking but mandatory before
   real-antenna keying after Phase 2 produces RF.

*(no remaining operator-input items at Phase 0 sign-off; all
sub-reads tracked in §13 per-section.)*

---

## 13. Sign-off

Phase 0 closes when Rick signs off on this document end-to-end.
After that:

1. Phase 1: Old TX → `tx-rip-archive` branch + delete from `main`
   + create empty file skeleton per §10.
2. Phase 2: Bottom-up port, one component at a time per Rule 9,
   each bench-validated against the reference's measurable
   behaviour before the next starts.

Re-opening sign-off (substantive document changes after first
approval) requires a new dated sign-off line below.

### Current state (end of Phase 0 first-pass)

| Section | Status | Sources read |
|---|---|---|
| §1 Overview & Scope | first-pass complete | (synthesis) |
| §2 Top-level architecture | first-pass complete | (synthesis) |
| §3 Threading model | CLOSED | cmaster.c ✓ ; cmbuffs.c ✓ ; cmasio.c ✓ ; netInterface.c spawn lines ✓ |
| §4 Queue topology | CLOSED | (synthesis from §3 + §7 reads) |
| §5 State machines | CLOSED first-pass; §5.4 DDC routing has open sub-read | console.cs:30058-30420, 19140-19200 ✓ |
| §6 TXA chain | CLOSED first-pass | TXA.c:1-680 ✓ ; per-stage setters defer to Phase 2 |
| §7 TCI TX-audio | CLOSED | TCIServer.cs:5339-5703, 1680-1707 ✓ ; cmaster.cs:1131-1830 ✓ |
| §8 HL2 EP2 wire | CLOSED first-pass | networkproto1.c:869-1200, 1204-1267, 380-413 ✓ ; ForceCandCFrame + obbuffs sub-reads open |
| §9 PureSignal hooks | first-pass (integration level) | calcc.c:1-100 ✓ ; iqc.c:1-130 ✓ ; PSForm.cs defers to v0.3 |
| §10 Implementation map | first-pass complete | (synthesis from §3-9) |
| §11 Declared deviations | structural detail #1 logged | — |
| §12 Open questions | 4 of original 4 resolved (Q1-Q4) ✓ ; remaining sub-reads logged in each § |

**Reference notes on disk** (`docs/refs/`):
- `README.md`
- `cm_thread_mechanism.md` (cmaster.c + cmbuffs.c)
- `wire_threads_and_mic_producer.md` (cmasio.c + networkproto1.c wire-recv/send)
- `mox_fsm_and_wire_frame.md` (console.cs MOX FSM + WriteMainLoop_HL2)
- `txa_chain.md` (WDSP TXA chain order + defaults)
- `tci_tx_audio_path.md` (TCIServer.cs + cmaster.cs TCI bridge)

### Pending sub-reads (do NOT block Phase 0 sign-off — flagged for
when each component lands in Phase 2)

1. `console.cs UpdateDDCs(rx2_enabled)` — full DDC routing matrix
   for §5.4. Needed when the `wire::DdcMap` file is implemented.
2. `console.cs HdwMOXChanged(bool, double)` + `AudioMOXChanged(bool)`
   internals — what they write/signal. Needed when keydown/keyup
   sequencers land.
3. `console.cs UpdateAAudioMixerStates()` — the AAmixer state
   product. Needed when wire::OutboundRing producers are wired.
4. `networkproto1.c:106 ForceCandCFrames` — the priming sequence.
   Needed when `wire::ForceCandC` lands.
5. The two `obbuffs.c` ring producers (LR + IQ) — the wire-send
   producer/consumer pairing. Needed when wire::Ep2SendThread
   lands.
6. `TCIServer.cs:5930, 5946, 5963, 5996` — the `audio_stream_*`
   handshake handlers that set `m_seenModernTxAudioNegotiation`.
   Needed when TciListener handshake parsers land.
7. The constants: `TCI_TX_EXTRA_BUFFER_MS`, `TCI_TX_MAX_OUTSTANDING`,
   `MAX_TX_AUDIO_QUEUE_BLOCKS`, `MAX_TX_AUDIO_QUEUE_COMPLEX_SAMPLES`.
   Needed when TciTxSupervisorThread + TciListener queue policy
   land.
8. `wdsp/PSForm.cs` operator FSM + `calc()` body + `xiqc()`
   per-sample math — defer to v0.3 PS implementation.

### Sign-off log

**Rule 2 exemption note (added 2026-06-05).** The signed sign-
off row(s) below are PRESERVED VERBATIM as a historical audit
artifact — the operator's signature ratifies the exact text in
the cell at the time of signing.  Any reference-application
name tokens that appear inside a signed sign-off cell are
exempted from the Rule 2 scrub (per the 2026-06-05 operator
decision) because rewriting a signed cell to neutral language
would alter the historical record.  The exemption applies ONLY
to text inside the table cells below — surrounding doc body
remains Rule 2 scrubbed.

| Date | Section | Rick sign-off | Notes |
|---|---|---|---|
| 2026-06-04 | first-pass end-to-end (§1-13) including §12 answers (10.4 approved / 10.3 follow 14-step as written / §15.27 defer cleanup / skip PowerSDR / kill-test bench-verify) | **✓ APPROVED** | Phase 0 closed. Phase 1 unblocked: archive `tx-rip-archive` + delete the 8 listed files from main + create empty file skeleton per §10.3 14-step order. Build target `lyra.exe` must continue to build (RX-only) at every step. |

---

*Doc started 2026-06-04 (task #113 in_progress, sub-task of
#112). Section reading order: §3 → §5 → §6 → §7 (closed Q#1,Q#2)
→ §8 (closed Q#4) → §9 → then synthesis pass §4 → §10 → §2 → §1
→ §13. §11 deviation entry #1 logged when the three-I/Q-layouts
finding surfaced. §12 closed all 4 original questions; sub-reads
parked per-section for when each implements in Phase 2.*
