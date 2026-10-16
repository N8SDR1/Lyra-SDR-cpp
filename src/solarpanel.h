// Lyra — solar / HF-propagation panel (dockable "PROP" strip).
//
// A compact horizontal readout (ported from old Lyra's
// propagation_panel.py): SFI / A-index / K-index value boxes (colour-
// coded) plus a 10-band Good/Fair/Poor heat-map keyed to the operator's
// current day or night.  Driven by SolarService; pure QtWidgets.

#pragma once

#include <QHash>
#include <QWidget>

class QLabel;
class QToolButton;

namespace lyra::ipc { class HL2Stream; }
namespace lyra::solar { class SolarService; }

namespace lyra::ui {

class BandMemory;
class GenSlots;
class NcdxfFollow;

class SolarPanel : public QWidget {
    Q_OBJECT
public:
    SolarPanel(lyra::solar::SolarService *svc, NcdxfFollow *follow,
               lyra::ipc::HL2Stream *stream, BandMemory *bandMemory,
               GenSlots *gen, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *makeValueBox(const QString &key, QLabel **valueOut);
    void buildFollowButton();
    void refreshFollowLabel();
    void refresh();
    // Same destination as a Band-panel chip: left click follows the
    // focused receiver; Shift+click and right-click park SUB there.
    // Ignored while transmitting (a band change retunes filters / PA).
    void tuneBand(const QString &bandKey, bool forceSub);

    lyra::solar::SolarService *svc_ = nullptr;
    NcdxfFollow               *follow_ = nullptr;
    lyra::ipc::HL2Stream      *stream_ = nullptr;
    BandMemory                *bandMemory_ = nullptr;
    GenSlots                  *gen_ = nullptr;
    QToolButton               *followBtn_ = nullptr;
    QLabel  *sfiVal_ = nullptr;
    QLabel  *aVal_   = nullptr;
    QLabel  *kVal_   = nullptr;
    QWidget *sfiBox_ = nullptr;            // tooltip carrier for extras
    QHash<QString, QToolButton *> bandCells_;   // band → coloured chip
};

} // namespace lyra::ui
