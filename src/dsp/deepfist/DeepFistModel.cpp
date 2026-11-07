// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/DeepFistModel.h"

#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <sstream>

#include <onnxruntime_cxx_api.h>

#include "dsp/deepfist/DeepFistCtc.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace lyra::dsp {

// ONNX Runtime handles kept out of the header.
struct DeepFistModel::Impl {
    Ort::Env      env{ORT_LOGGING_LEVEL_WARNING, "deepfist"};
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
};

namespace {

// The canonical 48-token table (DeepFist sidecar order).  Used as a fallback if
// the sidecar can't be read; the sidecar is still the runtime source of truth.
std::vector<std::string> fallbackTokens() {
    std::vector<std::string> t = {"", " "};   // blank rendered empty, then space
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890.,?/=+-@";
    for (const char* p = chars; *p; ++p) t.emplace_back(1, *p);
    t.emplace_back("<SK>");
    t.emplace_back("<KN>");
    // 48 total: blank + space + A-Z(26) + digits(10) + ". , ? / = + - @"(8) + <SK> + <KN>
    return t;
}

// Minimal parse of the sidecar "tokens": [...] array of quoted strings.
// Avoids pulling a JSON lib into this DSP path; the format is fixed + simple.
std::vector<std::string> parseSidecarTokens(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();

    const std::string key = "\"tokens\"";
    size_t k = s.find(key);
    if (k == std::string::npos) return {};
    size_t lb = s.find('[', k);
    size_t rb = s.find(']', lb == std::string::npos ? k : lb);
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return {};

    std::vector<std::string> out;
    size_t i = lb + 1;
    while (i < rb) {
        size_t q1 = s.find('"', i);
        if (q1 == std::string::npos || q1 >= rb) break;
        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 > rb) break;
        std::string tok = s.substr(q1 + 1, q2 - q1 - 1);
        if (tok == "<blank>") tok.clear();   // blank -> empty (never emitted)
        out.push_back(tok);
        i = q2 + 1;
    }
    return out;
}

// In-place radix-2 complex FFT (n a power of two); inverse when inv.
void fftInPlace(std::vector<std::complex<double>>& a, bool inv) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * 3.14159265358979323846 / static_cast<double>(len)
                           * (inv ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inv)
        for (auto& x : a) x /= static_cast<double>(n);
}

// Keep only |f - centerHz| <= halfWidthHz of a real signal at `sr`.
void bandMask(std::vector<float>& x, double sr, double centerHz, double halfWidthHz) {
    if (x.empty() || halfWidthHz <= 0.0) return;
    size_t n = 1;
    while (n < x.size() * 2) n <<= 1;            // zero-pad: no circular wrap
    std::vector<std::complex<double>> a(n, {0.0, 0.0});
    for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
    fftInPlace(a, false);
    const double binHz = sr / static_cast<double>(n);
    for (size_t b = 0; b < n; ++b) {
        const double f = (b <= n / 2) ? b * binHz : (static_cast<double>(b) - n) * binHz;
        if (std::abs(std::abs(f) - centerHz) > halfWidthHz) a[b] = 0.0;
    }
    fftInPlace(a, true);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(a[i].real());
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
#endif

}  // namespace

DeepFistModel::DeepFistModel() : impl_(std::make_unique<Impl>()) {}
DeepFistModel::~DeepFistModel() = default;

bool DeepFistModel::load(const std::string& modelDir) {
    ready_ = false;
    lastError_.clear();

    const std::string onnx    = modelDir + "/deepfist.onnx";
    const std::string sidecar = modelDir + "/deepfist.onnx.json";

    // tokens: sidecar first, fallback to the built-in table
    tokens_ = parseSidecarTokens(sidecar);
    if (tokens_.size() < 2) tokens_ = fallbackTokens();

    // Display prosigns in fldigi-style angle notation.  The model emits the
    // aliased punctuation tokens "=" / "+" for BT / AR (that's how they're
    // written in CW); relabel them to <BT>/<AR> so the copy matches the classic
    // decoder and operator expectation (consistent with <SK>/<KN>).  Display
    // only — a MISREAD prosign still shows whatever the model actually decoded.
    for (std::string& t : tokens_) {
        if (t == "=") t = "<BT>";
        else if (t == "+") t = "<AR>";
    }

    try {
        impl_->opts.SetIntraOpNumThreads(1);
        impl_->opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
#ifdef _WIN32
        impl_->session = std::make_unique<Ort::Session>(
            impl_->env, widen(onnx).c_str(), impl_->opts);
#else
        impl_->session = std::make_unique<Ort::Session>(
            impl_->env, onnx.c_str(), impl_->opts);
#endif
    } catch (const Ort::Exception& e) {
        lastError_ = std::string("ONNX load failed: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = std::string("model load failed: ") + e.what();
        return false;
    }

    ready_ = true;
    return true;
}

bool DeepFistModel::infer(const float* audio, int n,
                          std::vector<float>& logits, int& T, int& C) {
    if (!audio || n <= 0) { T = 0; C = 0; logits.clear(); return false; }
    return inferAt(audio, n, cond_.detectTone(audio, n), logits, T, C);
}

bool DeepFistModel::inferAt(const float* audio, int n, float toneHz,
                            std::vector<float>& logits, int& T, int& C) {
    T = 0; C = 0;
    logits.clear();
    if (!ready_ || !audio || n <= 0) return false;

    // fldigi-style conditioner FIRST (exp14+ models are trained on conditioned,
    // single-signal, 600 Hz-centred, level-normalised audio — train==inference).
    condScratch_ = cond_.conditionAt(audio, n, toneHz);
    if (outMaskHz_ > 0.0f && !condScratch_.empty())
        bandMask(condScratch_, DeepFistConditioner::kSr,
                 DeepFistConditioner::kOutPitch, outMaskHz_);
    const float* condAudio = condScratch_.empty() ? audio : condScratch_.data();

    int specT = 0;
    fe_.compute(condAudio, n, specScratch_, specT);  // [65 * specT] row-major
    if (specT == 0 || specScratch_.empty()) return false;

    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> inShape{
            1, 1, DeepFistSpectrogram::kFreqBins, specT};
        Ort::Value input = Ort::Value::CreateTensor<float>(
            mem, specScratch_.data(), specScratch_.size(),
            inShape.data(), inShape.size());

        const char* inNames[]  = {"spectrogram"};
        const char* outNames[] = {"log_probs"};
        auto outputs = impl_->session->Run(
            Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);

        auto info = outputs[0].GetTensorTypeAndShapeInfo();
        auto oshape = info.GetShape();                // [t_out, batch=1, class]
        if (oshape.size() != 3) return false;
        T = static_cast<int>(oshape[0]);
        C = static_cast<int>(oshape[2]);
        const float* logp = outputs[0].GetTensorData<float>();
        // [t_out, 1, class] row-major == [t_out][class] for batch 1
        logits.assign(logp, logp + static_cast<size_t>(T) * C);
        return true;
    } catch (const Ort::Exception& e) {
        lastError_ = std::string("inference failed: ") + e.what();
        return false;
    }
}

std::string DeepFistModel::decode3200(const float* audio, int n) {
    std::vector<float> logits;
    int T = 0, C = 0;
    if (!infer(audio, n, logits, T, C)) return {};
    return greedyCtcDecode(logits.data(), T, C, tokens_);
}

}  // namespace lyra::dsp
