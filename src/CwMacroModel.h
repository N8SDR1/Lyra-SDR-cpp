// Lyra — #176 CW macro bank (named click/F-key send macros for the CW console).
//
// Single source of truth for the CW memory macros + the "current contact" row
// the tokens fill from.  Owns expand() + send() so BOTH triggers use ONE path:
//   - QML chip click  -> CwMacros.sendIndex(i)
//   - MainWindow F1-F12 key -> CwMacros.sendByFkey(n)   (global, like a real
//     keyer — QML Shortcut doesn't fire inside the QQuickWidget docks)
// Macros + serial persist to QSettings (cw/*); the contact call/name are
// session-live (a fresh QSO each session).  Text tokens: {MYCALL}
// (Prefs.callsign), {CALL}/{RST}/{NAME} (contact row), {#} (serial).  Action
// token: {LOG} — stripped from the keyed CW, fires logQsoRequested() so a
// linked SDRLogger+ (Combo) logs the QSO (e.g. "TU 73 {MYCALL} ee {LOG}").
// Sends via HL2Stream::sendCw.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class QTimer;
namespace lyra { namespace ipc { class HL2Stream; } }

namespace lyra::ui {

class Prefs;

class CwMacroModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList macros READ macros NOTIFY macrosChanged)
    Q_PROPERTY(QVariantList tokens READ tokens NOTIFY tokensChanged)
    Q_PROPERTY(int    sendingIndex READ sendingIndex NOTIFY sendingChanged)
    Q_PROPERTY(QString hisCall READ hisCall WRITE setHisCall NOTIFY contactChanged)
    Q_PROPERTY(QString rst     READ rst     WRITE setRst     NOTIFY contactChanged)
    Q_PROPERTY(QString opName  READ opName  WRITE setOpName  NOTIFY contactChanged)
    Q_PROPERTY(int     serial  READ serial  WRITE setSerial  NOTIFY serialChanged)
    // #105 CW-3b — CW console send mode: false = Manual (compose then
    // Send/Enter), true = Auto (send as you type / CWX type-ahead).
    // Persisted (cw/typeAhead).
    Q_PROPERTY(bool    typeAhead READ typeAhead WRITE setTypeAhead NOTIFY typeAheadChanged)
public:
    CwMacroModel(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                 QObject *parent = nullptr);

    QVariantList macros() const;          // [{name,text,fkey,user}] for the QML Repeater
    QVariantList tokens() const;          // [{index,name,value}] personal tokens
    int  sendingIndex() const { return sendingIndex_; }
    QString hisCall() const { return hisCall_; }
    QString rst()     const { return rst_; }
    QString opName()  const { return opName_; }
    int     serial()  const { return serial_; }
    bool    typeAhead() const { return typeAhead_; }

    void setTypeAhead(bool on);
    void setHisCall(const QString &s);
    void setRst(const QString &s);
    void setOpName(const QString &s);
    void setSerial(int n);

    // Token substitution: {MYCALL} {CALL} {RST} {NAME} {#} (case-insensitive).
    Q_INVOKABLE QString expand(const QString &text) const;

    Q_INVOKABLE void sendIndex(int i);    // expand macros[i] -> keyer + sending state
    Q_INVOKABLE void stop();              // abort keying + clear sending
    Q_INVOKABLE void bumpSerial(int delta);

    // Editing (persisted).  addMacro assigns the next free F-key; user=true.
    Q_INVOKABLE void setMacro(int i, const QString &name, const QString &text);
    Q_INVOKABLE void addMacro(const QString &name, const QString &text);
    Q_INVOKABLE void removeMacro(int i);

    // Personal tokens (name→value), persisted.  Expanded in macros + the
    // keyboard line after the built-ins (so the 5 built-in names win).
    Q_INVOKABLE void setToken(int i, const QString &name, const QString &value);
    Q_INVOKABLE void addToken(const QString &name, const QString &value);
    Q_INVOKABLE void removeToken(int i);

    // MainWindow F-key hook (global accelerator).  fn = 1..12.
    void sendByFkey(int fn);

signals:
    void macrosChanged();
    void tokensChanged();
    void sendingChanged();
    void serialChanged();
    void typeAheadChanged();
    void contactChanged();
    // Emitted when a sent macro contained the {LOG} action token — the CW
    // Console asking a linked SDRLogger+ (Combo) to log the current QSO. The
    // token itself is stripped from the keyed text (never sent as morse); this
    // fires alongside the send. Self-authorizing consent: the operator put
    // {LOG} in the macro on purpose. See combo_link_design.md Stage B.
    void logQsoRequested();

private:
    struct Macro { QString name; QString text; int fkey = 0; bool user = false; };
    struct Token { QString name; QString value; };
    void load();
    void save() const;
    void saveTokens() const;
    void seedDefaults();
    void startSending(int i, const QString &expanded);

    lyra::ui::Prefs      *prefs_  = nullptr;
    lyra::ipc::HL2Stream *stream_ = nullptr;
    QVector<Macro> macros_;
    QVector<Token> tokens_;
    int      sendingIndex_ = -1;
    QString  hisCall_;
    QString  rst_  = QStringLiteral("599");
    QString  opName_;
    int      serial_ = 1;
    bool     typeAhead_ = false;     // Manual by default (cw/typeAhead)
    QTimer  *sendTimer_ = nullptr;   // clears the sending highlight after the est. burst
};

} // namespace lyra::ui
