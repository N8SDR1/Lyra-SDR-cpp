#include "helphint.h"
#include "prefs.h"

#include <QTimer>

namespace lyra::ui {

HelpHint::HelpHint(Prefs *prefs, QObject *parent)
    : QObject(parent), prefs_(prefs) {
    showTimer_ = new QTimer(this);
    showTimer_->setSingleShot(true);
    showTimer_->setInterval(550);
    connect(showTimer_, &QTimer::timeout, this, &HelpHint::applyPending);

    clearTimer_ = new QTimer(this);
    clearTimer_->setSingleShot(true);
    clearTimer_->setInterval(2200);
    connect(clearTimer_, &QTimer::timeout, this, &HelpHint::applyClear);

    if (prefs_) {
        connect(prefs_, &Prefs::tooltipsEnabledChanged, this, [this]() {
            if (!tipsOn()) {
                showTimer_->stop();
                clearTimer_->stop();
                publish({}, {}, false);
            }
        });
    }
}

bool HelpHint::tipsOn() const {
    return prefs_ && prefs_->tooltipsEnabled();
}

void HelpHint::setHoverHelp(const QString &title, const QString &text) {
    if (!tipsOn() || text.trimmed().isEmpty())
        return;
    pendingTitle_ = title.trimmed();
    pendingBody_ = text.trimmed();
    clearTimer_->stop();
    // Moving between controls: update immediately so the parked card
    // tracks the pointer.  First hover after idle waits the short delay.
    if (filled_)
        applyPending();
    else
        showTimer_->start();
}

void HelpHint::clearHoverHelp() {
    showTimer_->stop();
    if (!filled_)
        return;
    // Keep the last tip on the parked card briefly (and while the
    // operator is still dragging a slider off-thumb).
    clearTimer_->start();
}

void HelpHint::applyPending() {
    if (!tipsOn())
        return;
    publish(pendingTitle_, pendingBody_, true);
}

void HelpHint::applyClear() {
    publish({}, {}, false);
}

void HelpHint::publish(const QString &title, const QString &text, bool filled) {
    const bool titleCh = title != title_;
    const bool bodyCh = text != body_;
    const bool fillCh = filled != filled_;
    title_ = title;
    body_ = text;
    filled_ = filled;
    if (titleCh) emit titleChanged();
    if (bodyCh) emit bodyChanged();
    if (fillCh) emit filledChanged();
}

} // namespace lyra::ui
