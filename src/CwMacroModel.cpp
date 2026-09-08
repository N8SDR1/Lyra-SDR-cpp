// Lyra — #176 CW macro bank.  See CwMacroModel.h.

#include "CwMacroModel.h"

#include "prefs.h"
#include "hl2_stream.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <algorithm>

namespace {
constexpr auto kMacrosKey    = "cw/macros";
constexpr auto kSerialKey    = "cw/serial";
constexpr auto kTokensKey    = "cw/tokens";
constexpr auto kTypeAheadKey = "cw/typeAhead";
}

namespace lyra::ui {

CwMacroModel::CwMacroModel(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                           QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream) {
    sendTimer_ = new QTimer(this);
    sendTimer_->setSingleShot(true);
    connect(sendTimer_, &QTimer::timeout, this, [this]() {
        sendingIndex_ = -1;
        emit sendingChanged();
    });
    load();
}

void CwMacroModel::seedDefaults() {
    macros_ = {
        {QStringLiteral("CQ"),         QStringLiteral("CQ CQ DE {MYCALL} {MYCALL} K"), 1, false},
        {QStringLiteral("CQ contest"), QStringLiteral("CQ TEST {MYCALL} {MYCALL}"),    2, false},
        {QStringLiteral("His call"),   QStringLiteral("{CALL}"),                       3, false},
        {QStringLiteral("Reply"),      QStringLiteral("{CALL} DE {MYCALL} {RST} {RST}"),4, false},
        {QStringLiteral("Exchange"),   QStringLiteral("{RST} {#} {#}"),                5, false},
        {QStringLiteral("TU 73"),      QStringLiteral("TU 73 {MYCALL} ee"),            6, false},
        {QStringLiteral("AGN?"),       QStringLiteral("AGN AGN"),                      7, false},
        {QStringLiteral("QRZ?"),       QStringLiteral("QRZ? DE {MYCALL}"),             8, false},
    };
}

void CwMacroModel::load() {
    QSettings s;
    serial_    = std::max(1, s.value(kSerialKey, 1).toInt());
    typeAhead_ = s.value(kTypeAheadKey, false).toBool();
    const QByteArray raw = s.value(kMacrosKey).toByteArray();
    macros_.clear();
    if (!raw.isEmpty()) {
        const QJsonArray arr = QJsonDocument::fromJson(raw).array();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            macros_.push_back({o.value("name").toString(),
                               o.value("text").toString(),
                               o.value("fkey").toInt(0),
                               o.value("user").toBool(false)});
        }
    }
    if (macros_.isEmpty()) {     // first run / cleared → seed + persist
        seedDefaults();
        save();
    }

    tokens_.clear();
    const QByteArray traw = s.value(kTokensKey).toByteArray();
    if (!traw.isEmpty()) {
        const QJsonArray arr = QJsonDocument::fromJson(traw).array();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            tokens_.push_back({o.value("name").toString(), o.value("value").toString()});
        }
    }
}

