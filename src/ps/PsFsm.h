// PureSignal host FSM: SetPS* + GetPSInfo poll + auto-att.
// Host never calls SetTXAiqc* (deskHPSDR never does).

#pragma once

#include <QObject>
#include <QTimer>
#include <functional>

namespace lyra::ps {

class PsFsm : public QObject {
    Q_OBJECT
public:
    explicit PsFsm(QObject *parent = nullptr);

    void setAttnWriter(std::function<void(int)> fn);
    void setFeedbackRateHz(int hz);

    void setArmed(bool on);
    void setMox(bool on);
    void reset();

    int  feedbackLevel() const { return feedbackLevel_; }
    int  fsmState() const { return fsmState_; }
    bool correcting() const { return correcting_; }
    int  calCount() const { return calCount_; }
    int  ddc0Dbfs() const { return ddc0Dbfs_; }
    int  ddc1Dbfs() const { return ddc1Dbfs_; }
    int  feedSpr() const { return feedSpr_; }

signals:
    void telemetryChanged();

private:
    void onTick();
    void pushArm();
    void pushDisarm();

    QTimer timer_;
    std::function<void(int)> attnWriter_;
    int  feedbackRateHz_ = 192000;
    bool armed_ = false;
    bool mox_ = false;
    int  feedbackLevel_ = 0;
    int  fsmState_ = 0;
    int  calCount_ = 0;
    bool correcting_ = false;
    int  lastAttDb_ = 0;
    int  ddc0Dbfs_ = -999;
    int  ddc1Dbfs_ = -999;
    int  feedSpr_ = 0;
};

}  // namespace lyra::ps
