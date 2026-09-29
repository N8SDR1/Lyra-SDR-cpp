// HID mouse-wheel VFO (USB encoder that enumerates as a mouse).
// Settings → Hardware → Navigation.  Windows Raw Input; not MIDI.

#pragma once

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

namespace lyra::ipc { class HL2Stream; }
namespace lyra::dsp { class WdspEngine; }

namespace lyra::ui {

class Prefs;

struct HidMouseEntry {
    QString id;      // Raw Input device path (persisted)
    QString label;   // Operator-facing (VID/PID + short name)
};

class HidVfoWheel : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    static HidVfoWheel *instance();

    explicit HidVfoWheel(QObject *parent = nullptr);
    ~HidVfoWheel() override;

    void bind(QWidget *host, Prefs *prefs, lyra::ipc::HL2Stream *stream,
              lyra::dsp::WdspEngine *wdsp);
    void reregister();

    static QVector<HidMouseEntry> enumerateMice();
    static bool idsEquivalent(const QString &saved, const QString &got);

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void wheelHeard();   // selected device ticked (Settings wheel-test)

private:
    void applyTicks(int ticks);
    bool deviceMatches(const QString &rawName) const;
    QString deviceNameFor(void *hDevice) const;

    Prefs *prefs_ = nullptr;
    lyra::ipc::HL2Stream *stream_ = nullptr;
    lyra::dsp::WdspEngine *wdsp_ = nullptr;
    quintptr hwnd_ = 0;
    bool registered_ = false;
    QElapsedTimer lastMatch_;
};

} // namespace lyra::ui
