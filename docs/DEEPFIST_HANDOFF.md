# DeepFist neural CW decoder in Lyra — Handoff

_Last updated: 2026-07-12. Branch: `feat/deepfist-cw` (NOT pushed — coordinate
with Rick/N8SDR1 before pushing to the shared public repo)._

This is the single resume anchor for the DeepFist ↔ Lyra work. Read it top to
bottom. The companion memory files (`~/.claude/projects/.../memory/`) —
`deepfist-cw-integration`, `lyra-serialport-blocker`, `lyra-build-procedure` —
carry the same facts in short form.

---

## 1. What this is

Native embed of **DeepFist** — a self-trained neural (spectrogram → CNN+CTC)
CW decoder — into Lyra as a **second, selectable** RX CW engine beside the
classic fldigi decoder. Model is DeepFist's own (MIT, © Brent Crier). Runs via
**ONNX Runtime** (C++, CPU). Mirrors the verified Rust reference in
**`c:\dev\diddle`** (branch `feat/deepfist-cw`, `src-tauri/src/dsp/cw_neural.rs`
+ `scp.rs` + `dsp/rescore.rs`). DeepFist project: **`c:\dev\deepfist`** (read its
`HANDOFF.md`).

**The Lyra integration is COMPLETE and VERIFIED CORRECT.** The remaining issue
is decode *quality* on some real signals, which is a **DeepFist model
limitation, not a Lyra bug** (see §7).

## 2. Status — done & verified

Branch `feat/deepfist-cw`, on top of Rick's v0.18.0 (merged). Commits (newest
first):
- `5186cfb` debug tooling — float32 CW capture + harness float WAV reading
- `bd2e3fd` Merge origin/main (v0.18.0: Zero-beat + Getting Started)
- `399f184` CTC-lattice callsign rescorer (WSJT-X Deep Search)
- `5d6e770` front-end conditioner for exp14+ models
- `81dc8a1` stream characters via frame-timed commit
- `8c3d917` log INFO when the neural model loads
- `818103c` wire the neural engine into WdspEngine + CW panel
- `7d91840` verified DSP core + ONNX Runtime
- `a291b64` design spec (`docs/superpowers/specs/2026-07-12-deepfist-cw-design.md`)

