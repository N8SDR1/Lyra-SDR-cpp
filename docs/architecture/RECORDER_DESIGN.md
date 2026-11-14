# Session Recorder — Design (Task #201)

**Status:** design LOCKED pending operator sign-off (2026-07-11). No code yet.

A recording feature that captures **RX (and optionally TX-monitor) audio** to
WAV, plus **timed panadapter/waterfall snapshots**, bundled into a timestamped
**session folder**, with an **offline converter** to a single playable
MP4/MKV. Built to reuse the existing `tx/WavIo` + `ClipRecorder` family and to
never touch the real-time DSP / wire / paint path.

---

## 1. Goals & scope

- One-button audio recording of what the operator hears (RX), optionally the
  TX monitor too, so a QSO captures both sides.
- Optional periodic **PNG snapshots** of the spectrum + waterfall exactly as
  displayed (RX signal on receive, TX signal on transmit) — a visual timeline
  without a real-time video encoder.
- Everything lands in a **timestamped session folder** under an
  operator-chosen path; long recordings auto-split into numbered sub-files.
- An **offline** step converts a chosen session's WAV+PNGs into a single
  **MP4** (default) or **MKV** (lossless-audio option), saved back into that
  same session folder. The raw WAV is always kept.
- Safety rails: a **hard time limit** (default 10 min), an always-visible
  **`● REC` indicator**, and a **storage cap** on the recordings root.

**Non-goals (v1):** real-time in-app video/screen capture (use OBS / Win+G);
IQ recording; editing/trimming.

---

## 2. UI model (operator-approved)

Three surfaces, deliberately separated by job:

1. **Opt-in dockable "Recorder" panel** — the day-to-day control surface.
   - `RecorderPanel.qml`: **REC / ⏹** button, live **timer**, **snapshot
     on/off** toggle, snapshot-rate readout, and a shortcut to
     **Settings → Recording**.
   - Ships **available via View → Panels, NOT in the shipped default layout.**
     No new default layout is required; existing and saved layouts are
     untouched. (This also avoids the #189 "layout recall re-adds a panel"
     class — the panel only exists if the operator adds it.)
   - Behaves like every other panel: movable / dockable / floatable.

2. **Always-visible safety indicator** — independent of the panel.
   - A small **`● REC hh:mm:ss`** chip in the **status bar**, shown **only
     while recording** (hidden otherwise, so zero clutter for non-users),
     pulsing, **click-to-stop.**
   - Rationale: a dockable panel can be *closed*. If the control and the
     "it's running" light both lived only on the panel, a recording could be
     silently left running. The status-bar chip is the belt to the panel's
     suspenders and is the real guard behind the hard-limit.

3. **Settings → Recording tab** — config + session management.
   - Config: record source (RX / RX+TX), snapshot on/off + rate, auto-split
     (min or MB), hard time limit, record path (+ Browse), storage cap
     (+ optional auto-prune), video format (MP4 / MKV).
   - **Sessions list:** each timestamped session shows date/time, duration,
     size, snapshot count. Per-session: **Convert to MP4/MKV**, **Open
     folder**, **Delete**. **Multi-select → Delete.** Convert can also
     **Browse…** to any prior session/file.

---

## 3. Recording engine

- **Audio tap:** the RX audio the operator hears (post-DSP), and — when
  `RX+TX` is selected — the TX monitor tap during transmit, written via the
  existing `tx/WavIo` writer. No new audio-path code; a passive tap only.
- **Format:** 48 kHz WAV (mono or stereo). A lower-rate option may be offered
  to save space (this is the audio "sample-rate setting").
- **Snapshots:** master on/off. When on, a timer at the chosen **rate**
  (snapshots/min, or interval seconds — auto-scales with duration; e.g.
  5/min = one every 12 s) grabs the panadapter+waterfall framebuffer to PNG.
  Each PNG is recorded in the manifest with its time-offset, frequency, and
  mode. When off → **audio-only** (pure WAV, no PNGs, no video).
- **Auto-split:** at X minutes **or** X MB, close the current WAV and open the
  next (`…_001.wav`, `_002.wav`, …). Snapshots keep their own running index
  across sub-files.
- **Hard time limit:** operator-set max duration → **auto-stop** (default
  **10 min**). Also auto-stop if the stream stops.

---

## 4. Session bundle & manifest

One timestamped folder per recording under the record path:

```
<recordPath>/
  2026-07-11_143210_14074kHz_USB/
    audio_001.wav
    audio_002.wav            (if split)
    snap_0001.png            (t=+0s)
    snap_0002.png            (t=+12s)
    ...
    session.json             (manifest)
    session.mp4              (created later by Convert, optional)
```

**`session.json`** (self-describing, human-readable):

```json
{
  "created": "2026-07-11T14:32:10",
  "durationSec": 600,
  "source": "rx",
  "audio": [{ "file": "audio_001.wav", "rateHz": 48000, "channels": 1 }],
  "snapshots": [
    { "file": "snap_0001.png", "offsetSec": 0,  "freqHz": 14074000, "mode": "USB" },
    { "file": "snap_0002.png", "offsetSec": 12, "freqHz": 14074000, "mode": "USB" }
  ]
}
```

The bundle-of-files approach needs **no new dependency** and is fully useful
on its own (WAV plays anywhere, PNGs open anywhere). The MP4/MKV is a
convenience export layered on top.

---

## 5. Storage cap

