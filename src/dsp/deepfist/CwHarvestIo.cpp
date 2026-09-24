// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/CwHarvestIo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace lyra::dsp {

namespace {
void put32(unsigned char* p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
void put16(unsigned char* p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
}  // namespace

bool writeWav16(const std::string& path, const std::vector<float>& mono,
                int sampleRate, float* outPeak) {
    float peak = 0.0f;
    for (float v : mono) peak = std::max(peak, std::abs(v));
    if (outPeak) *outPeak = peak;
    const float scale = peak > 0.0f ? 20000.0f / peak : 0.0f;

    const uint32_t dataBytes = static_cast<uint32_t>(mono.size() * 2);
    unsigned char h[44];
    h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F'; put32(h + 4, 36 + dataBytes);
    h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';
    h[12]='f'; h[13]='m'; h[14]='t'; h[15]=' '; put32(h + 16, 16);
    put16(h + 20, 1);                                   // PCM
    put16(h + 22, 1);                                   // mono
    put32(h + 24, static_cast<uint32_t>(sampleRate));
    put32(h + 28, static_cast<uint32_t>(sampleRate) * 2);
    put16(h + 32, 2);                                   // block align
    put16(h + 34, 16);                                  // bits/sample
    h[36]='d'; h[37]='a'; h[38]='t'; h[39]='a'; put32(h + 40, dataBytes);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(h, 1, 44, f) == 44;
    for (size_t i = 0; ok && i < mono.size(); ++i) {
        const float scaled = mono[i] * scale;
        const int   v = static_cast<int>(std::lround(
                          std::clamp(scaled, -20000.0f, 20000.0f)));
        const int16_t s = static_cast<int16_t>(v);
        ok = std::fwrite(&s, 2, 1, f) == 1;
    }
    return std::fclose(f) == 0 && ok;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

bool writeTextFile(const std::string& path, const std::string& body) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace lyra::dsp
