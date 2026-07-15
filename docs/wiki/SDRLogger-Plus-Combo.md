# SDRLogger+ Combo link

**[SDRLogger+](https://n8sdr1.github.io/SDRLoggerPlus/)** is the companion
logging program — a modern contest/DX logger by the same developer. Over an
ordinary **TCI** connection it already drives Lyra and shows spots, but turning
on **Combo** upgrades that one-way link into a two-way *collaboration* that runs
over the **same TCI socket** — no bridge app, no second connection, no extra
port. The CW Console, the CW Decoder and the logger's Log-Entry row start
working as one.

> 🌐 **Get SDRLogger+:** [n8sdr1.github.io/SDRLoggerPlus](https://n8sdr1.github.io/SDRLoggerPlus/)
> · [Download / source](https://github.com/N8SDR1/SDRLoggerPlus)

## Turn it on

1. **Settings → Network → tick "SDRLogger+ Combo (share CW Console contact)".**
   It's **off by default**, per-machine, and remembered.
2. Make sure **TCI server running** is on (Combo rides the TCI link), and that
   **SDRLogger+ is connected to Lyra as a TCI client** — point its radio / TCI
   connection at Lyra's IP and port (the same **50001**).
3. When the two are linked, SDRLogger+ shows a **`● Lyra Combo`** badge in its
   Log-Entry header — that badge is your confirmation the link is live.

## What Combo does — four things, automatically

| | |
|---|---|
| 📇 **Call → logger** | Put a call in Lyra's CW Console **His call** — type it, or grab it from the [CW decoder](User-Guide) — and it lands in SDRLogger+'s log entry and fires its callbook (QRZ / HamQTH) lookup. You copy the call once, in Lyra. |
| 👤 **Name → back to `{NAME}`** | After the lookup resolves, SDRLogger+ sends the operator's **first name** back to Lyra, filling the **`{NAME}`** token. A reply macro like `{CALL} DE {MYCALL} GE {NAME}` greets them by name with no typing. |
| 📶 **Received signal → RST** | SDRLogger+ can auto-fill the **S** digit of **RST-Received** from Lyra's shared, *calibrated* S-meter — the number it logs is exactly what your meter shows. A signal-to-noise figure rides alongside so it fills only on a real signal, not band noise. Turn on **S-auto** next to the RST-Rcvd field. Works on SSB / CW / digital. |
| ✅ **One-click log with `{LOG}`** | Add the **`{LOG}`** action token to a CW macro — e.g. `TU 73 {MYCALL} ee {LOG}` — and sending it sends the sign-off *and* logs the QSO in SDRLogger+ (call, RST, mode and frequency all stamped from the shared state). A macro that is **only** `{LOG}` is a log-only button that keys nothing. |

Combo is a Lyra ↔ SDRLogger+ conversation — a plain third-party TCI logger simply
ignores the extra messages, so leaving it on does no harm. Nothing here changes
the normal TCI control / spots behaviour; it only *adds* the contact-sharing,
name-back, signal-report and one-click-log conveniences on top.

---

**See also:** [Feature Status](Feature-Status) · [User Guide](User-Guide) · [Home](Home) · 🌐 [SDRLogger+ website](https://n8sdr1.github.io/SDRLoggerPlus/)
