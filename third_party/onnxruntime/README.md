# ONNX Runtime (vendored, prebuilt CPU)

Lyra's DeepFist neural CW decoder (`src/dsp/deepfist/`) runs its ONNX model
through **ONNX Runtime** (MIT, © Microsoft). We vendor the official **prebuilt
CPU** package for Windows x64.

**Version:** 1.20.1 (see `VERSION_NUMBER`). License: MIT (`LICENSE`,
`ThirdPartyNotices.txt`).

## What is committed vs. not

- **Committed:** `include/` (API headers), `LICENSE`, `ThirdPartyNotices.txt`,
  `VERSION_NUMBER`, this README.
- **Not committed (git-ignored):** `lib/` (the ~11 MB `onnxruntime.dll` +
  import libs). These are large binaries; they are repopulated from the official
  release, and shipped to users via the installer.

## Repopulate `lib/` (needed to build)

Download `onnxruntime-win-x64-1.20.1.zip` from the official release:

    https://github.com/microsoft/onnxruntime/releases/tag/v1.20.1

Unzip and copy into `third_party/onnxruntime/lib/`:

- `onnxruntime.dll`
- `onnxruntime.lib`
- `onnxruntime_providers_shared.dll`
- `onnxruntime_providers_shared.lib`

CMake links `onnxruntime.lib` (include dir = `third_party/onnxruntime/include`)
and post-build-copies `onnxruntime.dll` next to the Lyra executable.
