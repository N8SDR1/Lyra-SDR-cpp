# Lyra — Getting Started (Quick Basics)

*The five-minute on-ramp. Brand new to Lyra? Do these in order and you'll be
listening — and talking — fast. Everything here is covered in more depth in
the in-app **Help** guide.*

---

## Currently supported

Lyra is a native **Windows** application.

- **Windows 10 (64-bit, v1809 or newer)** or **Windows 11** — 32-bit is not
  supported.
- A **DirectX 11 / OpenGL 3.3-capable** GPU (built-in graphics are fine).
- A display of at least **1600 × 900**. That fits the panels you need to
  operate — panadapter and waterfall, Tuning, Band, Filters, Audio, Meter and
  TX. The optional panels (Solar / Propagation, Display, Profiles) are closed
  by default at that size and want a bigger screen; **1920 × 1080** holds
  everything at once.
- A **wired Ethernet** connection to the radio — this is the single biggest
  factor in glitch-free audio.
- A Hermes Lite 2 or 2+ (HL2 / HL2+).

*(Full detail is in the Help guide under "System requirements". Linux/macOS
are on the roadmap but do not run today.)*

---

## 1 — Wire it up 🔌

- Connect the Hermes Lite to your PC with an **Ethernet cable**. Wired, not
  Wi-Fi — Wi-Fi in the radio path is the most common cause of pops and
  dropouts.
- Power the radio on.

## 2 — Launch & connect 📡

- Open **Lyra**. It scans the network and lists any radio it finds.
- **Double-click your radio** in the list to open it.
- On a fixed IP or a different subnet? Use **Add by IP** and type the radio's
  address.

*(First launch only: Lyra spends a few minutes tuning its DSP math to your
CPU — a one-time step. Let it finish; it opens by itself.)*

## 3 — Start the radio ▶️

- Click **▶ Start** in the top header. The spectrum comes alive.

## 4 — Hear a signal 🔊

- Open **Settings → Audio → Out** and pick where you listen:
  - **"HL2 audio jack (AK4951)"** — headphones plugged into the radio, or
  - your **PC speakers / headset**.
- Turn the **Volume** up.
- **Click on the panadapter or the waterfall** to tune to a signal, and set
  the **mode** — SSB (**USB** above 10 MHz, **LSB** below), or **AM** / **FM**.

## 5 — Talk 🎙️

- See **First voice setup (SSB / AM / FM)** in the Help guide — it walks the
  microphone and PTT in a couple of steps, for a mic on the radio *or* on the
  PC.

---

## If something's not working

- **No radio found** → check the **wired** cable and that the radio is powered.
  Wi-Fi and sleeping network adapters are the usual culprits.
- **No audio** → check **Settings → Audio → Out** (right device?) and the
  **Volume**.
- **Can't transmit** → see **First voice setup** — most first-time TX problems
  are the mic not set up in Windows, or the wrong **Mic source** in Lyra.

## Where to go next

Everything lives in the in-app **Help** guide (open it from the **Help** menu,
or the cyan **?** on any panel):

- **First voice setup** — your first phone contact, step by step.
- **The panadapter, Tuning, Filters, Band, Audio** panels — the day-to-day
  controls.
- **Settings** — audio devices, TX, CW, CAT, network, and more.
- **Session recorder** — capture RX audio + snapshots and turn them into a
  video.

Welcome aboard, and 73.
