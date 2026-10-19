// Lyra-to-Lyra control link (slice 1).
//
// The shack listens on its Tailscale address only after Allow desk.
// The desk connects to that name. Receiver audio is a second socket.
// Spectrum is a third socket. Frequency and mode ride the control
// socket. MOX, Tune, and 2-tone ride it after the Remote password
// proof. PureSignal arm is a host request; the shack runs the math.
// No public bind and no TCI address lists. See
// docs/architecture/lyra_to_lyra.md.

#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class QJsonObject;
class QTcpServer;
class QTcpSocket;
class QTimer;
class QProcess;
class QAudioSink;
class QAudioSource;
class QIODevice;

namespace lyra::ui {

class DeskLink : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool deskSide READ deskSide NOTIFY changed)
    Q_PROPERTY(int micGainDb READ micGainDb WRITE setMicGainDb
               NOTIFY micGainDbChanged)
    Q_PROPERTY(bool voiceHeld READ voiceHeld NOTIFY changed)
    Q_PROPERTY(bool moxLit READ moxLit NOTIFY changed)
    Q_PROPERTY(bool tuneLit READ tuneLit NOTIFY changed)
    Q_PROPERTY(bool twoToneLit READ twoToneLit NOTIFY changed)
    Q_PROPERTY(bool deskOwnsKey READ deskOwnsKey NOTIFY changed)
public:
    static constexpr quint16 kPort = 40111;
    static constexpr quint16 kAudioPort = 40112;
    static constexpr quint16 kSpecPort = 40113;

    explicit DeskLink(QObject *parent = nullptr);
    ~DeskLink() override;

    QString selfName() const { return selfName_; }
    QString selfAddress() const { return selfAddr_; }
    bool    tailscaleUp() const { return tailscaleUp_; }
    bool    allowDesk() const { return allow_; }
    int     hostProtocol() const { return hostProto_; }
    QString hostRigLabel() const { return hostRig_; }
    bool    attached() const { return peer_ != nullptr; }
    bool    isHost() const { return weHost_; }
    bool    attaching() const { return attaching_; }
    QString savedHostName() const { return savedHost_; }
    QString peerName() const { return peerName_; }
    int     rttMs() const { return rttMs_; }
    QString lamp() const { return lamp_; }
    bool    deskSide() const { return peer_ != nullptr && !weHost_; }
    bool    voiceHeld() const { return voiceHeld_; }
    bool    moxLit() const { return moxLit_; }
    bool    tuneLit() const { return tuneLit_; }
    bool    twoToneLit() const { return twoToneLit_; }
    bool    deskOwnsKey() const { return deskOwnsKey_; }
    bool    keying() const { return keying_; }

    // Empty when <name> may be tried. A public address, a house
    // address, a blank, or a name with a port is refused.
    static QString nameProblem(const QString &name);
    static bool    isTailscale4(const QString &ip);

    void setAllowDesk(bool on);
    void dropDesk();
    void attachToHost(const QString &name);
    void detach();
    // RX worker thread. Pre-volume stereo doubles at 48 kHz. Copies and returns.
    void offerRxAudio(const double *lr, int nframes);
    // Desk speaker level. Slider position 0..1, same taper as the audio panel.
    void setPlayPos(double pos);
    void setPlayMuted(bool muted);
    void sendTune(quint32 hz);
    void sendMode(const QString &mode);
    void sendBandwidth(int hz);
    void sendRit(bool on, int offsetHz);
    void sendSplit(bool on);
    void sendAgc(const QString &mode);
    void sendNr(bool on);
    void sendNnr(bool on);
    void sendNb(bool on, double strength);
    void sendBin(bool on, double depth);
    void sendAnf(bool on);
    void sendLms(bool on, double strength);
    void sendApf(bool on, double gainDb);
    void sendTxBandwidth(int hz);
    void sendTxLow(int hz);
    void sendRxLow(int hz);
    void sendDeskProfile(const QByteArray &json);
    void sendPs(int live, int fb, int att, int correcting, int cal, int fsm,
                int d0, int d1, int spr);
    void sendPsPlot(const QByteArray &raw);
    Q_INVOKABLE void requestRemotePsReset();
    Q_INVOKABLE void requestRemotePsArm(bool on);
    void sendRig(int protocol, const QString &label);
    void sendFrontEnd(int value, bool autoOn, int adc);
    void sendAntenna(bool alex, int count, int mask, int trx, int rx,
                     const QString &ext1);
    void sendBandAntenna(const QString &band, int ant, int rx);
    void sendSub(bool on);
    void sendRx2(quint32 hz);
    void sendSquelch(bool on, double threshold);
    void sendDrive(int level);
    void sendTuneDrive(int mode, int tunePct, int fixedPct);
    void sendMeters(double dbm, double fwdW, double revW);
    void sendMox(bool on, bool deskOwns);
    void sendTuneOn(bool on);
    void sendTwoToneOn(bool on);
    // Desk. The password stays in memory until the proof is sent, then it is cleared.
    Q_INVOKABLE void stageVoicePassword(const QString &password);
    Q_INVOKABLE void askVoice();
    Q_INVOKABLE void endVoice();
    Q_INVOKABLE void requestRemoteMox(bool on);
    Q_INVOKABLE void requestRemoteTune(bool on);
    Q_INVOKABLE void requestRemoteTwoTone(bool on);
    // Desk TCI digital app (JTDX / MSHV / Open SSTV). PTT is a separate
    // line from the laptop-mic MOX so the Mic slider is not applied.
    // Host confirms with dig 1 once the radio is keyed.
    Q_INVOKABLE void requestRemoteDigital(bool on);
    bool digitalKey() const { return digitalKey_; }
    // Host only. True from the dig request until the radio drops it.
    void setDigitalWanted(bool on) { digitalWanted_ = on; }
    bool digitalWanted() const { return digitalWanted_; }
    void sendDigitalState(bool on);
    // Pre-volume mono the desk is already playing. 48 kHz int16.
    void setRxTap(std::function<void(const qint16 *mono, int nframes)> tap);
    // MIC1 with no laptop Mic-slider gain. Dropped unless the host
    // has confirmed this digital key.
    void sendDigitalMono(const float *mono, int nframes);
    // Host replies. The token stays on this socket and is not sent.
    void sendVoiceChallenge(const QByteArray &nonce, const QByteArray &salt, int iters);
    void sendVoiceDeny(const QString &reason);
    void sendVoiceHold(const QString &word, int seconds);
    void noteVoiceGrant();
    void dropVoiceHold();
    void noteVoiceFail();
    QByteArray takeVoiceNonce();
    bool voiceGateOpen(QString *word, int *seconds) const;
    void setKeying(bool on);
    void setMicSink(std::function<void(const qint16 *pcm, int nframes)> sink);
    // Laptop microphone level, applied on this PC before MIC1 is packed.
    // The TX panel Mic slider drives this while deskSide is true.
    // 0 dB is unity. Stored only here, under deskRemote/micGainDb.
    // Range matches the TX panel Mic slider.
    static constexpr int kMicGainMinDb = -90;
    static constexpr int kMicGainMaxDb = 40;
    static constexpr int kMicGainDefaultDb = 18;
    int  micGainDb() const { return micGainDb_; }
    Q_INVOKABLE void setMicGainDb(int db);
    // Host spectrum read. Fills dst with full-span dB, writes center and span.
    void setSpectrumSource(std::function<int(float *dst, int maxN,
                                              int *centerHz, int *spanHz)> src);
    const float *spectrumDb() const { return specDb_.data(); }
    int spectrumCount() const { return specCount_; }

