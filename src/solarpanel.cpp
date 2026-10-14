// Lyra — solar / HF-propagation panel.  See solarpanel.h.

#include "solarpanel.h"

#include "bands.h"
#include "bandmemory.h"
#include "genslots.h"
#include "hl2_stream.h"
#include "solarservice.h"
#include "ncdxffollow.h"

#include <QActionGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

namespace lyra::ui {

using lyra::solar::Rating;
using lyra::solar::SolarService;

namespace {
QString orDash(const QString &s) {
    const QString t = s.trimmed();
    if (t.isEmpty() || t == QLatin1String("?") ||
        t.compare(QLatin1String("N/A"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("—");
    return t;
}
QFrame *vdivider(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QStringLiteral("QFrame{color:#2a3a4a;}"));
    return line;
}
} // namespace

SolarPanel::SolarPanel(SolarService *svc, NcdxfFollow *follow,
                         lyra::ipc::HL2Stream *stream, BandMemory *bandMem,
                         GenSlots *gen, QWidget *parent)
    : QWidget(parent), svc_(svc), follow_(follow), stream_(stream),
      bandMem_(bandMem), gen_(gen) {
    // Match the shared panel theme (the QML panels are #101820 / #2a4a5a).
    // ID-scoped so it styles only this panel's background, not the child
    // value boxes (which keep their own rgba cell backgrounds).
    // The `QWidget#solarPanel` rule in the app stylesheet (theme.h) gives
    // this panel the same surface as the QML panels.  WA_StyledBackground
    // ensures a plain QWidget subclass actually paints that background.
    setObjectName(QStringLiteral("solarPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(10, 6, 10, 6);
    row->setSpacing(10);

    sfiBox_ = makeValueBox(tr("SFI"), &sfiVal_);
    row->addWidget(sfiBox_);
    row->addWidget(makeValueBox(tr("A"), &aVal_));
    row->addWidget(makeValueBox(tr("K"), &kVal_));
    row->addWidget(vdivider(this));

    // 10-band heat-map. Each chip tunes VFO A to that band.
    for (const QString &b : SolarService::bands()) {
        auto *cell = new QToolButton(this);
        cell->setText(b);
        cell->setFixedSize(44, 22);   // identical chip size to the Band panel
        cell->setCursor(Qt::PointingHandCursor);
        cell->setFocusPolicy(Qt::NoFocus);
        cell->setAutoRaise(true);
        const QString band = b;
        connect(cell, &QToolButton::clicked, this, [this, band]() {
            tuneVfoA(band);
        });
        bandCells_.insert(b, cell);
        row->addWidget(cell);
    }

    // NCDXF beacon auto-follow — "Beacons" caption above the dropdown.
    row->addWidget(vdivider(this));
    buildFollowButton();
    if (followBtn_) {
        auto *box = new QWidget(this);
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        auto *cap = new QLabel(tr("Beacons"), box);
        cap->setAlignment(Qt::AlignCenter);
        cap->setStyleSheet(QStringLiteral("QLabel{color:#7a8a9c;font-size:15px;}"));
        v->addWidget(cap);
        v->addWidget(followBtn_);
        row->addWidget(box);
    }

    row->addStretch(1);

    if (svc_)
        connect(svc_, &SolarService::dataChanged, this, &SolarPanel::refresh);
    if (follow_)
        connect(follow_, &NcdxfFollow::stationChanged, this,
                [this]() { refreshFollowLabel(); });
    refresh();
    refreshFollowLabel();
}

void SolarPanel::buildFollowButton() {
    followBtn_ = new QToolButton(this);
    followBtn_->setPopupMode(QToolButton::InstantPopup);
    followBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    followBtn_->setCursor(Qt::PointingHandCursor);
    followBtn_->setToolTip(tr("NCDXF Beacon Auto-Follow — pick a station and "
        "Lyra auto-tunes through the rotation (20 → 17 → 15 → 12 "
        "→ 10 m, every 10 s) to wherever it's transmitting now."));
    followBtn_->setStyleSheet(QStringLiteral(
        "QToolButton{color:#cdd9e5;background:transparent;"
        "border:1px solid #2a3a4a;border-radius:3px;padding:3px 10px;"
        "font-size:13px;font-weight:600;}"
        "QToolButton:hover{border-color:#4a90c2;}"
        "QToolButton::menu-indicator{width:0;}"));

    auto *menu = new QMenu(followBtn_);
    auto *grp  = new QActionGroup(menu);
    grp->setExclusive(true);

    auto *off = menu->addAction(tr("Off  (no follow)"));
    off->setCheckable(true);
    off->setData(QString());
    grp->addAction(off);
    connect(off, &QAction::triggered, this,
            [this]() { if (follow_) follow_->setStation(QString()); });
    menu->addSeparator();

    for (const auto &s : NcdxfFollow::stations()) {
        auto *a = menu->addAction(QStringLiteral("%1  (%2)").arg(s.call, s.qth));
        a->setCheckable(true);
        a->setData(s.call);
        grp->addAction(a);
        const QString call = s.call;
        connect(a, &QAction::triggered, this,
                [this, call]() { if (follow_) follow_->setStation(call); });
    }
    followBtn_->setMenu(menu);
}

void SolarPanel::refreshFollowLabel() {
    if (!followBtn_) return;
    const QString call = follow_ ? follow_->station() : QString();
    followBtn_->setText(call.isEmpty() ? tr("▾  Follow: Off")
                                       : tr("▾  Follow: %1").arg(call));
    if (followBtn_->menu()) {
        const auto acts = followBtn_->menu()->actions();
        for (QAction *a : acts)
            if (a->isCheckable() && a->data().toString() == call)
                a->setChecked(true);
    }
}

QWidget *SolarPanel::makeValueBox(const QString &key, QLabel **valueOut) {
    auto *box = new QWidget(this);
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *k = new QLabel(key, box);
    k->setAlignment(Qt::AlignCenter);
    // Modest bump from the original 10px so the label reads cleanly.
    k->setStyleSheet(QStringLiteral("QLabel{color:#7a8a9c;font-size:15px;}"));
    auto *val = new QLabel(QStringLiteral("—"), box);
    val->setAlignment(Qt::AlignCenter);
    // Slightly larger value (13px) — legible but small enough that the
    // row height (and therefore the band cells) stays as it was.
    val->setStyleSheet(QStringLiteral(
        "QLabel{color:#cdd9e5;font-family:Consolas;font-size:15px;"
        "font-weight:700;}"));
    v->addWidget(k);
    v->addWidget(val);
    *valueOut = val;
    return box;
}

void SolarPanel::refresh() {
    if (!svc_) return;
    const auto &d = svc_->data();

    const QString valCss = QStringLiteral(
        "QLabel{color:%1;font-family:Consolas;font-size:15px;font-weight:700;}");
    sfiVal_->setText(orDash(d.sfi));
    sfiVal_->setStyleSheet(valCss.arg(SolarService::sfiColor(d.sfi)));
    aVal_->setText(orDash(d.aindex));
    aVal_->setStyleSheet(valCss.arg(SolarService::aIndexColor(d.aindex)));
    kVal_->setText(orDash(d.kindex));
    kVal_->setStyleSheet(valCss.arg(SolarService::kIndexColor(d.kindex)));

    // Extras tooltip on the SFI box.
    const QString when = svc_->isDaylight() ? tr("day") : tr("night");
    QString tip = QStringLiteral(
        "<b>Solar &amp; HF propagation</b> (HamQSL)<br>"
        "SFI: %1   SSN: %2<br>A: %3   K: %4<br>"
        "X-Ray: %5   Solar wind: %6 km/s<br>"
        "Conditions shown for: <b>%7</b><br>"
        "<span style='color:#7a8a9c'>Updated: %8</span>")
        .arg(orDash(d.sfi), orDash(d.sunspots), orDash(d.aindex),
             orDash(d.kindex), orDash(d.xray), orDash(d.solarwind),
             when, orDash(d.updated));
    if (!svc_->haveLocation())
        tip += QStringLiteral("<br><span style='color:#f0c040'>Set your grid "
                              "square in Settings → Hardware for accurate "
                              "day/night.</span>");
    if (!d.error.isEmpty())
        tip += QStringLiteral("<br><span style='color:#e05c5c'>Last fetch: "
                              "%1</span>").arg(d.error);
    sfiBox_->setToolTip(tip);

    // Band heat-map.
    for (auto it = bandCells_.cbegin(); it != bandCells_.cend(); ++it) {
        const QString &band = it.key();
        QToolButton *cell = it.value();
        const Rating r = svc_->ratingForBand(band);
        const QString col = SolarService::ratingColor(r);
        // Shared "chip" geometry with the Band panel buttons (50/50
        // design blend): radius/fill/padding/text matched; the border +
        // text colour stay condition-driven.
        cell->setStyleSheet(QStringLiteral(
            "QToolButton{background:rgba(22,32,42,180);color:%1;"
            "border:1px solid %1;border-radius:4px;"
            "font-size:13px;font-weight:700;padding:0;}"
            "QToolButton:hover{background:rgba(40,60,80,220);}").arg(col));
        QString rtxt;
        switch (r) {
        case Rating::Good: rtxt = tr("Good"); break;
        case Rating::Fair: rtxt = tr("Fair"); break;
        case Rating::Poor: rtxt = tr("Poor"); break;
        default:           rtxt = tr("n/a");  break;
        }
        cell->setToolTip(QStringLiteral("<b>%1m</b> — %2 (%3)<br>"
                                       "Click to tune VFO A to this band.")
                             .arg(band, rtxt, when));
    }
}

void SolarPanel::tuneVfoA(const QString &meters) {
    // A beacon follow would retune VFO A on the next slot. Drop it so
    // the chip sticks.
    if (follow_ && !follow_->station().isEmpty())
        follow_->setStation(QString());
    if (gen_)
        gen_->deactivate();
    const QString key = meters + QLatin1String("m");
    int hz = bandMem_ ? bandMem_->freqFor(key) : 0;
    if (hz <= 0) {
        for (const lyra::Band &b : lyra::amateurBands()) {
            if (key == QLatin1String(b.name)) {
                hz = b.defaultHz;
                break;
            }
        }
    }
    if (hz > 0 && stream_)
        stream_->setRx1FreqHz(static_cast<quint32>(hz));
}

} // namespace lyra::ui
