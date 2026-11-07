// Offline bench for the DeepFist neural CW decoder.  Qt-free.
// Build + run:  cmake --build build --target test_cw_neural_bench
//               build/test_cw_neural_bench.exe [modelDir]
//
// Synthesises CW with a known transcript (clean / weak / loud neighbour /
// off-tune / fast / slow / fading), runs it through the same streaming recipe
// the live decoder uses (rolling 6 s window at 3200 Hz, keying gate, greedy
// CTC, frame-timed commit), and reports character error rate (CER), display
// latency and model cost per tick.  Each run varies one knob:
//   tone lock  — how the conditioner picks the station
//   guard      — commit delay (how old a character's audio must be)
//   tick       — how often the window is re-decoded
//
// The streaming loop below mirrors NeuralCwDecoder::workerLoop (tone lock ->
// keying gate -> model -> CTC) and uses the same DeepFistCommit, so the commit
// behaviour measured here is the live decoder's.

#include "dsp/deepfist/DeepFistCommit.h"
#include "dsp/deepfist/DeepFistCtc.h"
#include "dsp/deepfist/DeepFistModel.h"
#include "dsp/deepfist/DeepFistResampler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace lyra::dsp;
using Search = DeepFistConditioner::ToneSearch;

namespace {

constexpr double kIn  = 48000.0;
constexpr int    kSr  = 3200;
constexpr double kPi  = 3.14159265358979323846;

// ── Morse synthesis ─────────────────────────────────────────────────────────
const std::map<char, const char*>& morse() {
    static const std::map<char, const char*> m = {
        {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
        {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
        {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
        {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
        {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
        {'Z', "--.."}, {'0', "-----"},{'1', ".----"},{'2', "..---"},{'3', "...--"},
        {'4', "....-"},{'5', "....."},{'6', "-...."},{'7', "--..."},{'8', "---.."},
        {'9', "----."},{'/', "-..-."},{'?', "..--.."},{'.', ".-.-.-"},{',', "--..--"},
    };
    return m;
}

struct Station {
    std::string text;
    double wpm    = 20.0;
    double tone   = 600.0;   // audio pitch (Hz)
    double amp    = 1.0;     // peak amplitude
    double startS = 0.5;
};

// Add one keyed station into `out` (48 kHz).  Raised-cosine 5 ms edges.
void addStation(std::vector<float>& out, const Station& st, double fadeDepth,
                double fadeHz) {
    const double unit = 1.2 / st.wpm;
    std::vector<std::pair<double, double>> keyOn;   // [start, end) seconds
    double t = st.startS;
    for (char ch : st.text) {
        if (ch == ' ') { t += 4.0 * unit; continue; }   // 3 already added -> 7
        auto it = morse().find(ch);
        if (it == morse().end()) continue;
        for (const char* p = it->second; *p; ++p) {
            const double len = (*p == '.') ? unit : 3.0 * unit;
            keyOn.emplace_back(t, t + len);
            t += len + unit;
        }
        t += 2.0 * unit;                                 // letter gap = 3 units
    }
    const double rise = 0.005;
    for (const auto& [a, b] : keyOn) {
        const int i0 = static_cast<int>(a * kIn);
        const int i1 = std::min(static_cast<int>(out.size()),
                                static_cast<int>(b * kIn));
        for (int i = std::max(0, i0); i < i1; ++i) {
            const double ts = i / kIn;
            double env = 1.0;
            if (ts - a < rise) env = 0.5 - 0.5 * std::cos(kPi * (ts - a) / rise);
            if (b - ts < rise) env = std::min(env, 0.5 - 0.5 * std::cos(kPi * (b - ts) / rise));
            double fade = 1.0;
            if (fadeDepth > 0.0)
                fade = 1.0 - fadeDepth * (0.5 + 0.5 * std::sin(2.0 * kPi * fadeHz * ts));
            out[static_cast<size_t>(i)] += static_cast<float>(
                st.amp * fade * env * std::sin(2.0 * kPi * st.tone * ts));
        }
    }
}

double stationLengthS(const Station& st) {
    const double unit = 1.2 / st.wpm;
    double t = st.startS;
    for (char ch : st.text) {
        if (ch == ' ') { t += 4.0 * unit; continue; }
        auto it = morse().find(ch);
        if (it == morse().end()) continue;
        for (const char* p = it->second; *p; ++p)
            t += ((*p == '.') ? unit : 3.0 * unit) + unit;
        t += 2.0 * unit;
    }
    return t;
}

// ── scoring ─────────────────────────────────────────────────────────────────
std::string normalise(const std::string& s) {
    std::string out;
    bool sp = true;
    for (char c : s) {
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!sp) out += ' ';
            sp = true;
        } else {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            sp = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                               prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// ── streaming simulation (mirrors NeuralCwDecoder::workerLoop) ──────────────
struct StreamCfg {
    Search search;
    double guardS     = 1.3;    // Guard: fixed commit delay.  Agree: hard cap.
    int    tickMs     = 400;
    float  blankPen   = 0.0f;
    float  keyingMin  = 12.0f;
    float  maskHz     = 0.0f;   // post-conditioner band mask (0 = off)
    bool   agree      = false;  // commit when two decodes agree (else Guard)
    double edgeS      = 0.30;   // Agree: never commit within this of the window end
};

struct StreamResult {
    std::string text;
    double meanLatS  = 0.0;   // tick time minus the character's frame time
    double maxLatS   = 0.0;
    double msPerTick = 0.0;   // conditioner + model + CTC, wall clock
    int    ticks     = 0;
};

StreamResult runStream(DeepFistModel& model, const std::vector<float>& a3200,
                       const StreamCfg& cfg) {
    constexpr int    kWindow  = 6 * kSr;
    constexpr double kWindowS = 6.0;
    constexpr int    kMinSamp = kSr;

    StreamResult r;
    std::vector<float> window(kWindow);
    std::vector<float> logits;
    bool   idleGap   = false;
    double latSum    = 0.0;
    int    latN      = 0;
    double busyMs    = 0.0;
    model.setOutputMaskHz(cfg.maskHz);
    DeepFistCommit commit;                         // the live decoder's commit
    DeepFistCommit::Config cc;
    cc.agree = cfg.agree; cc.guardS = cfg.guardS; cc.edgeS = cfg.edgeS;
    commit.setConfig(cc);

    const double endS = static_cast<double>(a3200.size()) / kSr;
    for (double t = cfg.tickMs / 1000.0; t <= endS + 1e-9; t += cfg.tickMs / 1000.0) {
        const long long total = static_cast<long long>(t * kSr);
        if (total < kMinSamp) continue;

        // newest kWindow samples, zero-padded at the front
        std::fill(window.begin(), window.end(), 0.0f);
        const long long first = std::max<long long>(0, total - kWindow);
        const size_t    count = static_cast<size_t>(total - first);
        std::copy(a3200.begin() + first, a3200.begin() + total,
                  window.end() - static_cast<long long>(count));

        const auto t0 = std::chrono::steady_clock::now();
        const float tone  = model.lockTone(window.data(), kWindow, cfg.search);
        const float ratio = model.keyingRatio(window.data(), kWindow, tone);
        ++r.ticks;
        if (ratio < cfg.keyingMin) {
            busyMs += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();
            commit.idle(t);
            if (!idleGap) { idleGap = true; r.text += ' '; }
            continue;
        }
        idleGap = false;

        int T = 0, C = 0;
        if (!model.inferAt(window.data(), kWindow, tone, logits, T, C)) continue;
        const CtcFrames fr = greedyCtcFrames(logits.data(), T, C, cfg.blankPen);
        busyMs += std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - t0).count();

        const auto& toks = model.tokens();
        for (const DeepFistCommit::Emitted& e : commit.step(fr, t, kWindowS)) {
            if (e.id > 0 && e.id < static_cast<int>(toks.size())) {
                r.text += toks[static_cast<size_t>(e.id)];
                const double lat = t - e.time;
                latSum += lat; ++latN;
                r.maxLatS = std::max(r.maxLatS, lat);
            }
        }
    }
    r.meanLatS  = latN ? latSum / latN : 0.0;
    r.msPerTick = r.ticks ? busyMs / r.ticks : 0.0;
    return r;
}

// ── scenarios ───────────────────────────────────────────────────────────────
struct Scenario {
    const char*          name;
    std::vector<Station> stations;     // [0] is the wanted station
    double               pitchHz;      // operator's CW pitch setting
    double               snrDb;        // wanted station, noise in 500 Hz
    double               fadeDepth = 0.0;
    double               fadeHz    = 0.0;
    bool                 carrier   = false;  // steady unkeyed tone at the pitch
    double               minLenS   = 0.0;
};
// A scenario whose wanted station has no text measures false characters
// (noise-only / steady carrier): the column shows a count, not a CER.
bool isSilentRef(const Scenario& sc) { return sc.stations[0].text.empty(); }

const char* kText1 = "CQ CQ DE N8SDR N8SDR K";
const char* kText2 = "UR RST 579 579 NAME RICK QTH OH BK";
const char* kText3 = "TNX FER CALL ES QSO 73 DE VK3KV K";
const char* kQrm   = "TEST W1AW W1AW TEST DL1ABC 5NN 14";

std::vector<Scenario> scenarios() {
    auto st = [](const char* txt, double wpm, double tone, double amp, double start = 0.5) {
        Station s; s.text = txt; s.wpm = wpm; s.tone = tone; s.amp = amp; s.startS = start;
        return s;
    };
    std::vector<Scenario> v;
    v.push_back({"clean 22wpm",       {st(kText1, 22, 600, 1.0)}, 600, 20});
    v.push_back({"clean 22wpm b",     {st(kText2, 22, 600, 1.0)}, 600, 20});
    v.push_back({"weak 6dB",          {st(kText2, 22, 600, 1.0)}, 600, 6});
    v.push_back({"weak 3dB",          {st(kText3, 20, 600, 1.0)}, 600, 3});
    v.push_back({"fast 35wpm",        {st(kText2, 35, 650, 1.0)}, 650, 15});
    v.push_back({"slow 14wpm",        {st(kText1, 14, 600, 1.0)}, 600, 15});
    v.push_back({"offtune +40Hz",     {st(kText3, 22, 640, 1.0)}, 600, 15});
    v.push_back({"QRM +10dB 300Hz",   {st(kText2, 22, 600, 1.0),
                                       st(kQrm, 28, 900, 3.16, 0.2)}, 600, 15});
    v.push_back({"QRM +6dB 180Hz",    {st(kText3, 20, 650, 1.0),
                                       st(kQrm, 26, 830, 2.0, 0.3)}, 650, 15});
    v.push_back({"QRM -6dB 250Hz",    {st(kText1, 22, 600, 1.0),
                                       st(kQrm, 25, 850, 0.5, 0.1)}, 600, 15});
    v.push_back({"QSB fading",        {st(kText2, 22, 600, 1.0)}, 600, 10, 0.8, 0.25});
    v.push_back({"noise only",        {st("", 22, 600, 1.0)}, 600, 10, 0, 0, false, 20});
    v.push_back({"carrier only",      {st("", 22, 600, 1.0)}, 600, 15, 0, 0, true, 20});
    return v;
}

std::vector<float> render(const Scenario& sc, unsigned seed, std::string& ref) {
    double lenS = 0.0;
    for (const auto& s : sc.stations) lenS = std::max(lenS, stationLengthS(s));
    lenS = std::max(lenS + 3.0, sc.minLenS);       // tail so the last chars commit
    std::vector<float> x(static_cast<size_t>(lenS * kIn), 0.0f);
    for (const auto& s : sc.stations) addStation(x, s, sc.fadeDepth, sc.fadeHz);
    if (sc.carrier)
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += static_cast<float>(sc.stations[0].amp *
                                       std::sin(2.0 * kPi * sc.pitchHz * (i / kIn)));

    // Noise in 500 Hz referenced to the wanted station's power (A^2/2).
    const double a  = sc.stations[0].amp;
    const double ps = a * a / 2.0;
    const double pn = ps / std::pow(10.0, sc.snrDb / 10.0);
    const double sigma = std::sqrt(pn * (kIn / 2.0) / 500.0);
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, sigma);
    for (float& v : x) v += static_cast<float>(g(rng));

    // Scale into a sane audio range, like the post-AGC receiver audio.
    float pk = 1e-9f;
    for (float v : x) pk = std::max(pk, std::fabs(v));
    for (float& v : x) v *= 0.5f / pk;

    ref = normalise(sc.stations[0].text);
    return x;
}

std::vector<float> to3200(const std::vector<float>& x48) {
    DeepFistResampler rs(kIn, kSr);
    std::vector<float> out;
    for (size_t i = 0; i < x48.size(); i += 1024) {
        const int n = static_cast<int>(std::min<size_t>(1024, x48.size() - i));
        rs.process(x48.data() + i, n, out);
    }
    return out;
}

struct Variant {
    std::string name;
    StreamCfg   cfg;
    bool        pinToPitch = false;   // centre the search on the scenario pitch
};

}  // namespace

int main(int argc, char** argv) {
    const std::string modelDir = argc > 1 ? argv[1] : "models";
    DeepFistModel model;
    if (!model.load(modelDir)) {
        std::printf("model load failed (%s): %s\n", modelDir.c_str(),
                    model.lastError().c_str());
        return 2;
    }

    std::vector<Variant> variants;
    auto add = [&](std::string name, Search s, bool pin, double guard, int tick) {
        Variant v; v.name = std::move(name); v.cfg.search = s; v.pinToPitch = pin;
        v.cfg.guardS = guard; v.cfg.tickMs = tick;
        variants.push_back(v);
    };
    auto addX = [&](std::string name, Search s, bool pin, float mask, float gate,
                    bool agree, double guard, int tick) {
        Variant v; v.name = std::move(name); v.cfg.search = s; v.pinToPitch = pin;
        v.cfg.maskHz = mask; v.cfg.keyingMin = gate; v.cfg.agree = agree;
        v.cfg.guardS = guard; v.cfg.tickMs = tick;
        variants.push_back(v);
    };
    const char* only = std::getenv("BENCH_SET");   // tone|speed|fix|commit
    const bool doTone  = !only || std::strcmp(only, "tone") == 0;
    const bool doSpeed = only && std::strcmp(only, "speed") == 0;
    const bool doFix   = only && std::strcmp(only, "fix") == 0;
    const bool doCommit = only && std::strcmp(only, "commit") == 0;
    const bool doFinal  = only && std::strcmp(only, "final") == 0;
    const Search pin{1, 80, true};
    if (doFinal) {   // original behaviour vs the shipped defaults
        addX("before (original)",  Search{}, false, 0,   12, false, 1.3, 400);
        addX("after (new default)", pin,     true,  150, 6,  true,  1.3, 400);
    }
    if (doFix) {
        addX("today",                     Search{}, false, 0,   12, false, 1.3, 400);
        addX("pin",                       pin,      true,  0,   12, false, 1.3, 400);
        addX("pin+mask150",               pin,      true,  150, 12, false, 1.3, 400);
        addX("pin+mask100",               pin,      true,  100, 12, false, 1.3, 400);
        addX("pin+mask150 gate8",         pin,      true,  150, 8,  false, 1.3, 400);
        addX("pin+mask150 gate6",         pin,      true,  150, 6,  false, 1.3, 400);
        addX("pin+mask150 gate4",         pin,      true,  150, 4,  false, 1.3, 400);
        addX("pin+mask150 gate0",         pin,      true,  150, 0,  false, 1.3, 400);
    }
    if (doCommit) {
        const float m = std::getenv("BENCH_MASK") ? std::strtof(std::getenv("BENCH_MASK"), nullptr) : 150.0f;
        const float g = std::getenv("BENCH_GATE") ? std::strtof(std::getenv("BENCH_GATE"), nullptr) : 6.0f;
        addX("guard 1.3 t400",   pin, true, m, g, false, 1.3, 400);
        addX("guard 0.8 t200",   pin, true, m, g, false, 0.8, 200);
        addX("agree t400",       pin, true, m, g, true,  1.3, 400);
        addX("agree t250",       pin, true, m, g, true,  1.3, 250);
        addX("agree t150",       pin, true, m, g, true,  1.3, 150);
    }
    if (doTone) {
        add("A loudest (today)",        Search{},                   false, 1.3, 400);
        add("B loudest, whole window",  Search{0, 0, true},         false, 1.3, 400);
        add("C pitch +/-80, newest",    Search{1, 80, false},       true,  1.3, 400);
        add("D pitch +/-80, whole win", Search{1, 80, true},        true,  1.3, 400);
        add("E pitch +/-50, whole win", Search{1, 50, true},        true,  1.3, 400);
    }
    if (doSpeed) {
        const Search best{1, 80, true};
        add("guard 1.0 tick 400",  best, true, 1.0, 400);
        add("guard 0.8 tick 400",  best, true, 0.8, 400);
        add("guard 0.6 tick 400",  best, true, 0.6, 400);
        add("guard 0.8 tick 200",  best, true, 0.8, 200);
        add("guard 0.6 tick 200",  best, true, 0.6, 200);
        add("guard 0.4 tick 200",  best, true, 0.4, 200);
    }

    const auto scs = scenarios();
    const unsigned seeds[] = {11u, 29u};

    // Pre-render every clip once.
    struct Clip { std::string ref; std::vector<float> a3200; double pitch; };
    std::vector<std::vector<Clip>> clips(scs.size());
    for (size_t s = 0; s < scs.size(); ++s)
        for (unsigned sd : seeds) {
            Clip c;
            const auto x = render(scs[s], sd, c.ref);
            c.a3200 = to3200(x);
            c.pitch = scs[s].pitchHz;
            clips[s].push_back(std::move(c));
        }

    std::printf("%-26s", "variant \\ scenario CER%");
    for (const auto& sc : scs) std::printf(" %15s", sc.name);
    std::printf(" %8s %8s %8s %8s\n", "meanCER", "lat avg", "lat max", "ms/tick");

    for (const Variant& v : variants) {
        std::printf("%-26s", v.name.c_str());
        double cerSum = 0.0, latSum = 0.0, latMax = 0.0, msSum = 0.0;
        int    n = 0, nScored = 0;
        for (size_t s = 0; s < scs.size(); ++s) {
            double cer = 0.0;
            for (const Clip& c : clips[s]) {
                StreamCfg cfg = v.cfg;
                if (v.pinToPitch) cfg.search.centerHz = static_cast<float>(c.pitch);
                const StreamResult r = runStream(model, c.a3200, cfg);
                const std::string hyp = normalise(r.text);
                double e = 0.0;
                if (isSilentRef(scs[s])) {
                    for (char ch : hyp) if (ch != ' ') e += 1.0;   // false chars
                } else {
                    e = static_cast<double>(editDistance(c.ref, hyp)) /
                        std::max<size_t>(1, c.ref.size());
                }
                cer += e;
                latSum += r.meanLatS; latMax = std::max(latMax, r.maxLatS);
                msSum  += r.msPerTick; ++n;
                if (std::getenv("BENCH_VERBOSE"))
                    std::printf("\n    [%s] ref=\"%s\"\n    hyp=\"%s\"", scs[s].name,
                                c.ref.c_str(), hyp.c_str());
            }
            cer /= static_cast<double>(clips[s].size());
            if (isSilentRef(scs[s])) {
                std::printf(" %13.1f c", cer);           // mean false chars
            } else {
                cerSum += cer; ++nScored;
                std::printf(" %15.1f", 100.0 * cer);
            }
        }
        std::printf(" %8.1f %7.2fs %7.2fs %8.1f\n", 100.0 * cerSum / std::max(1, nScored),
                    latSum / n, latMax, msSum / n);
        std::fflush(stdout);
    }
    return 0;
}