signals:
    void changed();
    void micGainDbChanged(int db);
    void statusMessage(const QString &text);
    void peerReady();
    void remoteFreq(quint32 hz);
    void remoteMode(const QString &mode);
    void remoteBandwidth(int hz);
    void remoteRit(bool on, int offsetHz);
    void remoteSplit(bool on);
    void remoteAgc(const QString &mode);
    void remoteNr(bool on);
    void remoteNnr(bool on);
    void remoteNb(bool on, double strength);
    void remoteBin(bool on, double depth);
    void remoteAnf(bool on);
    void remoteLms(bool on, double strength);
    void remoteApf(bool on, double gainDb);
    void remoteTxBandwidth(int hz);
    void remoteTxLow(int hz);
    void remoteRxLow(int hz);
    void remoteDeskProfile(const QByteArray &json);
    void remotePs(int live, int fb, int att, int correcting, int cal, int fsm,
                  int d0, int d1, int spr);
    void remotePsPlot(const QByteArray &raw);
    void remotePsReset();
    void remotePsArm(bool on);
    void sessionEnded(bool wasHost);
    void remoteRig(int protocol, const QString &label);
    void remoteFrontEnd(int value, bool autoOn, int adc);
    void remoteAntenna(bool alex, int count, int mask, int trx, int rx,
                       const QString &ext1);
    void remoteBandAntenna(const QString &band, int ant, int rx);
    void remoteSub(bool on);
    void remoteRx2(quint32 hz);
    void remoteSquelch(bool on, double threshold);
    void remoteDrive(int level);
    void remoteTuneDrive(int mode, int tunePct, int fixedPct);
    void remoteMeters(double dbm, double fwdW, double revW);
    void voiceRequested();
    void voiceProof(const QByteArray &mac);
    void voiceEnded();
    void remoteMox(bool on);
    void remoteTune(bool on);
    void remoteTwoTone(bool on);
    void remoteDigital(bool on);
    // Host confirmed or cleared the digital key. False also means a
    // local refuse (voice not held, or the link is gone).
    void digitalState(bool on);
    void forceUnkey();
    void metersTick();
    void spectrumFrame(int fullSpanHz);

