// Lyra — CW morse engine (#105 CW-3a). See CwMorse.h.

#include "CwMorse.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace lyra::tx {

namespace {

// ASCII → morse pattern ('.' = dit, '-' = dah). Covers letters,
// digits, common punctuation, and a few prosigns mapped onto the
// characters loggers/TCI clients send for them. Anything not here
// is treated as unsendable and skipped by the caller.
struct MorseRow { char ch; const char* pat; };

constexpr MorseRow kTable[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
    {'=', "-...-"},  {'+', ".-.-."},  {'-', "-....-"}, {'(', "-.--."},
    {')', "-.--.-"}, {':', "---..."}, {';', "-.-.-."}, {'\'', ".----."},
    {'"', ".-..-."}, {'@', ".--.-."}, {'!', "-.-.--"}, {'&', ".-..."},
};

const char* lookup(char up) noexcept {
    for (const auto& r : kTable)
        if (r.ch == up) return r.pat;
    return nullptr;
}

}  // namespace

int cwDitMs(int wpm) noexcept {
    wpm = std::clamp(wpm, 5, 100);
    return static_cast<int>(1200.0 / wpm + 0.5);       // rounded, not truncated
}

int cwDitUs(int wpm) noexcept {
    wpm = std::clamp(wpm, 5, 100);
    return static_cast<int>(1200000.0 / wpm + 0.5);    // dit in microseconds
}

std::vector<CwElement> cwTextToElements(const std::string& text,
                                        int wpm,
                                        int weightPct) {
    weightPct = std::clamp(weightPct, 10, 90);

    // Weight scales mark + intra-element gap around 50 % neutral;
    // a long element is 3 mark units; gaps below stay nominal.
    const double markScale  = weightPct / 50.0;
    const double spaceScale = (100 - weightPct) / 50.0;
    int speed = std::clamp(wpm, 5, 100);
    int dit = 0, markDit = 0, intraGap = 0, charGap = 0, wordGap = 0;
    // `<` / `>` step the speed of everything after them. Plain text
    // never hits this, so its element list stays the same.
    auto retiming = [&]() {
        dit      = cwDitUs(speed);
        markDit  = std::max(1, static_cast<int>(dit * markScale + 0.5));
        intraGap = std::max(1, static_cast<int>(dit * spaceScale + 0.5));
        charGap  = 3 * dit;
        wordGap  = 7 * dit;
    };
    retiming();

    std::vector<CwElement> out;
    bool prevWasChar = false;   // a sendable char was emitted before
    bool inProsign = false;
    bool proJoin = false;       // next letter inside |SK| uses the intra gap

    auto emitPattern = [&](const char* pat, bool tight) {
        if (prevWasChar)
            out.push_back({false, tight ? intraGap : charGap});
        for (const char* p = pat; *p; ++p) {
            out.push_back({true, (*p == '-') ? 3 * markDit : markDit});
            if (*(p + 1))
                out.push_back({false, intraGap});
        }
        prevWasChar = true;
    };

    for (unsigned char raw : text) {
        const char up = static_cast<char>(std::toupper(raw));

        if (inProsign) {
            if (up == '|') { inProsign = false; proJoin = false; continue; }
            if (up == ' ' || up == '\t' || up == '\n' || up == '\r') continue;
            const char* pat = lookup(up);
            if (!pat) continue;
            emitPattern(pat, proJoin);
            proJoin = true;
            continue;
        }
        if (up == '|') { inProsign = true; proJoin = false; continue; }
        if (up == '<') { speed = std::max(5, speed - 5); retiming(); continue; }
        if (up == '>') { speed = std::min(100, speed + 5); retiming(); continue; }

        if (up == ' ' || up == '\t' || up == '\n' || up == '\r') {
            // Word break: replace any pending inter-char gap with the
            // wider inter-word gap. Leading/duplicate spaces are no-ops.
            if (prevWasChar) {
                if (!out.empty() && !out.back().key)
                    out.back().durationUs = wordGap;  // upgrade char→word gap
                else
                    out.push_back({false, wordGap});
                prevWasChar = false;
            }
            continue;
        }

        const char* pat = lookup(up);
        if (!pat) continue;   // unsendable — skip, no keying

        // Inter-character gap before this char (not before the first).
        if (prevWasChar)
            out.push_back({false, charGap});

        for (const char* p = pat; *p; ++p) {
            out.push_back({true, (*p == '-') ? 3 * markDit : markDit});
            if (*(p + 1))                       // intra gap between elements
                out.push_back({false, intraGap});
        }
        prevWasChar = true;
    }

    // No trailing gap — the keyer owns PTT tail timing.
    return out;
}

}  // namespace lyra::tx
