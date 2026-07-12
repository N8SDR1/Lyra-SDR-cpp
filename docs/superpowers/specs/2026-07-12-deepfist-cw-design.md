# DeepFist neural CW decoder — Lyra integration design

_Date: 2026-07-12 · Branch: `feat/deepfist-cw`_

## 1. Goal

Add **DeepFist** — a self-trained neural (CNN+CTC) CW decoder — to Lyra as a
**second, selectable** RX CW decode engine alongside the existing faithful
fldigi port. The classic engine stays the default for clean/strong signals;
DeepFist targets weak-signal / QRM / contest conditions where the classic
slicer loses spacing. Only one engine runs at a time (operator-selected).

DeepFist ships as a portable **ONNX** model (`deepfist.onnx`, exp9, ~3.27M
params, 13 MB) plus a JSON sidecar. Lyra runs it via **ONNX Runtime (C++, CPU)**.
The model decodes single-signal CW tuned onto the operator's pitch (400–1200 Hz
band), 10–40 WPM, full character set incl. prosigns `<SK>` `<KN>`.

Source project: `c:\dev\deepfist` (MIT, © Brent Crier). Model artifact:
`c:\dev\deepfist\runs\deepfist.onnx` + `deepfist.onnx.json`.

## 2. Licensing (cleared)

| Component | License | Status in Lyra (GPLv3-or-later) |
|---|---|---|
| DeepFist code + trained model | MIT (© Brent Crier) | ✅ MIT ⊂ GPLv3 — fold in freely |
| ONNX Runtime (prebuilt CPU DLL) | MIT (Microsoft) | ✅ ship DLL, preserve notice |
| WebMorseRunner (data gen only) | Unlicense / public domain | not shipped |
| DeepCW / HamNoise | **AGPL-3.0** | ⚠️ **never enters the tree** |