private:
    void probe();
    void readProbe();
    void tryListen();
    void onNewConnection();
    void onAudioConnection();
    void onAudioRead();
    void onSpecConnection();
    void onSpecRead();
    void flushHostAudio();
    void flushHostSpectrum();
    void ensureDeskSpec();
    void startDeskPlay();
    void stopDeskPlay();
    void pushPlay(const qint16 *mono, int nframes);
    void feedDeskSpeaker();
    void onSocketRead();
    void onSocketClosed();
    void pump();
    void takeSocket(QTcpSocket *sock, bool hosting);
    void closePeer(const QString &why);
    void noteLine(const QByteArray &line);
    void sendLine(const QByteArray &line);
    void emitProfileObject(const QJsonObject &o);
    void emitBandedProfile(const QString &key, const QJsonObject &obj);
    void setLamp(const QString &text);
    void publish();
    void applyRtt(int ms);
    void noteLinkMode(const QString &mode);
    void ensureDeskAudio();
    void armAudioClient();
    void onHostMic();
    void startDeskMic();
    void stopDeskMic();
    void sendMicChunk();
    QString audioClause() const;

    QString selfName_;
    QString selfAddr_;
    bool    tailscaleUp_ = false;
    bool    allow_       = false;
    int     hostProto_   = 0;
    QString hostRig_;
    bool    hosting_     = false;
    bool    attaching_   = false;
    QString savedHost_;
    QString peerName_;
    QString lamp_;
    int     rttMs_       = -1;
    int     rttPeakMs_   = -1;
    qint64  rttPeakUntilMs_ = 0;
    QString linkMode_;
    int     lookupGen_   = 0;
    quint32 pingSeq_     = 0;
    qint64  pingAtMs_    = -1;
    qint64  lastRxMs_    = -1;

    QTcpServer *server_  = nullptr;
    QTcpServer *audioServer_ = nullptr;
    QTcpServer *specServer_ = nullptr;
    QTcpSocket *peer_    = nullptr;
    QTcpSocket *audioPeer_ = nullptr;
    QTcpSocket *specPeer_ = nullptr;
    QTimer     *probeTimer_ = nullptr;
    QTimer     *pumpTimer_  = nullptr;
    QTimer     *audioTimer_ = nullptr;
    QTimer     *specTimer_ = nullptr;
    QProcess   *probeProc_  = nullptr;
    QByteArray  rxBuf_;
    QByteArray  audioRx_;
    QByteArray  micRx_;
    QByteArray  specRx_;
    bool        voiceHeld_ = false;
    bool        moxLit_ = false;
    bool        tuneLit_ = false;
    bool        twoToneLit_ = false;
    bool        deskOwnsKey_ = false;
    bool        keying_ = false;
    bool        digitalKey_ = false;
    bool        digitalWanted_ = false;
    std::function<void(const qint16 *, int)> rxTap_;
    QString     voicePassword_;
    int         voiceProofGen_ = 0;
    QByteArray  voiceNonce_;
    int         voiceFails_ = 0;
    qint64      voiceWaitMs_ = 0;
    qint64      voiceLockMs_ = 0;
    std::function<void(const qint16 *, int)> micSink_;
    QAudioSource *micSrc_ = nullptr;
    QIODevice    *micDev_ = nullptr;
    int   micGainDb_ = kMicGainDefaultDb;
    float micGainLinear_ = 7.943282f;
    bool        weHost_  = false;
    std::atomic<bool> sendAudio_{false};
    std::atomic<bool> sendSpec_{false};
    std::atomic<bool> audioUp_{false};
    std::atomic<qint64> lastLocalAudioMs_{0};
    std::atomic<qint64> lastPlayMs_{0};
    std::atomic<int> lastPeak_{0};
    std::atomic<double> playGain_{0.089};
    std::atomic<bool> playMuted_{false};
    qint64 audioRetryAt_ = 0;
    qint64 audioConnectAt_ = 0;
    QString playBlock_;
    bool audioListenLogged_ = false;
    bool specListenLogged_ = false;
    qint64 specRetryAt_ = 0;
    qint64 specConnectAt_ = 0;
    std::function<int(float *, int, int *, int *)> specSrc_;
    std::vector<float> specDb_;
    int specCount_ = 0;
    int meterDiv_ = 0;

    std::vector<qint16> hostRing_;
    int hostRd_ = 0;
    int hostCount_ = 0;
    mutable std::mutex hostMtx_;

    std::vector<qint16> playRing_;
    int playRd_ = 0;
    int playCount_ = 0;
    int playPrime_ = 3840;
    bool playReady_ = false;
    std::mutex playMtx_;
    QAudioSink *playSink_ = nullptr;
    QIODevice  *playDev_  = nullptr;
};

} // namespace lyra::ui
