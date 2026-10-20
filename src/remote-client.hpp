#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QSslSocket;
class QTimer;

namespace fx {
class EffectManager;

class RemoteClient final : public QObject {
public:
    enum class State {
        Unpaired,
        Pairing,
        Connecting,
        Handshaking,
        Connected,
        WaitingToReconnect,
        AuthenticationFailed,
        Error,
        Stopped,
    };

    explicit RemoteClient(EffectManager &manager, QObject *parent = nullptr);
    ~RemoteClient() override;

    void start();
    void shutdown();
    void pair(const QString &pairingCode, const QString &deviceName);
    void reconnectNow();
    void forgetDevice();

    State state() const { return state_; }
    QString statusText() const;
    QString deviceId() const { return deviceId_; }
    bool paired() const { return !deviceId_.isEmpty() && !credential_.isEmpty(); }

    std::function<void()> onChanged;
    std::function<void(bool, const QString &)> onPairingFinished;

private:
    void setState(State state, const QString &detail = {});
    void connectSocket();
    void resetSocket();
    void sendUpgradeRequest();
    void consumeSocketData();
    bool consumeUpgradeResponse();
    bool consumeFrames();
    void handleTextMessage(const QByteArray &payload);
    void handleWelcome(const QJsonObject &message);
    void handleCommand(const QJsonObject &message);
    void sendJson(const QJsonObject &message);
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void sendClose(quint16 code, const QString &reason);
    void scheduleReconnect(const QString &reason);
    void heartbeat();
    QJsonObject normalizeResponse(const QJsonObject &command, QJsonObject response) const;

    EffectManager &manager_;
    QSettings &settings_;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *pairReply_ = nullptr;
    QSslSocket *socket_ = nullptr;
    QTimer *heartbeatTimer_ = nullptr;
    QTimer *reconnectTimer_ = nullptr;
    QTimer *phaseTimer_ = nullptr;
    QTimer *stabilityTimer_ = nullptr;
    State state_ = State::Unpaired;
    QString detail_;
    QString deviceId_;
    QByteArray credential_;
    QUrl endpoint_;
    QByteArray socketBuffer_;
    QByteArray fragmentBuffer_;
    QByteArray websocketKey_;
    quint8 fragmentOpcode_ = 0;
    int reconnectAttempt_ = 0;
    bool upgraded_ = false;
    bool welcomed_ = false;
    bool shuttingDown_ = false;
    bool authenticationBlocked_ = false;
};
} // namespace fx
