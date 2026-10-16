// Lyra — wheel events to VFO digit steps (FreqDisplay::wheelEvent).
//
// Qt's angleDelta is nominally 120 units per wheel notch, but platforms
// break that in different ways:
//
//   * macOS reports a mouse with smooth scrolling (most third-party mouse
//     drivers) as a pixel-scroll device: each notch arrives as ONE
//     un-phased event whose size depends on speed — ±24 for a slow notch,
//     ±206 normal, ±410 for a flick.  Dividing by 120 dropped slow notches
//     and turned flicks into several digit steps.  There, one event is one
//     notch: step once in its direction.
//   * Windows hi-res wheels split a notch into fractions of 120, so
//     un-phased events elsewhere accumulate to 120 per step.  A standard
//     120-per-notch mouse steps exactly as before.
//   * Trackpad gestures (any phase other than NoScrollPhase) stream many
//     small deltas; they accumulate to 120 per step on every platform, and
//     each new gesture starts from zero.
//
// `accumulator` carries the partial delta between events (one per widget).
// Returns the signed number of digit steps to apply, possibly 0.

#pragma once

#include <QtCore/qnamespace.h>

namespace lyra::ui {

inline int wheelDigitSteps(int angleDeltaY, Qt::ScrollPhase phase,
                           bool notchIsOneStep, int &accumulator)
{
    if (phase == Qt::NoScrollPhase && notchIsOneStep) {
        accumulator = 0;
        return (angleDeltaY > 0) - (angleDeltaY < 0);
    }
    if (phase == Qt::ScrollBegin)
        accumulator = 0;
    accumulator += angleDeltaY;
    const int steps = accumulator / 120;
    accumulator -= steps * 120;
    return steps;
}

}  // namespace lyra::ui
