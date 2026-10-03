# Protocol 2 wire engine — design (BrickSDR2 / ANAN-P2)

Status: **SCOPING / design locked, no code.** Grounds the native
C++23 HPSDR **Protocol 2** wire engine that lyra-cpp needs to talk to
Protocol-2 radios — first target the **BrickSDR2 14-bit** (operator's
unit), forward-compatible with the ANAN G2 / P2 family.

lyra-cpp is Windows-only, C++23 / Qt6. Provenance rule (standing):
**study the reference, implement Lyra-native, no code copy.** All
file:line citations below are for study only.

---

## 1. What the reference is (ramdor/Thetis)

Cloned to `D:/sdrprojects/ramdor-Thetis` (the ramdor fork, which has
Protocol 2, unlike the HermesLite-only path we ported from before).

Thetis's entire P1/P2 wire layer is a **native C library**,
`ChannelMaster.dll`, with **full GPL source in the repo** at
`Project Files/Source/ChannelMaster/`. The C# side
(`Console/HPSDR/NetworkIOImports.cs` — 248 `DllImport`s;
`clsRadioDiscovery.cs`) is only discovery + orchestration on top; it
carries **no packet framing**. The authoritative framing lives in:

| File | Role |
|---|---|
| `ChannelMaster/network.c` (1494 ln) | UDP sockets, endpoints, discovery, send/recv loops, C&C builders |
| `ChannelMaster/netInterface.c` (1689 ln) | command/data packet builders, per-field setters |
| `ChannelMaster/networkproto1.c` | the P1/HL2 path (already our lyra-cpp reference) |
| `ChannelMaster/network.h` | struct defs, `enum { … ETH = 1 }` (P2), bit fields |

### 1.1 How Thetis structures P1 vs P2 (the key structural finding)

Thetis uses **one engine with a per-radio protocol flag**, not a fork
and not clean classes:

- `nativeInitMetis(…, int protocol, int model_id, …)` stores the
  selected protocol (`network.c:84`).
- A single global/per-radio `RadioProtocol` field carries it
  (`network.h:452`: `ETH = 1 // Protocol ETH (P2)`).
- One `ReadThreadMainLoop()` (`network.c:645`) handles both.
- The shared send/recv/C&C functions branch **inline** on
  `if (RadioProtocol == ETH) { …P2… } else { …P1… }` at ~8 scattered
  points (`network.c:909, 1061, 1177, 1246, 1259, 1298, 1372, 1387`).

That inline-conditional style is exactly what we do **not** copy —
it's the tangle the NO-PATCHING / rewrite-correctly rule exists to
avoid. See §2.

---

## 2. lyra-cpp architecture decision

**Follow the structure the wire layer already committed to** — which
is exactly Thetis's: one engine, a `radioProtocol` field, protocol-
branched code. lyra-cpp's `src/wire/` is a **verbatim reference port**
of ChannelMaster, and it already carries the pieces:

- `enum class RadioProtocol { USB = 0, ETH = 1 }` and the live global
  selector `radioProtocol` (`RadioNet.h:159`, `RadioNet.cpp:405`,
  default `USB` = P1) — the direct mirror of Thetis's `RadioProtocol`
  shadow variable.
- The P2 (`ETH`) branches are **already present as DEFERRED reference
  text** — the wire layer's house discipline is to port ChannelMaster
  verbatim and carry not-yet-implemented paths as commented reference
  code tagged `DEFERRED [feature — version]`. `Network.cpp` already
  holds the `if (radioProtocol == RadioProtocol::ETH)` branches
  (`network.c:1246-1341`) this way, tagged "Protocol 2 / ANAN is v0.4
  scope."
- C&C framing lives in `FrameComposer.h` as free functions over the
  global `radionet` (`write_main_loop_hl2()`, `set_rx_freq()`,
  `set_tx_freq()`, `set_pa_on()`, …) — the P1 counterpart of Thetis's
  `WriteMainLoop_HL2` / `netInterface.c` setters.

So P2 is **not** a new polymorphic engine and **not** a `WireEngine`
interface — imposing either would be a rewrite against an already-
reference-faithful structure (and against the standing
reference-fidelity discipline). P2 = **un-defer the `ETH` branches and
fill the missing P2 units**, driven by the existing `radioProtocol`
selector.

> Course-correction note: an earlier draft of this doc proposed a
> `WireEngine` interface with P1/P2 implementations. That was wrong for
> this codebase — the grounding read showed the wire layer had already
> adopted Thetis's protocol-field-branch structure with the ETH path
> stubbed as deferred reference text. We follow that.

**What's genuinely missing (the P2 work):**

- **P2 discovery** — `hl2_discovery.{h,cpp}` is P1-specific (63-byte
  probe → :1024 broadcast). P2 discovery is a different packet; add it
  either as a `radioProtocol`-branch in the discovery path or a P2
  sibling that populates the same `RigRegistry`/`RadioCapabilities`.
- **P2 C&C composer** — a `write_main_loop_p2()` (or the un-deferred
  `ETH` branches in the shared composer) mirroring Thetis's P2 general
  (1024) + high-priority (1025) command build in `netInterface.c`.
- **P2 RX/TX data plane** — the DDC IQ recv + DUC TX paths, un-deferred
  from the `ETH` branches in `Network.cpp` / the recv thread.