void CwMacroModel::saveTokens() const {
    QJsonArray arr;
    for (const Token &t : tokens_) {
        QJsonObject o;
        o["name"] = t.name; o["value"] = t.value;
        arr.append(o);
    }
    QSettings().setValue(kTokensKey, QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList CwMacroModel::tokens() const {
    QVariantList out;
    for (int i = 0; i < tokens_.size(); ++i) {
        QVariantMap e;
        e["index"] = i;
        e["name"]  = tokens_[i].name;
        e["value"] = tokens_[i].value;
        out.append(e);
    }
    return out;
}

void CwMacroModel::setToken(int i, const QString &name, const QString &value) {
    if (i < 0 || i >= tokens_.size()) return;
    tokens_[i].name = name;
    tokens_[i].value = value;
    saveTokens();
    emit tokensChanged();
    emit macrosChanged();   // previews re-expand
}

void CwMacroModel::addToken(const QString &name, const QString &value) {
    tokens_.push_back({name, value});
    saveTokens();
    emit tokensChanged();
    emit macrosChanged();
}

void CwMacroModel::removeToken(int i) {
    if (i < 0 || i >= tokens_.size()) return;
    tokens_.remove(i);
    saveTokens();
    emit tokensChanged();
    emit macrosChanged();
}

void CwMacroModel::save() const {
    QJsonArray arr;
    for (const Macro &m : macros_) {
        QJsonObject o;
        o["name"] = m.name; o["text"] = m.text; o["fkey"] = m.fkey; o["user"] = m.user;
        arr.append(o);
    }
    QSettings s;
    s.setValue(kMacrosKey, QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList CwMacroModel::macros() const {
    QVariantList out;
    for (int i = 0; i < macros_.size(); ++i) {
        const Macro &m = macros_[i];
        QVariantMap e;
        e["index"] = i;
        e["name"]  = m.name;
        e["text"]  = m.text;
        e["fkey"]  = m.fkey;
        e["user"]  = m.user;
        e["preview"] = expand(m.text);     // token-substituted, for the dim line
        out.append(e);
    }
    return out;
}

QString CwMacroModel::expand(const QString &text) const {
    QString s = text;
    const QString myCall = prefs_ ? prefs_->callsign().toUpper() : QString();
    s.replace(QStringLiteral("{MYCALL}"), myCall,            Qt::CaseInsensitive);
    s.replace(QStringLiteral("{CALL}"),   hisCall_.toUpper(),Qt::CaseInsensitive);
    s.replace(QStringLiteral("{RST}"),    rst_,              Qt::CaseInsensitive);
    s.replace(QStringLiteral("{NAME}"),   opName_,           Qt::CaseInsensitive);
    s.replace(QStringLiteral("{#}"),      QString::number(serial_), Qt::CaseInsensitive);
    // {LOG} is an ACTION token, not text — strip it so it's never keyed as
    // morse. The send path (sendIndex) detects it in the raw macro and emits
    // logQsoRequested() separately. Stripped here too so the macro preview
    // shows only what actually gets sent.
    s.replace(QStringLiteral("{LOG}"), QString(), Qt::CaseInsensitive);
    // Personal tokens last — the 5 built-in names above already won.
    for (const Token &t : tokens_) {
        if (t.name.isEmpty()) continue;
        s.replace(QLatin1Char('{') + t.name + QLatin1Char('}'), t.value, Qt::CaseInsensitive);
    }
    return s;
}

void CwMacroModel::startSending(int i, const QString &expanded) {
    sendingIndex_ = i;
    emit sendingChanged();
    // Estimate the keyed burst length so the highlight/progress clears itself:
    // ~PARIS, a char averages ~10 dot-units, unit = 1200/wpm ms.
    double wpm = stream_ ? stream_->cwKeyerSpeedWpm() : 25.0;
    if (wpm < 5.0) wpm = 25.0;
    const int ms = static_cast<int>(expanded.trimmed().length() * 10.0 * (1200.0 / wpm)) + 250;
    sendTimer_->start(ms);
}

void CwMacroModel::sendIndex(int i) {
    if (i < 0 || i >= macros_.size()) return;
    // Detect the {LOG} action token in the RAW macro (expand() strips it).
    const bool wantLog =
        macros_[i].text.contains(QStringLiteral("{LOG}"), Qt::CaseInsensitive);
    const QString expanded = expand(macros_[i].text);
    // Key the CW if there's anything left after stripping {LOG} — a macro can
    // be pure "{LOG}" (a log-only button) with no morse to send.
    if (!expanded.trimmed().isEmpty()) {
        if (stream_) stream_->sendCw(expanded);
        startSending(i, expanded);
    }
    if (wantLog) emit logQsoRequested();
}

void CwMacroModel::sendByFkey(int fn) {
    for (int i = 0; i < macros_.size(); ++i)
        if (macros_[i].fkey == fn) { sendIndex(i); return; }
}

void CwMacroModel::stop() {
    if (stream_) stream_->abortCw();
    sendTimer_->stop();
    if (sendingIndex_ != -1) { sendingIndex_ = -1; emit sendingChanged(); }
}

void CwMacroModel::bumpSerial(int delta) { setSerial(serial_ + delta); }

void CwMacroModel::setSerial(int n) {
    n = std::max(1, n);
    if (n == serial_) return;
    serial_ = n;
    QSettings().setValue(kSerialKey, serial_);
    emit serialChanged();
    emit macrosChanged();   // {#} previews update
}

void CwMacroModel::setTypeAhead(bool on) {
    if (on == typeAhead_) return;
    typeAhead_ = on;
    QSettings().setValue(kTypeAheadKey, typeAhead_);
    emit typeAheadChanged();
}

void CwMacroModel::setHisCall(const QString &s) {
    if (s == hisCall_) return; hisCall_ = s; emit contactChanged(); emit macrosChanged();
}
void CwMacroModel::setRst(const QString &s) {
    if (s == rst_) return; rst_ = s; emit contactChanged(); emit macrosChanged();
}
void CwMacroModel::setOpName(const QString &s) {
    if (s == opName_) return; opName_ = s; emit contactChanged(); emit macrosChanged();
}

void CwMacroModel::setMacro(int i, const QString &name, const QString &text) {
    if (i < 0 || i >= macros_.size()) return;
    macros_[i].name = name;
    macros_[i].text = text;
    save();
    emit macrosChanged();
}

void CwMacroModel::addMacro(const QString &name, const QString &text) {
    int nextF = 1;                          // assign the lowest free F-key (1..12)
    for (; nextF <= 12; ++nextF) {
        bool taken = false;
        for (const Macro &m : macros_) if (m.fkey == nextF) { taken = true; break; }
        if (!taken) break;
    }
    macros_.push_back({name.isEmpty() ? QStringLiteral("Macro") : name, text,
                       nextF <= 12 ? nextF : 0, true});
    save();
    emit macrosChanged();
}

void CwMacroModel::removeMacro(int i) {
    if (i < 0 || i >= macros_.size()) return;
    macros_.remove(i);
    save();
    emit macrosChanged();
}

} // namespace lyra::ui
