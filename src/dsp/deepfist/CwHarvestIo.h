// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 2 harvest: tiny Qt-free file helpers.  writeWav16 emits the
// int16-PCM, peak->20000 clip format DeepFist's curation tools already eat
// (tools/pseudo_label_capture.py convention); jsonEscape/writeTextFile build
// the .json sidecars.
#pragma once

#include <string>
#include <vector>

namespace lyra::dsp {

// Peak-normalize `mono` to 20000 and write a complete 16-bit PCM WAV.
// `outPeak` (optional) receives the pre-normalization |peak| so the original
// scale is recoverable from the sidecar.  Returns false on any I/O failure.
bool writeWav16(const std::string& path, const std::vector<float>& mono,
                int sampleRate, float* outPeak = nullptr);

// Minimal JSON string escaping: backslash, quote, and control chars.
std::string jsonEscape(const std::string& s);

// Write `body` to `path` (binary, whole file).  Returns false on failure.
bool writeTextFile(const std::string& path, const std::string& body);

}  // namespace lyra::dsp
