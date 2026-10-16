// Lyra — VFO digit hit-testing (FreqDisplay mouse / wheel).
//
// The active VFO style reports one rect per digit place (index = place:
// 0 = Hz units … 9 = 1 GHz) in FreqDisplay's coordinates.  An empty rect —
// or a missing / non-rect entry — means that digit isn't shown and can't be
// clicked.  Pure functions so scratch/test_vfo_digit_hit.cpp can pin them.

#pragma once

#include <QPointF>
#include <QRectF>
#include <QVariant>
#include <QVariantList>

#include <vector>

namespace lyra::ui {

inline std::vector<QRectF> digitRectsFromVariant(const QVariantList &list,
                                                 int nDigits)
{
    std::vector<QRectF> out(static_cast<size_t>(nDigits));
    for (int i = 0; i < nDigits && i < list.size(); ++i) {
        const QVariant &v = list.at(i);
        if (v.canConvert<QRectF>())
            out[static_cast<size_t>(i)] = v.toRectF();
    }
    return out;
}

inline int digitPlaceAt(const std::vector<QRectF> &rects, const QPointF &p)
{
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!rects[i].isEmpty() && rects[i].contains(p))
            return static_cast<int>(i);
    }
    return -1;
}

}  // namespace lyra::ui
