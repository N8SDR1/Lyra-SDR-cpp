// PureSignal host FSM: SetPS* + GetPSInfo poll + auto-att.
// Host never calls SetTXAiqc* (deskHPSDR never does).

#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <vector>

namespace lyra::ps {

class PsFsm : public QObject {
    Q_OBJECT
public:
    explicit PsFsm(QObject *parent = nullptr);

    void setAttnWriter(std::function<void(int)> fn);
    // HL2 TX step-att: min -28 max 31, MOX seed 31 (ATT-on-TX floor).
    // Brick/Hermes P2 ADC0: min 0 max 31, MOX seed 0 (coupler not muted).
    void setAttnRange(int minDb, int maxDb, int moxSeed);
    void setFeedbackRateHz(int hz);

    void setArmed(bool on);
    void setMox(bool on);
    void reset();
    void setHwPeak(double peak);
    void captureGetPk();
    void setPlotHeld(bool on);
    bool plotHeld() const { return plotHeld_; }

    int  autoAttDb() const { return lastAttDb_; }
    int  feedbackLevel() const { return feedbackLevel_; }
    int  fsmState() const { return fsmState_; }
    bool correcting() const { return correcting_; }
    int  calCount() const { return calCount_; }
    int  ddc0Dbfs() const { return ddc0Dbfs_; }
    int  ddc1Dbfs() const { return ddc1Dbfs_; }
    int  feedSpr() const { return feedSpr_; }
    double hwPeak() const { return hwPeak_; }
    double maxTx() const { return maxTx_; }
    double getPkHold() const { return getPkHold_; }
    const QVariantList &ampMagX() const { return ampMagX_; }
    const QVariantList &ampMagY() const { return ampMagY_; }
    const QVariantList &ampCorrX() const { return ampCorrX_; }
    const QVariantList &ampCorrY() const { return ampCorrY_; }
    QVariantList info() const;

signals:
    void telemetryChanged();

private:
    void onTick();
    void pushArm();
    void pushDisarm();
    void pollAmpPlot();

    QTimer timer_;
    std::function<void(int)> attnWriter_;
    int  attMinDb_ = -28;
    int  attMaxDb_ = 31;
    int  moxSeedAttDb_ = 31;
    int  feedbackRateHz_ = 192000;
    bool armed_ = false;
    bool mox_ = false;
    int  feedbackLevel_ = 0;
    int  fsmState_ = 0;
    int  calCount_ = 0;
    bool correcting_ = false;
    int  lastAttDb_ = 0;
    int  prevCalCount_ = -1;
    int  info_[16] = {};
    int  ddc0Dbfs_ = -999;
    int  ddc1Dbfs_ = -999;
    int  feedSpr_ = 0;
    double hwPeak_ = 0.233;
    double maxTx_ = 0.0;
    double getPkHold_ = 0.0;
    bool plotHeld_ = false;
    std::vector<double> magEwma_;
    std::vector<double> corrEwma_;
    std::vector<double> dispX_;
    std::vector<double> dispYm_;
    std::vector<double> dispYc_;
    std::vector<double> dispYs_;
    std::vector<double> dispCm_;
    std::vector<double> dispCc_;
    std::vector<double> dispCs_;
    std::vector<double> dispXmCor_;
    std::vector<double> dispYmCor_;
    std::vector<double> dispXaCor_;
    std::vector<double> dispYaCor_;
    QVariantList ampMagX_;
    QVariantList ampMagY_;
    QVariantList ampCorrX_;
    QVariantList ampCorrY_;
};

}  // namespace lyra::ps