Full app **builds (exit 0)** and **runs** (log: "neural CW model loaded … SCP
rescorer: 50038 calls"). Coexists with Rick's ZeroBeat.

## 3. Architecture / file map

```
src/dsp/deepfist/
  DeepFistResampler.{h,cpp}   48k→3200 Hz decimator (121-tap, 1440 Hz) — matches Rust
  DeepFistConditioner.{h,cpp} AGC→tone-AFC(4096 FFT)→downconvert→2×1-pole LPF(90Hz)
                              →recenter 600Hz→peak-norm. REQUIRED by exp14+ models.
  DeepFistSpectrogram.{h,cpp} STFT 256/hop48, 65 bins 400-1200Hz, log1p, per-window
                              unbiased standardize. From-scratch FFT.
  DeepFistCtc.{h,cpp}         greedy CTC + greedyCtcFrames (frame indices)
  DeepFistScp.{h,cpp}         MASTER.SCP loader + edit-distance candidates
  DeepFistRescore.{h,cpp}     ctcNll (CTC forward) + rescoreCalls (callsign correction)
  DeepFistModel.{h,cpp}       ONNX Runtime session; infer() = condition→spectrogram→ORT
  NeuralCwDecoder.{h,cpp}     streaming adapter: worker thread, rolling 6s window,
                              re-decode every 0.4s, frame-timed commit, rescorer
src/wdsp_engine.{h,cpp}       cwEngine_ switch (0=Classic,1=Neural); tap routes
                              cwMonoBuf_ to selected engine; cwNeuralText / cwNeuralCall
                              signals; setCwDecodeEngine (lazy model load)
src/qml/CwDecoderPanel.qml    Classic/DeepFist toggle + "Calls" readout
src/prefs.{h,cpp}             cwDecodeEngine pref
third_party/onnxruntime/      vendored prebuilt CPU ORT 1.20.1 (headers committed,
                              lib/*.dll git-ignored — see its README)
models/                       deepfist.onnx + .json + MASTER.SCP — ALL git-ignored,
                              CMake copies them next to the exe on build
scratch/test_neural_cw.cpp        one-shot decode harness (WAV → text)
scratch/test_neural_cw_stream.cpp real-time streaming harness (WAV → live path)
scratch/test_rescore.cpp          ctcNll vs PyTorch + rescore unit test (CMake target)
```

**Model contract** (`models/deepfist.onnx.json`): input `spectrogram [1,1,65,T]`
@3200 Hz; output `log_probs [T,1,48]`; 48 tokens; blank=0; greedy CTC. Front-end
band 400-1200 Hz covers the operator CW pitch (no retune needed). The model is
**exp15** (conditioned single-signal). Sidecar has NO conditioner flag — the
conditioner is always-on in `DeepFistModel::infer`.

## 4. Build & run (Windows, MSVC)

**MUST use VS 2022 vcvars64** (VS 2026 lacks the C++ toolset → type_traits
C1083). Qt 6.11.1 kit — **SerialPort module was missing**, now installed (§8).

```powershell
# full app (build3.ps1 helper does the vcvars capture + cmake --build build)
&  <vcvars64.bat>; cmake -S . -B build; cmake --build build --target lyra
# artifacts land in build/  (lyra.exe + onnxruntime.dll + models/{onnx,json,SCP})
```

**Launch smoke** (pre-select DeepFist, capture the model-load log line):
```powershell
$b="HKCU:\Software\N8SDR\Lyra-cpp\cw"; New-ItemProperty $b decodeEngine 1 -PropertyType DWord -Force
Start-Process build\lyra.exe -WorkingDirectory build
# log: %APPDATA%\N8SDR\Lyra-cpp\logs\lyra-log.txt
```
Launching the app talks to the HL2 radio + grabs audio — a real hardware side
effect. RX-only; it does not TX at idle.

**Offline harnesses** (compile directly with cl — Qt-free; ORT-linked ones need
the DLL beside the exe). Example (one-shot decode of a WAV):
```powershell
cl /std:c++20 /EHsc /utf-8 /O2 /MD /I src /I third_party\onnxruntime\include `
  scratch\test_neural_cw.cpp src\dsp\deepfist\{DeepFistResampler,DeepFistSpectrogram,`
  DeepFistConditioner,DeepFistCtc,DeepFistModel}.cpp `
  /link third_party\onnxruntime\lib\onnxruntime.lib
test_neural_cw.exe models <clip.wav>   # rate 3200 → direct; else via decimator
```
`test_neural_cw_stream.exe models <clip48k.wav>` = real streaming path (adds
DeepFistScp+DeepFistRescore+NeuralCwDecoder sources). Both read 16-bit PCM AND
fmt=3 32-bit-float WAVs.

## 5. Verify / reference-compare

- `cmake --build build --target test_rescore && build\test_rescore` → ctcNll
  matches PyTorch F.ctc_loss (1e-4) + flips A1B→A1C. **PASSES.**
- One-shot decode matches Python-ONNX exactly on 5 clean clips; conditioner
  matches the Rust reference exactly on those.
- **Authoritative reference = diddle's Rust decoder** (SAME model + conditioner):
  `cd c:\dev\diddle\src-tauri && DIDDLE_SCP=c:\dev\deepfist\data\MASTER.SCP `
  `cargo run --release --example cw_decode_wav -- <clip.wav>`. Use this to tell a
  Lyra bug from a model limitation — if diddle and Lyra agree, it's the model.

## 6. DeepFist front-end recipe (must match EXACTLY)

Resample 48k→3200 (÷15). **Conditioner** (mirror `cw_neural.rs::Conditioner`):
unit-RMS AGC → dominant tone in 400-1200 Hz via 4096-pt FFT of the LAST 4096
samples (no window) → complex downconvert (incremental phasors, f32) → two
cascaded 1-pole LPFs, α=1−e^(−2π·45/3200) → recenter to 600 Hz (real part) →
peak-norm. Then spectrogram: STFT n_fft=256, hop=48, periodic Hann, center=true
(reflect-pad 128), |mag|, bins 32..97 → 65, log1p, per-window standardize
(unbiased/N−1). Greedy CTC (blank=0), tokens from sidecar. NOTE: the Python
`conditioner.py` is a "twin" and differs slightly in detect_tone (first-4096 +
Hann); the **Rust is authoritative** (on-air verified) — Lyra mirrors the Rust.

## 7. CURRENT INVESTIGATION — the "horrible live decode" (RESOLVED as model issue)

Symptom: both neural AND classic decode badly on a signal the operator says is
clean. Debugged systematically via `LYRA_CW_WAV` capture of the exact decoder
input. **Findings (all evidenced):**

1. The tapped audio is CLEAN. True (unclamped float) capture: peak ≈ 21, RMS ≈ 6
   — the RX audio runs at **~×21 full scale** (+19 dB `afGainDb` makeup × AGC).
   That is NOT clipping — the earlier 16-bit capture only *looked* clipped
   because it clamps. Keying envelope is textbook: **122:1 contrast, 45% key-up
   gaps.** The conditioner's AGC normalizes the ×21 level fine.
2. The model CAN decode it: a well-aligned 6 s one-shot window →
   **`KG4CB` … `TW GREGG`** (real callsign + name), in BOTH Python and C++.
3. But **sliding-window streaming garbles it** (`INN … TG4CXW … GREGG`), because
   **exp15 is alignment-sensitive** — it reads the callsign cleanly only when it
   sits centered in the window, and mangles it at other offsets (`G4CXW`,
   `AG4CXR`). Most sliding alignments are "wrong".
4. **diddle's reference decoder garbles the SAME capture identically**
   (`AG4CXR`, `CXW GRE`, `T GREGG I`, …). ⇒ Lyra's port is faithful; **this is a
   DeepFist model limitation, not a Lyra bug.** Fix belongs in DeepFist.

Saved as a real eval clip: **`c:\dev\cw\wav\lyra_kg4cb_gregg_2026-07-12.wav`**
(60 s, 48k, peak-normalized) + `.txt` with a TENTATIVE label
(`KG4CB DE <?> TU TW GREGG WA <?> RIC <?>`) — **operator ground-truth still
needed** (Brent knows the QSO). DeepFist's HANDOFF explicitly wants hand-labeled
real eval clips; this is one where exp15 fails.

## 8. Environment gotchas (all resolved unless noted)

- **Qt SerialPort** (v0.17.0+ needs it; 6.11.1 kit lacked it): installed by
  downloading the official module 7z from download.qt.io …/qt6_6111_msvc2022_64/
  qt.qt6.6111.addons.qtserialport.win64_msvc2022_64/ (sha1-verified), py7zr
  extract, robocopy-merge into `C:\Qt\6.11.1\msvc2022_64`. (aqtinstall kept
  failing on the Updates.xml checksum.) See `lyra-serialport-blocker` memory.
- **ONNX Runtime**: prebuilt CPU 1.20.1, vendored; `lib/` git-ignored. onnxruntime.dll
  ships beside the exe (load-time import).
- **Model + MASTER.SCP git-ignored** — CMake copies from `models/` next to the
  exe on build. Source copies came from `c:\dev\deepfist\runs\deepfist.onnx(.json)`
  and `c:\dev\deepfist\data\MASTER.SCP`.
- CRLF warnings on commit are benign.

## 9. Next steps (highest-leverage first)

1. **Improve the model (in DeepFist, not Lyra).** Alignment-robust decode is the
   real fix: e.g. train/augment with random window offsets, or a decode-time
   strategy (multi-offset decode + consensus, or align windows to keying gaps).
   Use `c:\dev\cw\wav\lyra_kg4cb_gregg_2026-07-12.wav` (label it first) as a
   regression clip. Re-measure with diddle's `cw_decode_wav` reference.
2. Optional Lyra-side experiment (beyond the reference; only if the model can't
   be fixed soon): multi-offset decode + pick the reading the rescorer scores
   best. Speculative — the rescorer already false-positives here (confirmed
   `AG4CXR` at 22 nats), so treat with caution.
3. Not-yet-ported from diddle: `probe_calls` (cluster-spot scoring), green/amber
   in-transcript chips, blank-penalty + activity-gating from `tci_decode.py`
   (kBlankPen=0 currently; docs call these "what makes live copy readable" —
   worth trying, though they won't fix alignment sensitivity).
4. When ready to share: coordinate with Rick, decide model/SCP delivery (Git LFS
   / installer bundle / release asset — currently code-only, artifacts local).

## 10. Key locations

- Lyra branch `feat/deepfist-cw` · design spec `docs/superpowers/specs/2026-07-12-deepfist-cw-design.md`
- diddle (Rust reference): `c:\dev\diddle` branch `feat/deepfist-cw` — `src-tauri/src/dsp/cw_neural.rs`, `src/scp.rs`, `src/dsp/rescore.rs`, `examples/cw_decode_wav.rs`
- DeepFist (model/training): `c:\dev\deepfist` — `HANDOFF.md`, `runs/deepfist.onnx(.json)`, `data/MASTER.SCP`, `deepfist/features/{spectrogram,conditioner}.py`, `scripts/tci_decode.py`
- Real clips: `c:\dev\cw\wav\` (audio_001, untitled, the new lyra_kg4cb_gregg…)
```
