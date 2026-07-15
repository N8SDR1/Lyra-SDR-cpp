# PC Requirements

Lyra is a native **64-bit Windows** application. Its panadapter and waterfall
render on the GPU, so graphics capability matters a little more here than in a
typical desktop app — but Lyra runs comfortably on modest hardware, and
**built-in / integrated graphics are fine**.

> **Operating system:** Windows 10 (64-bit, **v1809 / build 17763** or newer)
> or Windows 11. **32-bit Windows is not supported.** Linux and macOS are on
> the [Roadmap](Roadmap) for a future version.

## The three tiers

| Component | 🟢 Minimum | 🔵 Recommended | 🟣 Maxed+ |
|---|---|---|---|
| **Operating system** | Windows 10 64-bit, v1809 (build 17763)+ or Windows 11 | Windows 11, or Windows 10 22H2 | Windows 11, current release |
| **CPU (Intel)** | 4th-gen Core (Haswell, ~2013) or newer | Core i5 / i7, 10th-gen or newer | Core i7 / i9, 12th-gen or newer |
| **CPU (AMD)** | Any Ryzen, or an AVX-capable FX / A-series | Ryzen 5 / 7, 3000-series or newer | Ryzen 7 / 9, 5000-series or newer |
| **Cores** | Dual-core (quad preferred) | Quad-core+ with AVX2 | High-core-count with AVX2 |
| **Memory** | 4 GB (8 GB comfortable) | 16 GB | 32 GB |
| **Storage** | ~300 MB free (SSD recommended) | SSD, ~500 MB free | NVMe Gen4 SSD |
| **Network** | 100 Mbit **wired** Ethernet to the radio | Gigabit Ethernet, ideally a dedicated NIC | Dedicated Gigabit / 2.5 GbE NIC, wired straight to the radio |
| **Display** | 1600 × 900 | 1920 × 1080 | Dual / triple monitor; 1440p or 4K |

## Display — what the minimum actually buys you

Lyra's front panel is a set of dockable panels, and they need real estate. So
rather than just quote a number, here is exactly what you get at each size.

At **1600 × 900**, Lyra ships arranged with the panels you need to operate —
all of them fully reachable, nothing cut off:

| Panel | What it's for |
|---|---|
| **Panadapter + waterfall** | See the band, click to tune |
| **Tuning** | VFO, step, mode, RIT / XIT, split |
| **Band** | Band switching |
| **Filters** | Sample rate, RX and TX bandwidth |
| **Audio** | LNA, volume, AF gain, mute, and the RX DSP row (NB / NR / ANF / LMS / SQ …) |
| **Meter** | S-meter on receive; power / SWR / ALC on transmit |
| **TX** | Drive, mic gain, ATT, PROT, VOX, Tune, MOX |

The remaining panels are **closed by default** at that size and available any
time from the **View** menu — but each wants screen space you don't have spare
at 1600 × 900, so opening one means giving something else up:

| Optional panel | Note |
|---|---|
| **Solar / Propagation** | A wide strip — needs ~780 px of width on its own |
| **Display** | Panadapter zoom, frame rate, waterfall rate, peak hold |
| **Profiles** | TX/RX profile picker and Save |

At **1920 × 1080** you can have all of them open at once with room to spare.
That's why it's the Recommended tier.

**Why not 1366 × 768?** It's height, not width, that binds. Below about 900 px
there isn't room for a usable panadapter *and* the operating panels without
something being clipped off the bottom — so 1366 × 768 doesn't work even for
the minimal set. Lyra will still start on a smaller display, but the window
won't be able to shrink to fit it.

## Graphics / video — in detail

Lyra draws the spectrum and waterfall on the GPU. It works on any
**DirectX 11 / OpenGL 3.3-class** card (the Minimum tier), but the **Vulkan**
path is smoother, uses less CPU, and is vendor-neutral — it runs equally well
on NVIDIA, AMD, and Intel. That's why Recommended and Maxed+ call for a
Vulkan-capable GPU: it's what lets high-FPS waterfalls scale cleanly across
large or multiple monitors.

| GPU tier | 🟢 Minimum | 🔵 Recommended | 🟣 Maxed+ |
|---|---|---|---|
| **API** | DirectX 11 / OpenGL 3.3 (integrated OK) | **Vulkan 1.1-capable** | **Vulkan 1.3-capable** dedicated GPU with ample VRAM |
| **NVIDIA** | GeForce GT 700-series or newer | GeForce GTX 10-series / RTX or newer | GeForce RTX 30 / 40-series |
| **AMD** | Radeon HD 7000 (GCN) or newer, incl. integrated | RX 400 / 500 / Vega / RX 5000–9000 (RDNA) | RX 6000 / 7000 / 9000 (RDNA 2/3/4) |
| **Intel** | HD Graphics 4000 (Ivy Bridge) or newer / UHD / Iris | UHD 620 / Iris Xe / Arc | Arc A-series or newer |

*(If a GPU or driver ever misbehaves, Lyra automatically falls back to a safer
graphics backend on the next launch — you're never stuck. You can also pick the
backend yourself in **Settings → Visuals → Graphics backend**.)*

## What the higher tiers actually buy you

Lyra runs fine on the **Recommended** spec — the extra CPU/GPU headroom in
**Maxed+** isn't about *whether* it runs, it's about running *everything at
once* without the machine breaking a sweat: the full TX speech-processing rack,
spots, high-refresh spectrum and waterfall on a big screen, and comfortable
margin for the heavier DSP features on the roadmap (dual receiver, PureSignal).

## Two things that matter on any tier

- 🔌 A **wired** connection to the Hermes Lite is the single biggest factor in
  glitch-free audio. **Wi-Fi in the radio path** is the most common cause of
  pops and dropouts.
- ⚙️ On first launch, Lyra tunes its FFT math to your CPU and caches it — a
  **one-time** step that takes a few minutes (a faster CPU finishes sooner).
  You can rebuild it later from **Settings → Backup &amp; Restore** after a CPU
  change.

---

**See also:** [Quick Start](Quick-Start) · [Supported Radios](Supported-Radios) · [Feature Status](Feature-Status)