**Selection wiring** — `RadioCapabilities.protocol`
(`src/rig/RadioCapabilities.h:62`, `1|2`) from the active `RigProfile`
sets `radioProtocol` at connect-time. Today it always resolves to
`USB`/P1; a BrickP2/AnanP2 rig sets it to `ETH`.

`HL2Stream` stays the Qt-facing `QObject` operator surface; the P2
branches live below it in the wire layer, exactly as the P1 code does.
Plugs into the rig layer already built: `RigRegistry` (MAC identity),
`RadioCapabilities` (`AnanP2`/`BrickP2`, `protocol=2`, `adcBits=14`),
`RigScope` (per-rig config).

---

## 3. Protocol 2 wire facts (from `network.c` / `netInterface.c`)

The data a `NetworkProto2` unit must implement. **To be filled to
byte-level at implementation time** — this is the endpoint/plane map,
not the final packing spec.

### 3.1 UDP endpoints

| Port | Dir | Purpose (Thetis) |
|---|---|---|
| **1024** | host→radio | General C&C (`CmdGeneral`, `network.c:821`) |
| **1025** | both | Rx-specific C&C + **60-byte High-Priority C&C** (`CmdRx`; `SendHighPriority`; recv `network.c:519` "60 bytes - High Priority C&C data") |
| **1026** | host→radio | DUC / TX (audio + TX-IQ) — to confirm at impl time |
| **1027** | radio→host | 16-bit raw ADC data, 1024-byte frames (`network.c:774`) |
| (DDC data ports) | radio→host | per-DDC RX IQ streams — enumerate at impl time |

`p2_custom_port_base` handling (`network.c:112` = 1025) + a
`p2hw_uses_different_ports` init flag exist — some P2 hardware remaps
ports; capture that as a capability, not a hardcode.

### 3.2 Discovery

Discovery reply carries **`MACAddr` (6 B)**, **`fwCodeVersion`**,
**`BoardType`** (`network.c:349-359`). BoardType → `RadioFamily`
(`familyForBoardId` already stubbed; add the P2 board ids —
BrickSDR2 ≈ ANAN-10E-class). MAC → `RigRegistry` id (already the
MAC-keyed scheme). This is the identity source for the Rig picker.

### 3.3 Data / control planes to build

1. **Discovery** — broadcast probe, parse reply → capabilities + rig
   identity.
2. **RX data plane** — DDC configuration + per-DDC IQ recv → feed the
   existing WDSP RX chain (same sink the HL2 path uses).
3. **C&C control plane** — general (1024) + 60-byte high-priority
   (1025): freq (DDC LO), sample rate, LNA/gain, MOX.
4. **TX / DUC plane** — high-priority TX + DUC (1026): TX-IQ + audio,
   PA/drive. Gated behind the same default-OFF PA discipline as the
   HL2 TX work.

---

## 4. Staged build order

Each stage is a small, independently benchable commit — mirrors the
multi-rig config staging. RX before TX; TX gated behind default-OFF PA.

| Stage | Deliverable | Bench gate |
|---|---|---|
| **P2-0** | Wire `RadioCapabilities.protocol` → `radioProtocol` at connect (P1 rigs still resolve to `USB`, **no behaviour change**) | HL2 still works identically |
| **P2-1** | P2 **discovery** → populate `RigProfile` + capabilities | BrickSDR2 appears in Rig picker with correct identity (no RX/TX — inert) |
| **P2-2** | P2 **RX** — un-defer the DDC config + IQ recv `ETH` branches → WDSP RX | Hear the Brick |
| **P2-3** | P2 **C&C** — `write_main_loop_p2` / un-deferred composer: freq / rate / gain / MOX | Tune, rate-switch, LNA on the Brick |
| **P2-4** | P2 **TX / DUC** — un-defer the `ETH` TX branch, default-OFF PA, dummy-load first | Follows the HL2 TX safety-gate ritual |

Each stage un-defers a bounded set of the `ETH` branches already
carried as reference text. P2-0 is the only stage touching the shared
selector (a one-line wiring, P1 unchanged); everything after is the P2
branches, inert for P1 rigs by construction.

---

## 5. Forward-compat / capability notes

- `nddc` / DDC count is P2-family-specific (ANAN G2 = 4, 7000DLE = 7,
  Brick TBD) — read from capabilities, never hardcode.
- P2 gives true per-DDC sample-rate independence (unlike HL2 P1's
  shared wire rate) — the engine must not assume a single global rate.
- BrickSDR2 capability profile = **Hermes-class** (mic / PTT / input
  options, **14-bit ADC**) over **Protocol 2**, auto-detected. Timmy's
  working Thetis config for the Brick uses Radio Model = HERMES with
  Auto-detect Protocol → P2; the "HERMES model" is the *capability*
  profile, the wire is P2.
- One rig at a time (locked earlier) — no simultaneous P1+P2.

---

## 6. Provenance

Study-only references (no code copied into lyra-cpp; GPL-compatible):
`ramdor/Thetis` `Project Files/Source/ChannelMaster/{network.c,
netInterface.c, network.h}` for P2 framing; `networkproto1.c` for the
P1 contrast. WDSP DSP ports remain the only attributed code per the
project rule.
