// Lyra — in-app Log viewer (Help → Show Log…).
//
// A read-only monospace view of the captured diagnostic log (see
// LogBuffer) with Copy, Save-to-file and Open-folder buttons so a
// non-technical operator can grab the text and send it to us when
// something misbehaves.  Updates live while open.  A "Verbose debug
// logging" checkbox mirrors the Settings → Hardware toggle.

#pragma once

#include <QDialog>

class QPlainTextEdit;
class QCheckBox;

namespace lyra::ui {

class Prefs;

class LogDialog : public QDialog {
    Q_OBJECT
public:
    explicit LogDialog(Prefs *prefs, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void fitToScreen();
    void appendLine(const QString &line);
    void copyAll();
    void saveToFile();
    void openFolder();

    Prefs          *prefs_ = nullptr;
    QPlainTextEdit *view_  = nullptr;
    QCheckBox      *verbose_ = nullptr;
};

} // namespace lyra::ui
