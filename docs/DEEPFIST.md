# DeepFist neural CW decoder

Lyra's optional **neural** CW (Morse) receive engine, selectable in the CW
Decoder panel beside the classic fldigi-port decoder. It runs a self-trained
neural model (spectrogram → CNN + CTC) through **ONNX Runtime** (C++, CPU).

- **DeepFist** (the model, synthetic-data generator, and training) is MIT,
  © Brent Crier (N9BC). The C++ front-end here is an original re-implementation of
  DeepFist's own reference. See [`NOTICE.md`](../NOTICE.md) /
  [`CREDITS.md`](../CREDITS.md); as integrated into Lyra it is part of the
  GPLv3+ aggregate (per-file `SPDX-License-Identifier` headers record the MIT
  provenance).
- **ONNX Runtime** (Microsoft, MIT) is vendored under `third_party/onnxruntime/`
  (headers committed; the prebuilt CPU DLL/lib are git-ignored — see that
  directory's README to repopulate `lib/`).

The classic decoder remains the default; the neural engine is opt-in per
session and loads its model lazily the first time you select it.

## Architecture / file map

```
src/dsp/deepfist/
  DeepFistResampler.{h,cpp}   48k→3200 Hz decimator (121-tap, 1440 Hz cutoff)
  DeepFistConditioner.{h,cpp} AGC → tone AFC (4096-pt FFT) → complex downconvert
                              → 2× 1-pole LPF (~90 Hz) → recenter 600 Hz → peak-norm
  DeepFistSpectrogram.{h,cpp} STFT n_fft=256/hop=48, 65 bins (400-1200 Hz), log1p,
                              per-window unbiased standardize; from-scratch FFT
  DeepFistCtc.{h,cpp}         greedy CTC + greedyCtcFrames (frame indices)
  DeepFistScp.{h,cpp}         MASTER.SCP loader + edit-distance candidates
  DeepFistRescore.{h,cpp}     CTC forward (ctcNll) + callsign rescoring
  DeepFistModel.{h,cpp}       ONNX Runtime session; infer() = condition → spectrogram → ORT
  NeuralCwDecoder.{h,cpp}     streaming adapter: worker thread, rolling 6 s window,
                              re-decode ~every 0.4 s, frame-timed commit, keying gate
src/wdsp_engine.{h,cpp}       cwEngine_ switch (0=Classic, 1=Neural); routes the CW
                              mono tap to the selected engine; cwNeuralText /
                              cwNeuralCall signals; setCwDecodeEngine (lazy model load)
src/qml/CwDecoderPanel.qml    Classic/DeepFist toggle, Sensitivity slider, Calls readout
src/prefs.{h,cpp}             cwDecodeEngine + cwBlankPenalty prefs
third_party/onnxruntime/      vendored prebuilt CPU ONNX Runtime (headers committed,
                              lib/ git-ignored — see its README)
models/                       deepfist.onnx + .json + MASTER.SCP — git-ignored;
                              CMake copies them next to the exe on build
scratch/test_neural_cw.cpp        one-shot decode harness (WAV → text)
scratch/test_neural_cw_stream.cpp real-time streaming harness (WAV → live path)
scratch/test_rescore.cpp          ctcNll vs PyTorch + rescore unit test (CMake target)
```

### Model contract

`models/deepfist.onnx.json` (sidecar) defines the contract: input
`spectrogram [1,1,65,T]` at 3200 Hz; output `log_probs [T,1,48]`; 48 tokens;
blank index 0; greedy CTC. The front-end band (400-1200 Hz) covers typical CW
pitch, so no retune is needed. The conditioner is **always on** in
`DeepFistModel::infer` — the model is trained on conditioned, single-signal,
600 Hz-centred, level-normalised audio, so inference must match training. The
sidecar carries no conditioner flag.

Prosigns: the model emits the CW-punctuation aliases `=` (BT) and `+` (AR);
`DeepFistModel::load` relabels them to `<BT>` / `<AR>` for display (consistent
with `<SK>` / `<KN>`), matching the classic decoder and operator expectation.

## Front-end recipe

The C++ front-end must reproduce the model's training pipeline exactly:

1. **Resample** 48 k → 3200 Hz (÷15).
2. **Condition:** unit-RMS AGC → dominant tone in 400-1200 Hz via a 4096-pt FFT
   of the last 4096 samples → complex downconvert (incremental phasors) → two
   cascaded 1-pole LPFs, α = 1 − e^(−2π·45/3200) → recenter to 600 Hz (real
   part) → peak-normalise.
3. **Spectrogram:** STFT n_fft=256, hop=48, periodic Hann, center=true
   (reflect-pad 128), magnitude, bins 32..97 → 65, log1p, per-window
   standardize (unbiased, N−1).
4. **Decode:** greedy CTC (blank=0), tokens from the sidecar.

A per-window **keying gate** (`DeepFistConditioner::keyingRatio`, the p90/p10
ratio of the locked tone's baseband envelope) suppresses decoding of dead air —
below the threshold the streamer emits nothing, which prevents idle-noise
hallucination without affecting real-signal copy.

The **Sensitivity** slider (CW panel) is the CTC blank penalty (−1…+1, default
0): negative suppresses stray/doubled characters on strong signals, positive
pulls fainter code out of the noise.

## Build & run (Windows, MSVC)

**Use the VS 2022 vcvars64 environment.** The full app builds with the normal
CMake flow (`cmake -S . -B build && cmake --build build`); `lyra.exe`,
`onnxruntime.dll`, and `models/` land in `build/`.

The model (`deepfist.onnx` + `.json`) and `MASTER.SCP` are **git-ignored**
binaries distributed separately (installer / release asset), not committed.
Place them in `models/`; CMake's POST_BUILD copies them next to the exe on each
relink. (If you swap `models/deepfist.onnx` without changing code, force a
relink or copy it into `build/models/` manually — POST_BUILD only runs on a
relink.)

Launching the app talks to the HL2 radio and grabs audio — a real hardware side
effect. It is RX-only and does not transmit at idle.

### Offline harnesses

The `scratch/test_neural_cw*` and `test_rescore` targets decode WAV files
without Qt or the radio, for verification against the model reference. Both WAV
readers accept 16-bit PCM and 32-bit-float (fmt=3). `test_rescore` checks the
CTC forward pass against PyTorch's `F.ctc_loss` and is a CMake target.

## Verification

- `cmake --build build --target test_rescore && build/test_rescore` — the CTC
  forward (ctcNll) matches PyTorch `F.ctc_loss` to ~1e-4 and applies the
  expected callsign correction.
- One-shot decode matches the Python/ONNX reference on clean clips, and the C++
  conditioner matches the model's reference conditioner.

## Cross-platform

The DeepFist engine is written to be portable — it is **not** what ties Lyra to
Windows:

- The DSP (resampler, conditioner, spectrogram, CTC, rescorer) is plain
  C++/STL. The only platform guard is in `DeepFistModel.cpp`: an `#ifdef _WIN32`
  for `<windows.h>` + wide-char ONNX path conversion, with a non-Windows `#else`
  branch that passes the plain UTF-8 filename. It compiles as-is on Linux/macOS.
- **ONNX Runtime ships for Linux and macOS** (x64 + arm64) with the same API
  headers. The only Windows-specific runtime file is `onnxruntime.dll`; swap in
  `libonnxruntime.so` (Linux) or `libonnxruntime.dylib` (macOS) — same version,
  same headers. So introducing ONNX Runtime adds no new permanent Windows
  lock-in.

The current blocker to a Linux/macOS build is the **rest of Lyra**, not
DeepFist: the WDSP DSP core is presently a Windows DLL, plus WinSock2
networking. A POSIX shim (`src/os_compat.h`) is a start on that port but is not
yet wired in. When the broader cross-platform port lands, DeepFist comes along
with a recompile plus the platform's ONNX Runtime binary.
