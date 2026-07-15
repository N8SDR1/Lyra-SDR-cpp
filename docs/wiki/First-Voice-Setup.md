# First Voice Setup (SSB · AM · FM)

New to the radio and just want to **talk**? This is the short, no-jargon path
to your first voice contact. It covers the phone modes — **SSB** (USB/LSB),
**AM**, and **FM**. The audio wiring is the **same for all three** — you just
pick the mode you're operating. *(Digital modes like FT8 use a different path —
see the [User Guide](User-Guide).)*

> **Start with one question — where are your microphone and headphones/speaker
> plugged in?**
>
> - Into the **radio** (it has a MIC jack and a PHONES jack on the box) → **Path A**
> - Into your **computer** (USB headset, a PC mic, PC speakers) → **Path B**
>
> Follow only your path.

---

## 🟢 Path A — mic &amp; headphones plugged into the radio  ✅ simplest

This needs an **HL2+ / AK4951** — the version with audio jacks on the box.
Nothing touches Windows sound settings.

1. Plug your headphones or speaker into the radio's **PHONES** jack.
2. Plug your microphone into the radio's **MIC** jack.
3. **Settings → Audio → Out = "HL2 audio jack (AK4951)"**.
4. **Settings → TX → Mic source = "Mic In"**. If people say you're quiet, tick
   **Mic Boost (+20 dB)**.
5. Pick your mode and frequency — SSB (**USB** above 10 MHz, **LSB** below), or
   **AM** / **FM** as the band or net calls for.
6. Hold your mic **PTT** (or a foot switch, or click **MOX**) and talk — watch
   the **MIC** meter move as you speak. That means it's hearing you.

That's it — no Windows sound settings needed.

---

## 🔵 Path B — mic &amp; headphones plugged into the computer

Works on any HL2 or HL2+.

**Hearing the radio (RX):**

1. **Settings → Audio → Out =** your PC speakers/headset (pick your Windows
   playback device from the list).

**Talking (TX) — through a computer microphone:**

2. Know which kind of mic you have — both appear in Windows as an **input
   device**:
   - a **USB** microphone or USB headset shows up as its own device;
   - an **analog** mic in the PC's pink MIC jack, or into a sound-card / USB
     interface input, shows up as that card's *Microphone* / line-in.
3. **Set the mic up in Windows first** — Lyra can't fix a mic Windows can't
   hear. In **Windows → Settings → Sound → Input**, choose that microphone and
   set its **mic level** to a usable value (not muted, not maxed). Many mics
   ship muted or at 0; some USB mics/headsets also have their own manufacturer
   app or a hardware volume/mute — check it. Speak and confirm the Windows input
   bar moves.
4. Point Lyra at it — **Settings → TX → Mic source = "PC Soundcard (VAC1)"**,
   then **Settings → Audio → VAC1** and set the **Input device** to that same
   Windows microphone.
5. Pick your mode and frequency (as in Path A step 5), hold **MOX** (or your
   PTT), and talk — watch Lyra's **MIC** meter move. No movement → go back to
   step 3 (Windows).

---

**See also:** [Quick Start](Quick-Start) · [User Guide](User-Guide) · [FAQ &amp; Troubleshooting](FAQ-and-Troubleshooting)
