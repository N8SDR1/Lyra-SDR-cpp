// Lyra — one draggable hover-help card over the main window.
//
// Child overlay of MainWindow (not a Qt::Tool window — those ate the
// Solar / Propagation QWidget chip tips).  Draggable onto the spectrum
// or waterfall.  Position persists in QSettings.

#pragma once

#include <QFrame>
#include <QHash>
#include <QPoint>

class QLabel;
class QDockWidget;
class QMouseEvent;
class QScrollArea;

namespace lyra::ui {

class HelpHint;
class Prefs;
class MainWindow;

class HelpCard : public QFrame {
    Q_OBJECT
public:
    HelpCard(MainWindow *host, HelpHint *hint, Prefs *prefs,
             QHash<QString, QDockWidget *> *docks);

    void syncVisibility();
    void applyDefaultOrSaved();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshText();
    void relayout();
    void persistPos();
    void followHost();
    QPoint defaultLocalPos() const;
    QPoint clampLocal(QPoint local) const;

    MainWindow *host_ = nullptr;
    HelpHint   *hint_ = nullptr;
    Prefs      *prefs_ = nullptr;
    QHash<QString, QDockWidget *> *docks_ = nullptr;
    QLabel     *title_ = nullptr;
    QLabel     *body_ = nullptr;
    QScrollArea *scroll_ = nullptr;

    bool   dragging_ = false;
    bool   parked_ = false;
    QPoint dragOffset_;
    QPoint localPos_;
};

} // namespace lyra::ui
