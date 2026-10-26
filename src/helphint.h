// Lyra — parkable hover-help bridge (QML → one floating card).
//
// QML controls call setHoverHelp / clearHoverHelp.  The HelpCard widget
// (parented as a Tool window of the main window) renders the text.  This
// object never shows a Qt ToolTip.

#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace lyra::ui {

class Prefs;

class HelpHint : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString body READ body NOTIFY bodyChanged)
    Q_PROPERTY(bool filled READ filled NOTIFY filledChanged)

public:
    explicit HelpHint(Prefs *prefs, QObject *parent = nullptr);

    QString title() const { return title_; }
    QString body() const { return body_; }
    bool filled() const { return filled_; }

    Q_INVOKABLE void setHoverHelp(const QString &title, const QString &text);
    Q_INVOKABLE void clearHoverHelp();

signals:
    void titleChanged();
    void bodyChanged();
    void filledChanged();

private:
    void applyPending();
    void applyClear();
    void publish(const QString &title, const QString &text, bool filled);
    bool tipsOn() const;

    Prefs  *prefs_ = nullptr;
    QTimer *showTimer_ = nullptr;
    QTimer *clearTimer_ = nullptr;
    QString pendingTitle_;
    QString pendingBody_;
    QString title_;
    QString body_;
    bool    filled_ = false;
};

} // namespace lyra::ui
