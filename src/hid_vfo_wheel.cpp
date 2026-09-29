#include "hid_vfo_wheel.h"

#include "hl2_stream.h"
#include "prefs.h"
#include "wdsp_engine.h"

#include <QApplication>
#include <QByteArray>
#include <QEvent>
#include <QRegularExpression>
#include <QVector>
#include <QWheelEvent>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace lyra::ui {

namespace {

HidVfoWheel *g_hidVfo = nullptr;

#ifdef Q_OS_WIN
QString hidDeviceName(HANDLE hDevice)
{
    UINT n = 0;
    GetRawInputDeviceInfo(hDevice, RIDI_DEVICENAME, nullptr, &n);
    if (n == 0)
        return {};
    QVector<wchar_t> buf;
    buf.resize(int(n));
    if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICENAME, buf.data(), &n)
            == (UINT)-1)
        return {};
    return QString::fromWCharArray(buf.constData());
}

QString hidVid(const QString &path)
{
    static const QRegularExpression re(
        QStringLiteral("VID_([0-9A-Fa-f]{4})"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(path);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

QString hidPid(const QString &path)
{
    static const QRegularExpression re(
        QStringLiteral("PID_([0-9A-Fa-f]{4})"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(path);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

QString hidLabel(const QString &path)
{
    const QString v = hidVid(path);
    const QString p = hidPid(path);
    QString shortName = path;
    const int slash = shortName.lastIndexOf(QLatin1Char('\\'));
    if (slash >= 0)
        shortName = shortName.mid(slash + 1);
    if (shortName.size() > 48)
        shortName = shortName.left(48) + QLatin1String("…");
    if (!v.isEmpty() && !p.isEmpty())
        return QStringLiteral("VID_%1 PID_%2  %3").arg(v, p, shortName);
    return shortName.isEmpty() ? path : shortName;
}

bool hidIdsMatch(const QString &saved, const QString &got)
{
    if (saved.isEmpty() || got.isEmpty())
        return false;
    if (saved.compare(got, Qt::CaseInsensitive) == 0)
        return true;
    const QString v1 = hidVid(saved), v2 = hidVid(got);
    const QString p1 = hidPid(saved), p2 = hidPid(got);
    return !v1.isEmpty() && v1 == v2 && p1 == p2;
}
#endif

} // namespace

HidVfoWheel *HidVfoWheel::instance()
{
    return g_hidVfo;
}

HidVfoWheel::HidVfoWheel(QObject *parent)
    : QObject(parent)
{
    g_hidVfo = this;
    lastMatch_.invalidate();
}

HidVfoWheel::~HidVfoWheel()
{
#ifdef Q_OS_WIN
    if (registered_ && hwnd_) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.dwFlags     = RIDEV_REMOVE;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        registered_ = false;
    }
#endif
    if (g_hidVfo == this)
        g_hidVfo = nullptr;
}

void HidVfoWheel::bind(QWidget *host, Prefs *prefs, lyra::ipc::HL2Stream *stream,
                       lyra::dsp::WdspEngine *wdsp)
{
    prefs_ = prefs;
    stream_ = stream;
    wdsp_ = wdsp;
    hwnd_ = host ? quintptr(host->window()->winId()) : 0;
    qApp->installNativeEventFilter(this);
    qApp->installEventFilter(this);
    connect(prefs_, &Prefs::hidVfoWheelEnabledChanged,
            this, &HidVfoWheel::reregister);
    connect(prefs_, &Prefs::hidVfoListenUnfocusedChanged,
            this, &HidVfoWheel::reregister);
    reregister();
}

void HidVfoWheel::reregister()
{
#ifdef Q_OS_WIN
    if (!hwnd_)
        return;
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0x02;   // generic desktop mouse
    rid.hwndTarget  = reinterpret_cast<HWND>(hwnd_);
    if (registered_) {
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        registered_ = false;
    }
    rid.hwndTarget = reinterpret_cast<HWND>(hwnd_);
    rid.dwFlags = 0;
    if (prefs_ && prefs_->hidVfoListenUnfocused())
        rid.dwFlags |= RIDEV_INPUTSINK;
    if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
        registered_ = true;
#else
    Q_UNUSED(hwnd_);
#endif
}

bool HidVfoWheel::idsEquivalent(const QString &saved, const QString &got)
{
#ifdef Q_OS_WIN
    return hidIdsMatch(saved, got);
#else
    Q_UNUSED(saved);
    Q_UNUSED(got);
    return false;
#endif
}

QVector<HidMouseEntry> HidVfoWheel::enumerateMice()
{
    QVector<HidMouseEntry> out;
#ifdef Q_OS_WIN
    UINT n = 0;
    GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST));
    if (n == 0)
        return out;
    QVector<RAWINPUTDEVICELIST> list;
    list.resize(int(n));
    if (GetRawInputDeviceList(list.data(), &n, sizeof(RAWINPUTDEVICELIST))
            == (UINT)-1)
        return out;
    for (UINT i = 0; i < n; ++i) {
        if (list[i].dwType != RIM_TYPEMOUSE)
            continue;
        const QString path = hidDeviceName(list[i].hDevice);
        if (path.isEmpty())
            continue;
        bool dup = false;
        for (const auto &e : out) {
            if (e.id.compare(path, Qt::CaseInsensitive) == 0) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        out.push_back({path, hidLabel(path)});
    }
#endif
    return out;
}

bool HidVfoWheel::deviceMatches(const QString &rawName) const
{
#ifdef Q_OS_WIN
    if (!prefs_)
        return false;
    return idsEquivalent(prefs_->hidVfoDeviceId(), rawName);
#else
    Q_UNUSED(rawName);
    return false;
#endif
}

QString HidVfoWheel::deviceNameFor(void *hDevice) const
{
#ifdef Q_OS_WIN
    return hidDeviceName(HANDLE(hDevice));
#else
    Q_UNUSED(hDevice);
    return {};
#endif
}

bool HidVfoWheel::nativeEventFilter(const QByteArray &eventType, void *message,
                                    qintptr *result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    auto *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_INPUT)
        return false;
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT,
                        &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return false;
    if (raw.header.dwType != RIM_TYPEMOUSE)
        return false;
    if ((raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) == 0)
        return false;
    const QString name = hidDeviceName(raw.header.hDevice);
    if (!deviceMatches(name))
        return false;
    const auto delta = static_cast<SHORT>(raw.data.mouse.usButtonData);
    int ticks = int(delta) / 120;
    if (ticks == 0)
        ticks = (delta > 0) ? 1 : ((delta < 0) ? -1 : 0);
    if (ticks == 0)
        return false;
    lastMatch_.restart();
    emit wheelHeard();
    if (prefs_ && prefs_->hidVfoWheelEnabled())
        applyTicks(ticks);
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}

bool HidVfoWheel::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (!prefs_ || !prefs_->hidVfoWheelOnly())
        return false;
    if (event->type() != QEvent::Wheel)
        return false;
    if (!lastMatch_.isValid() || lastMatch_.elapsed() > 50)
        return false;
    event->accept();
    return true;
}

void HidVfoWheel::applyTicks(int ticks)
{
    if (!stream_ || !prefs_ || ticks == 0)
        return;
    const bool rx2 = stream_->focusedRx() == 2 && stream_->subEnabled();
    const bool vfoB = stream_->focusedRx() == 2 && stream_->splitEnabled()
                      && !stream_->subEnabled();
    const QString mode = rx2 ? prefs_->modeRx2() : prefs_->mode();
    const int step = rx2 ? prefs_->vfoStepHzRx2(mode) : prefs_->vfoStepHz(mode);
    if (step <= 0)
        return;
    const int off = wdsp_
        ? (rx2 ? wdsp_->markerOffsetHzRx2() : wdsp_->markerOffsetHz())
        : 0;
    quint32 dds = stream_->rx1FreqHz();
    if (rx2)
        dds = stream_->rx2FreqHz();
    else if (vfoB)
        dds = stream_->vfoBHz();
    qint64 next = qint64(dds) + qint64(off) + qint64(ticks) * qint64(step)
                  - qint64(off);
    if (next < 0)
        next = 0;
    if (next > 2147483647LL)
        next = 2147483647LL;
    const auto hz = static_cast<quint32>(next);
    if (rx2)
        stream_->setRx2FreqHz(hz);
    else if (vfoB)
        stream_->setVfoBHz(hz);
    else
        stream_->setRx1FreqHz(hz);
}

} // namespace lyra::ui