- Operator-set **max total MB** for the recordings root.
- Behavior: **warn as it approaches**, and **refuse to START a new
  recording** once over the cap — **never silently delete** operator data.
- **Optional opt-in** "auto-prune oldest sessions" tick for those who want a
  rolling ring; off by default.

---

## 6. Offline MP4/MKV converter (the "no dropouts" design)

The single most important correctness point: **the encode never touches the
real-time threads.**

- Triggered by the operator (button), operating on **already-saved files**.
- Runs as a **separate OS process** (ffmpeg-class encoder) reading the WAV +
  PNGs off disk and writing the video — it shares nothing with the DSP audio
  pipeline or the Qt render thread.
- Launched at **below-normal priority**, so the OS schedules the radio's
  high-priority audio/wire threads first. On a modern multicore box it is
  barely felt; worst realistic case on a weak PC under load is a *momentary*
  UI hitch — **not** an audio dropout, **not** a stall.
- UI: a friendly **"Converting… (runs in background) — N%"** indicator, not a
  "dropouts may occur" warning we've engineered out. Progress scales with
  snapshot count + CPU.
- **TX ⇄ convert mutual lock-out:**
  - No TX while converting — PTT/MOX disabled with a tooltip
    (*"TX paused — finishing export"*).
  - No convert while transmitting — the Convert action defers/queues until
    un-key.

### Format story

- **MP4 (H.264 + AAC ~256 kbps) — the v1 format (only).** Universal
  double-click playback everywhere; AAC at that rate is transparent for
  voice/ham audio.
- **MKV (H.264 + FLAC lossless audio) — DEFERRED** to a later pass (optional
  "max quality" pick; plays in VLC / Win11; can chapter-mark snapshots). The
  format setting ships MP4-only; MKV slots in later without reworking the flow.
- **The raw WAV is always kept** in the session folder regardless of export —
  nothing lossy is ever forced (and it's the lossless-audio answer until MKV
  lands).
- Audio-only session → the Convert action offers an audio-only export
  (M4A/MP3) or is simply N/A (the WAV is already the deliverable).

### Dependency note

The single-file MP4/MKV pulls in an **ffmpeg / Media-Foundation** encoder.
ffmpeg (LGPL/GPL) is license-compatible with Lyra's GPLv3. The
**session-folder bundle itself needs no new dependency** — only the export
does. Operator confirmed OK taking the encoder dependency for the MP4 payoff.

---

## 7. Config keys (QSettings, `recorder/` group — indicative)

| Key | Default | Meaning |
|---|---|---|
| `recorder/path` | `%USERPROFILE%\Lyra\Recordings` | record root (never Program Files) |
| `recorder/source` | `rx` | `rx` / `rx_tx` |
| `recorder/snapshotsOn` | `true` | master snapshot toggle |
| `recorder/snapshotPerMin` | `5` | snapshot rate |
| `recorder/splitMinutes` | `0` | 0 = off |
| `recorder/splitMB` | `0` | 0 = off |
| `recorder/hardLimitMin` | `10` | auto-stop |
| `recorder/capMB` | `5120` | 5 GB default; 0 = no cap |
| `recorder/autoPrune` | `false` | ring-delete oldest |
| `recorder/videoFormat` | `mp4` | v1 = `mp4` only (`mkv` deferred) |
| `recorder/audioRateHz` | `48000` | WAV rate |

(All owned by the Backup & Restore "advanced"/a new section by prefix.)

---

## 8. Staged build plan (each stage independently testable; no wire-path risk)

1. **Recorder engine** — audio tap → `WavIo`, timer, auto-split, hard limit,
   session folder + `session.json`. Headless, unit-tested.
2. **Snapshot capture** — framebuffer grab → PNG at rate, manifest entries.
3. **Recorder panel + status-bar `● REC` chip** — control surface + safety
   indicator; panel available via View → Panels, NOT in default layout.
4. **Settings → Recording tab** — config + Sessions list (convert / open /
   delete / multi-delete) + storage-cap logic.
5. **Offline encoder** — separate below-normal-priority process, TX⇄convert
   lock-out, MP4/MKV, progress UI.
6. **Docs** — User Guide section (incl. the legal note) + README mention.

---

## 9. Legal note (for the User Guide)

Not legal advice; varies by jurisdiction. Amateur radio is an **open, public
service** (Part 97 forbids obscuring message meaning; ECPA §2511 exempts
radio "readily accessible to the general public"), so **recording received
traffic for personal use is standard and lawful.** The operator is
responsible for local rules and for getting consent before *publishing* a
recording of someone. A one-line note to that effect belongs in the guide.

---

## 10. Sign-off decisions (operator, 2026-07-11)

1. **Always-on indicator → status-bar `● REC` chip** (with the HL2 telemetry,
   out of the busy header). ✅
2. **Export format → MP4 only for v1** (H.264 + AAC ~256 kbps). MKV/FLAC is
   **deferred** to a later pass — the format dropdown ships MP4-only now, with
   MKV as a future addition. ✅
3. **Storage-cap default → 5 GB** (`recorder/capMB` = 5120). Warn as it
   approaches, refuse to start a new recording once over, no silent delete;
   optional auto-prune still opt-in/off by default. ✅
4. **RX+TX → one stereo WAV** — RX on the left channel, TX on the right, so a
   QSO capture keeps both sides separable in a single file. RX-only records as
   normal (mono/stereo per the audio path). ✅

Design is signed off; ready to stage the build per §8 on the operator's go.
