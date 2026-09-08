// Protocol-neutral live RX front-end controls for QML.

#pragma once

#include <QObject>
#include <QString>

namespace lyra::ipc { class HL2Stream; }
namespace lyra::wire { class P2RxBridge; }

namespace lyra::hardware {

class ActiveFrontEndModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool p2Active READ p2Active NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY changed)
    Q_PROPERTY(int minimum READ minimum NOTIFY changed)
    Q_PROPERTY(int maximum READ maximum NOTIFY changed)
    Q_PROPERTY(bool autoAvailable READ autoAvailable NOTIFY changed)
    Q_PROPERTY(bool autoEnabled READ autoEnabled WRITE setAutoEnabled NOTIFY changed)
    Q_PROPERTY(int overloadTier READ overloadTier NOTIFY changed)
    Q_PROPERTY(int overloadMask READ overloadMask NOTIFY changed)
    Q_PROPERTY(int adcPeak READ adcPeak NOTIFY changed)
    Q_PROPERTY(int adcIndex READ adcIndex WRITE setAdcIndex NOTIFY changed)

public:
    ActiveFrontEndModel(lyra::ipc::HL2Stream *p1,
                        lyra::wire::P2RxBridge *p2,
                        QObject *parent = nullptr);

    bool p2Active() const;
    QString label() const;
    int value() const;
    int minimum() const;
    int maximum() const;
    bool autoAvailable() const;
    bool autoEnabled() const;
    int overloadTier() const;
    int overloadMask() const;
    int adcPeak() const;
    int adcIndex() const;

public slots:
    void setValue(int value);
    void setAutoEnabled(bool on);
    void setAdcIndex(int index);

signals:
    void changed();

private:
    lyra::ipc::HL2Stream *p1_ = nullptr;
    lyra::wire::P2RxBridge *p2_ = nullptr;
};

} // namespace lyra::hardware
