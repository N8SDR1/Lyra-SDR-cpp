# FAQ &amp; Troubleshooting

Quick answers to common questions and fixes. For full detail see the
**[User Guide](User-Guide)**; to report something new, open an
**[issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)**.

---

## General

**Where do I get help?**
**[Discord](https://discord.gg/BwjsQvjcSc)** for live debug and logs.
The **[Facebook group](https://www.facebook.com/groups/1994335921523916)**
for screenshots, on-air reports, and “will this radio Open.” Bugs that
need a paper trail: a
**[GitHub issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)**.


**What hardware does Lyra support?**
Hermes Lite 2 and 2+ over HPSDR Protocol 1, and **BrickSDR2 / Brick3SDR over Protocol 2
only**. **SUB / RX2** and **SPLIT** on all of them. One-converter radios
(HL2, Brick2, ANAN-10 / 100) draw RX2 on the main pane; **Brick3SDR /
ANAN-100D / 200D** open a **second panadapter**. Colours: orange **TUNE A**
/ cyan RX1 vs lime **TUNE B** / green RX2. N2ADR follows **RX1** on
one-ADC boxes. **ANAN-10 / 10E** TX like BrickSDR2; **ANAN-100-series** TX
like Brick3SDR (on-air). **G2** still dummy-load arm. Discovery **Hermes**
defaults to BrickSDR2 — pick the marketed ANAN model in Settings → Hardware.
A Brick or classic ANAN still on **Protocol 1** is **refused on Open**.
Flash **Protocol 2**; Brick FPGA help: **Anton (linoobs)** on Discord.
**7000DLE / 8000DLE / Orion MkII / Anvelina-Pro3** are **in tester hands**
(not locked; RX/BPF on the bench; TX dummy-load + drive ceiling). See
**[Supported Radios](Supported-Radios)**.

**Does Lyra support MIDI controllers?**
Not inside the radio. A USB knob that Windows treats as a **mouse wheel**
goes on **Settings → Hardware → Navigation**. A real MIDI surface needs a
**MIDI→TCI sidecar** you install yourself (for example
[midi2tci](https://github.com/ftl/midi2tci)), then **Settings → Apps → Browse**
to that `.exe`, with TCI on (default port **40001**). Skip RIT/XIT and
mixer maps until Lyra's TCI grows those. Details: in-app **Help → MIDI
controllers (sidecar)**.

**Which digital / SSTV programs are recommended?**
Lyra does not ship them. **Settings → Apps** lists programs we have tested
with: [STD](https://github.com/N8SDR1/STD/releases) (N8SDR's digital-modes
app; connects over TCI, which carries rig control and audio;
[details](https://n8sdr1.github.io/STD/)),
[Open SSTV](https://bucknova.github.io/Open-SSTV/),
[MSHV](http://lz2hv.org/mshv) or [WSJT-X](https://wsjtx.github.io/wsjtx/index.html)
for FT8/FT4, and [VarAC](https://www.varac-hamradio.com/). Browse to each
`.exe` after you install it.

**Is it free? What's the license?**
Yes — GPL v3+ (compatible with the WDSP DSP engine it uses). See
[NOTICE](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/NOTICE.md).

**Where's my version number?**
Header → the title/about area, or **Help → About**. Releases and notes are
on the [Releases page](https://github.com/N8SDR1/Lyra-SDR-cpp/releases).

**Does it run on macOS / Linux?**
The codebase is cross‑platform C++/Qt, but the shipped installer is Windows
only today. See
[CROSS_PLATFORM.md](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/docs/CROSS_PLATFORM.md).

---

## Connecting

**Lyra doesn't find my radio.**
- Make sure you pressed **▶ Start**, and the PC + HL2 are on the same LAN.
- You don't need an "Any Subnet" or "Via all NICs" option — Lyra **always**
  scans every adapter (including direct‑connect `169.254.x.x`) with both a
  limited ("any subnet") and subnet‑directed broadcast, for P1 and P2. So a
  no‑show usually means the radio's **reply** can't route back (different
  subnets, no IP assigned yet, or a firewall), not that a broadcast mode is
  off.
- Use **Settings → Hardware → Radio → Add by IP** and type the radio's
  address — this sends a directed probe and works across subnets or where
  broadcast is blocked. On a direct cable with no router, give the radio a
  static IP in the **same subnet** as the PC's NIC so its reply can get home.
- Check the radio is powered and its link LED is up; a direct NIC‑to‑radio
  cable is the most reliable setup.

**Do I need to "Run as administrator"?**
No. The installer adds the firewall rules Lyra needs. If you skipped that or
removed the rules, re‑run the installer. If Lyra **won't even launch** unless
you elevate: close leftover `lyra.exe` in Task Manager (or reboot), then start
normally. Current builds also start if a leftover instance's ping fails
instead of blocking forever. "Run as administrator" is not the intended way
to start Lyra.

**Lyra does nothing after an update (no window).**
From **v0.24.3**, a crash during GPU startup steps down OpenGL → software
and **turns off 4× MSAA** on that path (4× samples + software hung some
Intel UHD PCs). From **v0.24.4**, QML docks load **after** the window is
shown (one panel at a time) so a software/Intel hang at `building docks`
has a chance to finish. From **v0.24.5**, if you already picked **Vulkan /
Direct3D / OpenGL** in Settings, that pin is **kept** — a leftover
incomplete-start latch from an older installer no longer forces software
over a working GPU. Try:
1. Start again — the second/third launch is the recovery ladder (only when
   the backend is still Auto).
2. If it still never appears: `lyra.exe --safe` from a command prompt
   (software graphics, no auto-connect).
3. Then **Settings → Visuals → Graphics backend** — pick Vulkan, OpenGL, or
   Direct3D, Apply, restart. **Leave safe mode** if the banner is up.
   You do **not** need Revo or a registry wipe.

**Open says Protocol 1 ANAN / my Brick is listed but won’t connect.**
Discovery found a **Protocol 1** Hermes-class reply (typical for a Brick or
ANAN still on the old FPGA). Lyra **will not attach** that stream: P1 TX
bytes are for the **Hermes Lite 2**. Forcing it usually means no RF, wrong
PA/T/R, wrong power, or an unprotected RX while keyed. **Flash Protocol 2**,
rediscover, Open the **P2** row. Brick flash / image: **Anton (linoobs)** on
Discord. Detail: **[Supported Radios](Supported-Radios)**.

**Settings lists my Brick as Hermes / firmware v0.**
A Brick2 that still reports the Hermes board ID is labelled **Brick** when
Lyra already knows it **and it is on Protocol 2**. Firmware is
**v{code/10}.{code%10}** (e.g. code 106 → **v10.6**). Dual RX needs current
Brick2 FPGA; flashing does not change the Ethernet MAC. If the row is
**Protocol 1**, that is the refuse case above — not a label bug.

**It froze / connected to the wrong IP on launch.**
Fixed in current versions: Lyra probes the remembered IP first and
**self‑heals** by re‑scanning if the radio isn't there. Update to the
[latest release](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest).

**Switching between two radios doesn't work.**
**Close** the current radio before you **Open** another — only one is live
at a time. The connected one is shown green/bold in the list.

---

## Audio

**NNR vs NR vs NR-C — which one?**
- **NR** — classic WDSP EMNR (Modes 1–4, AEPF, NPE).
- **NNR** — WDSP 2.10 neural denoiser. **Standard** is the default (lighter
  CPU); **Premium** uses more and is comfortable on the Recommended PC tier.
  Mask runs **−10…−50 dB**, default **−40**. **NR** and **NNR** cannot run
  at the same time.
- **NR-C** — captured band-noise profile (your QTH). It can run with either
  NR or NNR.

**No receive audio.**
- Press **▶ Start**, raise **AF Gain / Volume**, and check the squelch
  isn't holding it closed.
- Confirm the **output path** (Audio panel): **HL2 audio jack** = plug into
  the radio; **PC sound device** = pick the right Windows output + host API.
- On the HL2 jack, some firmware swaps L/R — there's a swap option if one ear
  is silent.

**Pops / clicks / glitches in the audio.**
- Use a **wired** connection (ideally a dedicated NIC straight to the HL2),
  not Wi‑Fi.
- Tick the installer's **network‑throttling** option (or set
  `NetworkThrottlingIndex` accordingly) — it stops Windows starving the
  radio's UDP cadence.
- On the PC sound path, try **WASAPI Exclusive** for lower, steadier
  latency.

**How do I get RX audio into WSJT‑X / FLDigi / a logger?**
Use a **Virtual Audio Cable** (VAC) as the PC output, or **TCI**. See
**[User Guide → Digital modes](User-Guide#digital-modes--getting-rx-audio-to-another-program)**.
**Enable VAC1** starts the engine and RX→PC. Transmit from the cable also
needs **Settings → TX → Mic source = PC Soundcard (VAC1)** (or **Use VAC1
as TX source**) plus a VAC **Input** device. **VAC2** is the same pattern
for RX2 (enable **SUB** so VAC2 has audio). **TCI** as mic source is
exclusive — Lyra will not also arm VAC TX. If **Auto-enable for digital**
is on, switch to **DIGU / DIGL**; USB turns that auto path off.

---

## Transmit

**USB headset / PC mic: I hear RX, MOX, no RF (MIC stays 0).**
That is **Path B** — not a virtual cable. **Out** = headset **speakers**.
**Mic source = PC Soundcard (VAC1)**. VAC1 **Enable** on,
**Auto-enable for digital** **off** (USB/LSB otherwise kills VAC),
**Input** = headset **mic**, **Output** = `(none)`. Watch **MIC**, not
watts. Windows Sound → Input must move when you speak. Step list:
**[First Voice Setup → Path B](First-Voice-Setup)**.

**I key up but there's no/low power.**
- Make sure **TX drive** is above 0 and, if your build requires it,
  **Enable PA** is ticked (Settings → TX → Advanced). With the PA off the
  radio keys (relays click) but makes no power — by design.
- Power reads on the **meter** (set it to **PWR**); the HL2 telemetry banner
  also shows PA current.

**Does the meter show CW transmit?**
Yes (current versions). Sending CW from the console **or** a paddle/key
flips the meter to forward power and reds the VFO on‑air. The panadapter
stays on RX during CW so you still see the keyed carrier.

**No analog band voltage for GP100 / GPA100 / M0AWS DB9 / Xiegu XPA125.**
Stock Pico PWM is **J4 pin 8**, not J3. Xiegu-class amps (XPA125, Micro
PA50) decode **voltage**, not OC ticks. Analog follows TX frequency on
Protocol 1 **without** needing **Enable N2ADR / IO board** — that box
is OC / LPF relays only. Start the radio; Lyra writes TX Hz over I2C
**0x1D** (same as Thetis/Quisk/SparkSDR). There is **no extra I2C/PWM
checkbox**. **Leave HL2 Band Volts on J3 off** unless the amp is
actually jumpered to the **fan header**. 0 V on J3 with that box off is
expected when J3 is GND. Protocol 2 Brick/ANAN do not use the Pico 0x1D
path. Optional tester switch **`LYRA_NO_IOBOARD_FCODE=1`**:
[User Guide → How to set LYRA_NO_IOBOARD_FCODE](User-Guide#how-to-set-lyra_no_ioboard_fcode).

**Filter board stuck on the previous band.**
Install **v0.25.20**. The voltage is written after the band change
finishes, and a return to a band already showing on the dial is written
again. Stock PWM is **J4 pin 8**. Leave **Band Volts on J3** off unless
the amp is on the fan header. Protocol 2 Brick/ANAN do not use this path.

**I'm running an amplifier — anything to set first?**
Yes — configure **TR‑sequencing** and the **RF‑delay / ATT‑on‑TX**
protections before keying into an amp. Per-band **PA Gain / Full Output (W)**
and the optional **watts cap** live on **Settings → PA Gain** (including a
dedicated **11m** slot). Read
**[User Guide → external amplifier](User-Guide#operating-with-an-external-amplifier-hot-switch-protection)**
and **[Settings → PA Gain](User-Guide#settings--pa-gain)**. **USB-BCD**
(Settings → Hardware) drives Yaesu-style band codes to the amp; verify
the code on each band at low power before full power.

**PureSignal FB stays 0 / never goes green.**
You need the **coupler mod** (HL2) or Brick feedback path, **attestation**
ticked (Settings → TX), and **enough RF** — often **more than 2–3 W** on
a bare HL2 (try ~5–8 W / high drive into a dummy). Arm the **PureSignal**
dock, then key **2-tone**. FB **teal** = too low, **green** = 129–181,
**red** = too hot. SUB pauses while MOX+PS is live. See
**[User Guide → PureSignal](User-Guide#puresignal)**.

**JTDX / WSJT-X won't connect over TCI (MSHV and Open SSTV do).**
Those two clients expect ExpertSDR3 identity (`protocol:ExpertSDR3,2.0`,
SunSDR-class `device:`), **`start;` before `ready;`**, and an echo of
their own `start;` / `stop;`. “TCI SDR is not switched on” means that
handshake flag, **not** Lyra’s TCI checkbox. Settings → Network → both
emulate boxes on (default). Match Lyra's **port** (usually **40001**);
WSJT templates often still say **50001**. Rig = **Expert Electronics
(TCI)**, Network Server `127.0.0.1:<that port>`.

**TCI connects but TUNE / TX does nothing, or TX audio is silent.**
Press **▶ Start** first — TUNE and TRX are refused while the radio
session is stopped. **TUNE** keys the host **TUN** path; **TRX** keys
**MOX**. Set **Settings → TX → Mic source** to **TCI**. From **v0.25.14**,
CHRONO TX-audio frames use the negotiated sample count (not a fixed 1024).
STOP during TX does not drop MOX (v0.25.13). If an older 0.25.10–0.25.13
build attached but would not key, install 0.25.14 or later.

**BrickSDR2 TX stays on the old frequency after I move the VFO.**
Fixed in **v0.25.15**. First key-up was on the dial; after any retune, TX
stayed where you started until you restarted the stream. Install 0.25.15
(SSB, CW, AM, FM share that path).

**Lyra always loads the Default (SSB) profile even though I last used FM.**
Fixed in **v0.25.15**. Startup order is: mode-family binding → last-used
profile → Default (first-run fallback). Bind FM (or CW / Digital) in
**Settings → Profiles**. Step-by-step: in-app **Help → How to create a
profile**.

**Tuning Mem left-click does nothing after idle; right-click still works.**
Fixed in **v0.25.15**. Left-click opens the recall list again.

**MUTE is quiet, unmute is an echo / loop.**
MUTE only silences speakers / the HL2 jack. **VAC Output** and **MON TX**
can still play. If VAC Output is the same speakers (or loops back), you
hear two copies. Use a dedicated VAC device, or turn VAC / MON TX off.

**Windows mixer jumps to full / unmutes after Stop or Start.**
Fixed in **v0.25.17** (Issue #18). Lyra now remembers its **per-app**
Windows mixer volume across Stop / Start and relaunch. That slider is
**not** Lyra **Vol** / **AF**. If an older build slammed you to 100 %,
install 0.25.17 or later, set the mixer once, then Stop / Start again.

**What is Auto Step / MAP (Protocol 2)?**
**Step** on DSP+Audio walks the ADC pad **0–31** (clip +3 / unwind −1),
frozen while keyed. Default **off**. **MAP** (Brick3 / 100-series) is
per-band **ANT** and **RX jack** chips — RX2 is Ext1, not ANT2. That
jack is also the second input for **Diversity** (below). MAP itself
does not arm the mix.

---

## Settings &amp; layout

**How do I rearrange / lock the panels?**
Drag a panel by its title bar — a cyan zone previews where it lands (edge /
split / tab / float). Drag the cyan separators to resize. **View → Layouts**
has four save‑able slots plus the factory default. **View → Lock panels**
(Ctrl+L) freezes move *and* resize. See
**[User Guide → Getting around the window](User-Guide#getting-around-the-window)**.

**Factory layout looks fine, then restart is scrambled (GitHub #20).**
Uninstall does **not** wipe the layout. It lives in
`%APPDATA%\N8SDR\Lyra-cpp` and the registry
`HKCU\Software\N8SDR\Lyra-cpp`. **View → Layouts → Recall → Lyra default
(factory)** then close: the next start rebuilds factory (it no longer
saves a broken photograph of it). **Settings → Backup & Restore → Reset
to factory defaults** clears everything. **`--safe`** starts factory
without touching the saved slot.

**Back up or move my settings to another PC.**
Settings export/import writes a single profile file you can copy. See
**[User Guide → Backing up &amp; sharing your settings](User-Guide#backing-up--sharing-your-settings)**.

**First launch is stuck "optimizing".**
That's the one‑time FFT plan‑cache build — let it finish (a few minutes).
It only happens once (or after **Clear &amp; rebuild**). Companion apps
(SDRLogger+ Hardware Startup) wait until that cache and the radio layer
are up. If **0.25.10** vanished during optimizing, install **0.25.11** and
close leftover SDRLogger+ in Task Manager before the first run.

**Xvtr VFO stops at 55.999.999 Hz.**
That was the old eight-digit LED cap. **v0.25.3+** uses ten digits so 2 m /
70 cm / 23 cm **RF** show on the dial. Slot **RF low/high** were already
MHz. Band-plan **region does not auto-fill** 144–146 vs 144–148 — set it
in the Xvtr editor (**right-click** the chip). Full map: **[User Guide →
Xvtr](User-Guide#xvtr-transverters)**.

**Transverter IF is on the Hermes Lite Alt RX jack (or I hear nothing on 2 m).**
Protocol 1 shows **RF1** and **Alt** on the Band dock Xvtr row. **RF1** is
the PA / N2ADR antenna SMA. **Alt** is the Pico / IO **Alt RX** SMA.
Click **Alt** once on that Xvtr chip; it is remembered. Do **not** click
Alt if you have only one SMA. GPA100 / J4 analog band voltage is a
different path — leave **RF1 / Alt** alone unless the IF is on Alt RX.
Protocol 2 hides these chips (use **Ant / MAP**). Station table:
**[User Guide → Which Hermes Lite station is mine?](User-Guide#which-hermes-lite-station-is-mine)**.

**Where did the RTTY decoder go?**
Lyra does **not** ship an in-radio RTTY modem. Use **Settings → Apps** (or
the **Apps** header chip) to launch fldigi / MMTTY / etc., with **TCI** or
**VAC** for audio. **CW** receive decode and CW sending stay in Lyra. See
**[User Guide → Apps](User-Guide#apps-companion-programs)** and
**[Reading CW](User-Guide#reading-cw--the-decoder)**.

---

## Dual receive (SUB / RX2)

**Brick3SDR / ANAN-100D / 200D — second panadapter?**
Yes. Two converters: **SUB** opens a **second spectrum** (own waterfall,
band-plan, 11 m chips, click-tune). Settings → Visuals has **SUB floor /
ceiling** (per RX2 band) and an optional **SUB layout**. One-converter
radios (HL2, Brick2, ANAN-10 / 100) still draw RX2 as the **green overlay**
on the main pane.

**Does TX protect the RX2 jack?**
On two-converter radios, **RX2 jack isolate on TX** (default **on**)
grounds/disconnects that jack for the whole key-down. ATT-on-TX still pads
both ADCs; it does not open the analog RX2 path. Diversity uses that
jack in receive and drops the mix for the whole key, so the isolate
and the pause agree.

**What is Diversity, and how is it different from SUB?**
**SUB** is a second receiver on its own frequency. **Diversity** mixes
the two converters on the **same** frequency (gain and phase) so a
fading signal can be filled in from the **RX2 jack**. They are
exclusive: turning Diversity **ON** turns **SUB** off. HL2 and
BrickSDR2 have one converter and do not show the **DIV** chip.
Brick3SDR is confirmed; ANAN-100D / 200D use the same path.

**How do I turn Diversity on?**
Press the **DIV** chip (with PureSignal and Amp View). That only
opens the dock — size and place are remembered. Press **ON** in the
dock to arm the mix. Put the second antenna on the **RX2 jack**
(Ext1 on the MAP chips), not ANT2. Drag the polar dot, or step gain
(**−27…+27 dB**) and phase (**−180…+180°**). Each band keeps the last
pair you used; a band you have never set starts at **0 dB / 0°**.
The on/off switch is global, not per band. While you transmit, or
while PureSignal is live, the mix drops and comes back when you unkey.

**How do I turn on the second receiver?**
Press **SUB** on the Tuning panel (it lights **green**). There is **no**
“enable RX2” checkbox under **Settings → Network**. TCI clients use
`rx_enable:1` (or the SUB button). Setting an RX2 frequency does **not**
auto-enable SUB.

**SUB vs SPLIT — which is which?**
**SUB** is a second *receiver* (its own frequency, mode, ear). **SPLIT**
moves *transmit* to VFO B. They are independent — both, one, or neither.
FM **RPT** still replaces SPLIT in FM.

**What do the colours mean?**
**Orange TUNE A** + **cyan** passband = RX1. **Lime TUNE B** + **green**
passband / carrier = RX2. Band chips: **red** glow = RX1’s band, **green**
glow = SUB’s band. Lime **TX** line (red while keyed) is **SPLIT**, not
RX2. Full map: **[User Guide → Second receiver](User-Guide#second-receiver-sub--rx2--how-it-works)**.

**What are ◀ RX2 and RX2 ▶?**
Chips on the **left** or **right** edge of the panadapter when SUB is on
but RX2 is **off the current span**. **Click** to swap VFOs onto that
parked frequency. On one-ADC radios the waterfall is RX1 IQ (cross-band
SUB is weaker). On two-ADC radios the **SUB pane** shows RX2’s own IQ.
**◀ TX** / **TX ▶** are the same idea for SPLIT TX — don’t mix them up.

**How do I hop SUB to another band without moving VFO A?**
**Shift+click** or **right-click** a Ham / BC / 11m chip. Plain **click**
stays the focused VFO (usually A). GEN / TIME / Mem stay RX1-only.

**Why does a band chip show 1, 2, or 3?**
Each Ham / BC / 11m chip stores three frequencies. Click the band you
are already on to step to the next. A click from another band returns
the slot you last left, without skipping. Shift+click and right-click
still hop SUB and do not step that stack. SUB keeps its own three.

**Can RX and TX use different low cutoffs?**
Yes, on the current build. Filters dock: **RX lo** under **RX BW**,
**TX lo** under **TX BW** (0–500 Hz, SSB and digital). The lock ties
them. Unlock and they stay apart. An older profile loads the old
shared low on both sides until you change one.

**Can TCI / SDRLogger+ drive RX2?**
Yes. `vfo:1,0` / `dds:1` = SUB frequency; `vfo:0,1` = SPLIT VFO B (does
**not** turn SUB on). Calibrated S-meter: `rx_channel_sensors` **0,0** =
RX1, **1,0** = RX2 (quiet floor ~−140 dBm if SUB is off). Combo auto-RST
still uses **RX1** only — see **[SDRLogger+ Combo](SDRLogger-Plus-Combo)**.

**Hermes Lite 2 second receiver?**
Same **SUB** button as BrickSDR2 (Protocol 1 DDC1). One ADC + N2ADR
filter follows RX1 — same-band SUB is full strength; another ham band
on RX2 is much weaker. Status log warns once when the amateur bands
differ.

**11 m chips on SUB?**
From **v0.25.18** the SUB pane shows the same 11 m / CB channel chips as
the main panadapter. Click-tune **mode follows Settings → Hardware →
Region** (AM vs USB/LSB).

---

Still stuck? Open an
**[issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** with your
Windows version, Lyra version, radio (HL2 / HL2+ / BrickSDR2 / Brick3SDR
+ gateware), and what you saw.
