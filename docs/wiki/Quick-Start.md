# Quick Start

The five-minute on-ramp. Brand new to Lyra? Do these in order and you'll be
listening — and talking — fast. This same guide is built into the app
(**Help → Quick Basics**).

> **Currently supported:** Windows 10 (64-bit, v1809+) or Windows 11, a
> DirectX 11 / OpenGL 3.3 GPU (built-in graphics are fine), and a **wired
> Ethernet** link to a Hermes Lite 2 / 2+. Full detail on
> [PC Requirements](PC-Requirements). *(Not on Windows? Linux and macOS are on
> the [Roadmap](Roadmap).)*

## 1 · Wire it up 🔌

- Connect the Hermes Lite to your PC with an **Ethernet cable** — wired, not
  Wi-Fi. Wi-Fi in the radio path is the most common cause of pops and dropouts.
- Power the radio on.

## 2 · Launch &amp; connect 📡

- [Download](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest) and run the
  installer, then open **Lyra**. It scans the network and lists any radio it
  finds.
- **Double-click your radio** to open it. On a fixed IP or different subnet, use
  **Add by IP**.
- *First launch only:* Lyra spends a few minutes tuning its DSP math to your
  CPU — a one-time step. Let it finish; it opens by itself.

## 3 · Start the radio ▶️

- Click **▶ Start** in the top header. The spectrum comes alive.

## 4 · Hear a signal 🔊

- Open **Settings → Audio → Out** and pick where you listen:
  **"HL2 audio jack (AK4951)"** (headphones plugged into the radio) or your
  **PC speakers / headset**.
- Turn the **Volume** up.
- **Click on the panadapter or the waterfall** to tune to a signal, and set the
  **mode** — SSB (**USB** above 10 MHz, **LSB** below), or **AM** / **FM**.

## 5 · Talk 🎙️

- See **[First Voice Setup (SSB / AM / FM)](First-Voice-Setup)** — it walks the
  microphone and PTT in a couple of steps, for a mic on the radio *or* on the PC.

---

## If something's not working

- **No radio found** → check the **wired** cable and that the radio is powered.
  Wi-Fi and sleeping network adapters are the usual culprits.
- **No audio** → check **Settings → Audio → Out** (right device?) and the
  **Volume**.
- **Can't transmit** → see [First Voice Setup](First-Voice-Setup) — most
  first-time TX problems are the mic not set up in Windows, or the wrong
  **Mic source** in Lyra.

More in the [FAQ &amp; Troubleshooting](FAQ-and-Troubleshooting).

## Where to go next

Everything lives in the in-app **Help** guide (open it from the **Help** menu,
or the cyan **?** on any panel) and in the [User Guide](User-Guide):

- 🎙️ [First Voice Setup](First-Voice-Setup) — your first phone contact, step by step
- 📖 [User Guide](User-Guide) — the panadapter, tuning, filters, bands, audio, TX rack, CW, profiles…
- ✅ [Feature Status](Feature-Status) — everything Lyra can do today

Welcome aboard, and 73.