Conditions baked into this design:
- The shipped model is **exp9**, trained solely on DeepFist's own synthetic
  generator (per DeepFist `HANDOFF.md` §14a — "clean, moderate… never saw
  WMR"). No third-party weights, no AGPL data.
- C++ preprocessing (spectrogram) and CTC decode are re-implemented from
  DeepFist's own MIT Python + the sidecar params — **not** from DeepCW/HamNoise.
- Add DeepFist (MIT) and ONNX Runtime (MIT) attribution to `NOTICE.md` and
  `CREDITS.md`.

## 3. Model contract (from `deepfist.onnx.json`)

- **Input** `spectrogram` `float32 [batch, 1, freq=65, time]`.
- **Output** `log_probs` `float32 [time_out, batch, class=48]`.
- **Front-end** (per-window, no fixed constants): sample_rate 3200, n_fft 256,
  hop 48, band 400–1200 Hz → **65 bins** (`ceil(400/12.5)=32 .. floor(1200/12.5)+1=97`),
  Hann window, `center=true` (reflect-pad n_fft/2 both ends), magnitude `abs`,
  compress `log1p`, then **global standardize per window**:
  `spec = (spec - spec.mean()) / (spec.std() + 1e-6)` over the whole 65×T tile.
- **CTC** blank_index 0, time_downsample 2 (already applied in the model
  output). Greedy decode: argmax per frame → collapse repeats → drop blanks →
  map to `tokens`.
- **Tokens** (48): `<blank>`, space, A–Z, 1–9, 0, `.` `,` `?` `/` `=` `+` `-` `@`,
  `<SK>`, `<KN>`. Verbatim from the sidecar (single source of truth at runtime).

## 4. Architecture — new module `src/dsp/deepfist/`

```
RX audio 48k mono (private copy, CW-mode-gated)
  └─ DeepFistResampler   decimate ÷15 → 3200 Hz (Hamming-sinc FIR, CwDecoder pattern)
       └─ DeepFistStreamer   6 s ring buffer @3200 Hz; fires a decode every ~1.5 s
            ├─ DeepFistSpectrogram   STFT 256/hop48 → 65 bins → log1p → per-window standardize
            ├─ ORT session (deepfist.onnx)   spectrogram[1,1,65,T] → log_probs[T,1,48]
            ├─ DeepFistCtc   greedy CTC → token string
            └─ stitch   emit only newly-stable text  ──► onText(std::string)
```

Each unit is independently testable:

1. **`DeepFistResampler`** — ÷15 integer FIR decimator, 48 k→3200 Hz. Direct
   reuse of `CwDecoder`'s Hamming-windowed-sinc decimator pattern
   (`src/dsp/CwDecoder.cpp`), cutoff ~1500 Hz (below 1600 Hz Nyquist).
   What it does: streaming resample. Depends on: nothing.
2. **`DeepFistSpectrogram`** — port of `deepfist/features/spectrogram.py`. Takes
   a 3200 Hz mono window, returns a row-major `float32 [65 * T]` tile.
   Real FFT (radix-2, n_fft 256) written from scratch (no FFTW dep). Depends on:
   nothing.
3. **`DeepFistCtc`** — port of `deepfist/model/decode.py`. Argmax/collapse/drop
   over `[T,48]`, maps ids → the sidecar token strings. Depends on: token table.
4. **`DeepFistModel`** — thin ONNX Runtime wrapper: owns `Ort::Env` + `Ort::Session`,
   loads the model + sidecar, runs one spectrogram tile → log-probs. Loads the
   token table + preprocessing params from the sidecar so the model file is the
   single source of truth. Depends on: ONNX Runtime.
5. **`DeepFistStreamer`** — 6 s ring buffer; every hop (~1.5 s) decodes the whole
   window and **stitches**: it tracks the last committed text and emits only the
   newly-confirmed tail from the stable (older) region of each window, so
   overlapping windows don't duplicate or rewrite characters. A low-amplitude
   **gate** (peak below threshold) skips the decode (silence). Depends on: 2,3,4.
6. **`DeepFistDecoder`** — the adapter mirroring `CwDecoder`'s shape:
   `setSampleRate(hz)`, `setToneHz(hz)` (informational; band is fixed 400–1200),
   `process(const float* mono, int nframes)`, `onText` callback, `reset()`,
   `bool ready()` (model loaded). Owns 1 + 5. Threading identical to `CwDecoder`:
   audio thread calls `process()`/`reset()`; `onText` fires from `process()` and
   the `WdspEngine` marshals it to the GUI thread.

### Streaming design choices (defaults)
- Window **6 s**, hop **1.5 s** → text appears ~1.5–2 s behind live.
- Incremental emit: commit the stable prefix of each window; append only the
  newly-confirmed tail. Never rewrites already-shown characters.
- Heavy work (FFT + ORT run) happens on the audio thread inside `process()` only
  when a hop boundary is crossed. exp9 is ~47 ms / 6 s-window CPU 1-thread
  (~126× real-time), so a decode every 1.5 s is a small, bounded burst. If this
  proves too heavy for the audio callback, a follow-up can move the decode to a
  dedicated worker thread (out of scope for v1).

## 5. Dependency: ONNX Runtime (prebuilt CPU)

- Vendor Microsoft's official **prebuilt CPU** ONNX Runtime C++ package under
  `third_party/onnxruntime/` (headers + import lib + `onnxruntime.dll`). MIT.
- `CMakeLists.txt`: add include dir + link the import lib; a post-build step
  copies `onnxruntime.dll` next to the Lyra exe. Installer (`installer/lyra.iss`)
  bundles the DLL.
- Pin the ORT version in the design doc / CMake for reproducibility.

## 6. Model delivery (NOT committed to git)

- The model does **not** enter git — the PR stays code-only.
- The app loads `deepfist.onnx` + `deepfist.onnx.json` from its resource dir
  (next to the exe, e.g. `models/`). A CMake post-build step copies both from
  `c:\dev\deepfist\runs\` into the build output when present. If the model is
  absent, the DeepFist panel shows "model not found" and the engine is disabled;
  the classic decoder is unaffected.
- End-user distribution (installer bundle vs. release asset) is deferred — a
  later decision for Rick, independent of this PR.

## 7. Engine selection & `WdspEngine` integration

- Add a value member `lyra::dsp::DeepFistDecoder deepFistDecoder_;` beside
  `cwDecoder_` in `wdsp_engine.h`.
- Add an **active-engine** enum/atomic `cwEngine_` (0 = Classic, 1 = DeepFist).
  New Q_INVOKABLE `setCwDecodeEngine(int)` + `cwDecodeEngine()` property +
  `cwDecodeEngineChanged()` signal.
- In the CW tap (`wdsp_engine.cpp:~3554`), keep the existing gate; route the mono
  buffer to the selected engine only:
  `if (cwEngine_==DeepFist && deepFistDecoder_.ready()) deepFistDecoder_.process(...)
   else cwDecoder_.process(...)`.
- Wire `deepFistDecoder_.onText` to the **existing** `cwDecodedChar(ch, conf)`
  signal (conf 1.0) so both engines share the same decoded-text surface. WPM is
  classic-only (DeepFist emits word-spaces natively, no WPM estimate) → report 0
  / "—" for DeepFist.
- Switching engine calls `reset()` on both and clears the panel.

## 8. UI — new `DeepFistPanel.qml` (dockable + pop-out)

- Register via `addQuickDock("deepfist", "DeepFist", "DeepFistPanel.qml", …)` in
  `mainwindow.cpp` beside the existing `cwdecoder` dock, then `setFloating(true);
  hide();` — identical floating-dock + chip (pop-out) pattern already used by the
  CW Decoder panel.
- Panel content: engine selector (Classic / DeepFist) bound to
  `WdspEngine.cwDecodeEngine`, a scrolling decoded-text area (reuse
  `CwDecoderPanel`'s `appendDecoded`/color-preset styling), a model status line
  ("DeepFist model loaded" / "model not found — put deepfist.onnx in models/"),
  and Clear. **No** BW/threshold/tracking knobs — DeepFist is self-tuning.
- Styling matches `CwDecoderPanel` / `CwConsolePanel` (hand-drawn chips, section
  dividers, dark palette).

## 9. Testing

- **`DeepFistSpectrogram`**: golden-vector test — feed a known 3200 Hz tone
  buffer, compare the 65×T tile to values produced by DeepFist's Python
  `audio_to_spectrogram` on the same input (tolerance ~1e-4).
- **`DeepFistCtc`**: unit test — hand-built `[T,48]` log-prob arrays →
  expected token strings (blank collapse, repeat collapse, prosigns).
- **`DeepFistModel`** (integration, model present): load `deepfist.onnx`, run a
  synthetic CW clip generated by DeepFist's own generator, assert the decode
  matches the Python reference (the DeepFist repo already verified 10/10
  PyTorch↔ONNX decode match — we mirror one such clip).
- **Resampler**: impulse/step + tone response sanity (÷15, passband ≤1500 Hz).
- **End-to-end smoke**: run the app in CWU, select DeepFist, confirm decoded
  text appears and the classic engine still works when reselected.

## 10. Out of scope (v1)

- Multi-signal / skimmer (multiple decoder instances) — "single now, multi later".
- GPU / DirectML inference.
- Moving inference off the audio thread (revisit only if CPU proves tight).
- Committing the model to git / installer distribution decision.
- Any change to DeepFist itself (it's feature-complete through ONNX export).

## 11. File map

```
third_party/onnxruntime/            (vendored prebuilt CPU ORT — headers, lib, dll)
src/dsp/deepfist/
  DeepFistResampler.{h,cpp}
  DeepFistSpectrogram.{h,cpp}
  DeepFistCtc.{h,cpp}
  DeepFistModel.{h,cpp}
  DeepFistStreamer.{h,cpp}
  DeepFistDecoder.{h,cpp}
src/qml/DeepFistPanel.qml
src/wdsp_engine.{h,cpp}             (add engine member + switch + Q_INVOKABLE)
src/mainwindow.cpp                  (register floating dock + chip)
CMakeLists.txt                      (ORT link, model copy, new sources)
installer/lyra.iss                  (bundle onnxruntime.dll)
NOTICE.md / CREDITS.md              (MIT attribution)
models/                            (build output: deepfist.onnx + sidecar — gitignored)
```
