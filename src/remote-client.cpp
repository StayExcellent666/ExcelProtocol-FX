#include "remote-client.hpp"

#include "effects.hpp"

#include <obs-module.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <limits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

#ifndef FX_VERSION
#define FX_VERSION "1.0.0"
#endif

namespace fx {
namespace {
constexpr int maxMessageBytes = 16 * 1024;
constexpr auto credentialPrefix = L"ExcelProtocol/FX/";
constexpr auto protocolName = "excelprotocol-fx";

QString credentialTarget(const QString &deviceId)
{
    return QString::fromWCharArray(credentialPrefix) + deviceId;
}

bool storeCredential(const QString &deviceId, const QByteArray &secret)
{
    if (deviceId.isEmpty() || secret.isEmpty()) return false;
    const std::wstring target = credentialTarget(deviceId).toStdWString();
    std::wstring user = L"ExcelProtocol FX";
    CREDENTIALW value{};
    value.Type = CRED_TYPE_GENERIC;
    value.TargetName = const_cast<wchar_t *>(target.c_str());
    value.CredentialBlobSize = static_cast<DWORD>(secret.size());
    value.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.constData()));
    value.Persist = CRED_PERSIST_LOCAL_MACHINE;
    value.UserName = user.data();
    return CredWriteW(&value, 0) == TRUE;
}

QByteArray loadCredential(const QString &deviceId)
{
    if (deviceId.isEmpty()) return {};
    const std::wstring target = credentialTarget(deviceId).toStdWString();
    PCREDENTIALW value = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &value) || !value) return {};
    QByteArray result(reinterpret_cast<const char *>(value->CredentialBlob),
                      static_cast<qsizetype>(value->CredentialBlobSize));
    CredFree(value);
    return result;
}

void deleteCredential(const QString &deviceId)
{
    if (deviceId.isEmpty()) return;
    const std::wstring target = credentialTarget(deviceId).toStdWString();
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
}

bool protocolFieldsValid(const QJsonObject &object)
{
    return object.value("protocol") == protocolName && object.value("protocol_version").toInt() == 1;
}

QByteArray isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8();
}

QDateTime parseIsoDate(QString value)
{
    // Python emits six fractional digits while Qt stores milliseconds. Trim
    // only the excess precision; the represented instant is unchanged here.
    const qsizetype dot = value.indexOf('.');
    if (dot >= 0) {
        qsizetype zone = value.indexOf('Z', dot);
        const qsizetype plus = value.indexOf('+', dot);
        const qsizetype minus = value.indexOf('-', dot);
        if (zone < 0 || (plus >= 0 && plus < zone)) zone = plus;
        if (zone < 0 || (minus >= 0 && minus < zone)) zone = minus;
        if (zone > dot + 4) value.remove(dot + 4, zone - (dot + 4));
    }
    QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(value, Qt::ISODate);
    return parsed;
}

QString safeHttpError(int status)
{
    switch (status) {
    case 400: return "The pairing code is invalid, expired, or already used.";
    case 401: return "Pairing was not authorized.";
    case 403: return "Pairing was rejected by the server.";
    case 409: return "That pairing code was already used.";
    case 429: return "Too many pairing attempts. Wait a minute and try again.";
    default: return QString("Pairing failed (HTTP %1).").arg(status);
    }
}

quint16 read16(const QByteArray &data, qsizetype offset)
{
    return (static_cast<quint8>(data[offset]) << 8) | static_cast<quint8>(data[offset + 1]);
}

quint64 read64(const QByteArray &data, qsizetype offset)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | static_cast<quint8>(data[offset + i]);
    return value;
}
} // namespace

RemoteClient::RemoteClient(EffectManager &manager, QObject *parent)
    : QObject(parent), manager_(manager), settings_(manager.settings())
{
    network_ = new QNetworkAccessManager(this);
    heartbeatTimer_ = new QTimer(this);
    heartbeatTimer_->setInterval(30000);
    QObject::connect(heartbeatTimer_, &QTimer::timeout, this, [this] { heartbeat(); });
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    QObject::connect(reconnectTimer_, &QTimer::timeout, this, [this] { connectSocket(); });
    phaseTimer_ = new QTimer(this);
    phaseTimer_->setSingleShot(true);
    QObject::connect(phaseTimer_, &QTimer::timeout, this, [this] {
        if (!socket_ || welcomed_) return;
        detail_ = "Secure connection timed out - reconnecting automatically";
        socket_->abort();
    });
    stabilityTimer_ = new QTimer(this);
    stabilityTimer_->setSingleShot(true);
    QObject::connect(stabilityTimer_, &QTimer::timeout, this, [this] {
        if (state_ == State::Connected) reconnectAttempt_ = 0;
    });
}

RemoteClient::~RemoteClient() { shutdown(); }

void RemoteClient::start()
{
    if (shuttingDown_) return;
    deviceId_ = settings_.value("connection/deviceId").toString().trimmed();
    endpoint_ = QUrl(settings_.value("connection/wssEndpoint", "wss://excelprotocol.fly.dev/api/fx/ws").toString());
    credential_ = loadCredential(deviceId_);
    if (!paired()) {
        if (!deviceId_.isEmpty()) settings_.remove("connection/deviceId");
        deviceId_.clear();
        setState(State::Unpaired);
        return;
    }
    connectSocket();
}

void RemoteClient::shutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;
    heartbeatTimer_->stop();
    reconnectTimer_->stop();
    phaseTimer_->stop();
    stabilityTimer_->stop();
    if (pairReply_) { pairReply_->abort(); pairReply_ = nullptr; }
    if (socket_) {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    credential_.fill('\0');
    credential_.clear();
    setState(State::Stopped);
}

void RemoteClient::pair(const QString &pairingCode, const QString &deviceName)
{
    if (shuttingDown_ || pairReply_) return;
    QString code = pairingCode.trimmed().toUpper();
    code.remove('-'); code.remove(' ');
    if (code.size() != 8) {
        if (onPairingFinished) onPairingFinished(false, "Enter the 8-character code from the dashboard.");
        return;
    }
    code.insert(4, '-');
    const QString name = deviceName.trimmed().isEmpty()
        ? QString("OBS on %1").arg(QHostInfo::localHostName()).left(64)
        : deviceName.trimmed().left(64);
    QJsonObject body{
        {"pairing_code", code}, {"device_name", name},
        {"plugin_version", FX_VERSION}, {"obs_version", QString::fromUtf8(obs_get_version_string())},
    };
    QByteArray encoded = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkRequest request(QUrl("https://excelprotocol.fly.dev/api/fx/pair"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QString("ExcelProtocol-FX/%1").arg(FX_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    setState(State::Pairing);
    pairReply_ = network_->post(request, encoded);
    encoded.fill('\0');
    QObject::connect(pairReply_, &QNetworkReply::readyRead, this, [this] {
        if (pairReply_ && pairReply_->bytesAvailable() > maxMessageBytes) pairReply_->abort();
    });
    QObject::connect(pairReply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = pairReply_;
        pairReply_ = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray bytes = reply->readAll();
        const auto networkError = reply->error();
        reply->deleteLater();
        if (shuttingDown_) { bytes.fill('\0'); return; }
        if (networkError != QNetworkReply::NoError || status != 201 || bytes.size() > maxMessageBytes) {
            bytes.fill('\0');
            const QString message = status ? safeHttpError(status) : "Could not reach ExcelProtocol over HTTPS.";
            setState(paired() ? State::Error : State::Unpaired, message);
            if (onPairingFinished) onPairingFinished(false, message);
            return;
        }
        QJsonParseError error{};
        const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &error);
        bytes.fill('\0');
        const QJsonObject object = parsed.object();
        const QString newDeviceId = object.value("device_id").toString();
        QByteArray newCredential = object.value("device_credential").toString().toUtf8();
        const QUrl newEndpoint(object.value("wss_endpoint").toString());
        const QUuid uuid(newDeviceId);
        if (error.error != QJsonParseError::NoError || uuid.isNull() || newCredential.isEmpty()
            || !newEndpoint.isValid() || newEndpoint.scheme() != "wss" || newEndpoint.host().isEmpty()) {
            newCredential.fill('\0');
            const QString message = "The server returned an invalid pairing response.";
            setState(paired() ? State::Error : State::Unpaired, message);
            if (onPairingFinished) onPairingFinished(false, message);
            return;
        }
        if (!storeCredential(newDeviceId, newCredential)) {
            newCredential.fill('\0');
            const QString message = "Windows Credential Manager could not save the device credential.";
            setState(paired() ? State::Error : State::Unpaired, message);
            if (onPairingFinished) onPairingFinished(false, message);
            return;
        }
        const QString oldDeviceId = deviceId_;
        if (!oldDeviceId.isEmpty() && oldDeviceId != newDeviceId) deleteCredential(oldDeviceId);
        credential_.fill('\0');
        credential_ = newCredential;
        newCredential.fill('\0');
        deviceId_ = newDeviceId;
        endpoint_ = newEndpoint;
        settings_.setValue("connection/deviceId", deviceId_);
        settings_.setValue("connection/wssEndpoint", endpoint_.toString(QUrl::FullyEncoded));
        settings_.sync();
        authenticationBlocked_ = false;
        reconnectAttempt_ = 0;
        if (onPairingFinished) onPairingFinished(true, "Paired. Connecting to ExcelProtocol...");
        reconnectNow();
    });
}

void RemoteClient::reconnectNow()
{
    if (shuttingDown_ || !paired()) return;
    authenticationBlocked_ = false;
    reconnectTimer_->stop();
    resetSocket();
    connectSocket();
}

void RemoteClient::forgetDevice()
{
    reconnectTimer_->stop();
    heartbeatTimer_->stop();
    phaseTimer_->stop();
    stabilityTimer_->stop();
    resetSocket();
    deleteCredential(deviceId_);
    credential_.fill('\0'); credential_.clear();
    deviceId_.clear(); endpoint_.clear();
    settings_.remove("connection/deviceId");
    settings_.remove("connection/wssEndpoint");
    settings_.sync();
    authenticationBlocked_ = false;
    reconnectAttempt_ = 0;
    setState(State::Unpaired);
}

QString RemoteClient::statusText() const
{
    if (!detail_.isEmpty()) return detail_;
    switch (state_) {
    case State::Unpaired: return "Not paired";
    case State::Pairing: return "Pairing securely...";
    case State::Connecting: return "Connecting to ExcelProtocol...";
    case State::Handshaking: return "Authenticating device...";
    case State::Connected: return "Connected - reward effects are ready";
    case State::WaitingToReconnect: return "Connection lost - reconnecting automatically";
    case State::AuthenticationFailed: return "Authorization expired or was revoked. Pair this device again.";
    case State::Error: return "Connection error";
    case State::Stopped: return "Stopped";
    }
    return "Unknown";
}

void RemoteClient::setState(State state, const QString &detail)
{
    state_ = state;
    detail_ = detail;
    if (onChanged) onChanged();
}

void RemoteClient::connectSocket()
{
    if (shuttingDown_ || authenticationBlocked_ || !paired()) return;
    if (!endpoint_.isValid() || endpoint_.scheme() != "wss" || endpoint_.host().isEmpty()) {
        setState(State::Error, "The saved server address is invalid. Pair this device again.");
        return;
    }
    resetSocket();
    socket_ = new QSslSocket(this);
    QObject::connect(socket_, &QSslSocket::encrypted, this, [this] { sendUpgradeRequest(); });
    QObject::connect(socket_, &QSslSocket::readyRead, this, [this] { consumeSocketData(); });
    QObject::connect(socket_, &QSslSocket::disconnected, this, [this] {
        if (!shuttingDown_ && !authenticationBlocked_) scheduleReconnect("Connection lost - reconnecting automatically");
    });
    QObject::connect(socket_, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!shuttingDown_ && socket_) detail_ = "Secure connection failed: " + socket_->errorString();
    });
    QObject::connect(socket_, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) {
        if (!socket_) return;
        detail_ = "The server TLS certificate could not be verified.";
        socket_->abort();
    });
    setState(State::Connecting);
    phaseTimer_->start(15000);
    socket_->connectToHostEncrypted(endpoint_.host(), static_cast<quint16>(endpoint_.port(443)));
}

void RemoteClient::resetSocket()
{
    heartbeatTimer_->stop();
    phaseTimer_->stop();
    stabilityTimer_->stop();
    upgraded_ = false; welcomed_ = false;
    socketBuffer_.clear(); fragmentBuffer_.clear(); fragmentOpcode_ = 0; websocketKey_.clear();
    if (!socket_) return;
    socket_->disconnect(this);
    socket_->abort();
    socket_->deleteLater();
    socket_ = nullptr;
}

void RemoteClient::sendUpgradeRequest()
{
    if (!socket_ || credential_.isEmpty()) return;
    QByteArray nonce(16, Qt::Uninitialized);
    for (qsizetype i = 0; i < nonce.size(); ++i) nonce[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    websocketKey_ = nonce.toBase64();
    QString path = endpoint_.path(QUrl::FullyEncoded);
    if (path.isEmpty()) path = "/";
    if (endpoint_.hasQuery()) path += "?" + endpoint_.query(QUrl::FullyEncoded);
    QByteArray host = endpoint_.host(QUrl::FullyEncoded).toUtf8();
    if (endpoint_.port(443) != 443) host += ':' + QByteArray::number(endpoint_.port());
    QByteArray request;
    request += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n";
    request += "Sec-WebSocket-Key: " + websocketKey_ + "\r\n";
    request += "Authorization: Bearer " + credential_ + "\r\n";
    request += "User-Agent: ExcelProtocol-FX/" FX_VERSION "\r\n\r\n";
    socket_->write(request);
    request.fill('\0');
    setState(State::Handshaking);
    phaseTimer_->start(10000);
}

void RemoteClient::consumeSocketData()
{
    if (!socket_) return;
    socketBuffer_ += socket_->readAll();
    if (socketBuffer_.size() > maxMessageBytes + 8192 && !upgraded_) {
        authenticationBlocked_ = true;
        setState(State::AuthenticationFailed, "The server returned an invalid WebSocket response.");
        socket_->abort();
        return;
    }
    if (!upgraded_ && !consumeUpgradeResponse()) return;
    if (upgraded_) consumeFrames();
}

bool RemoteClient::consumeUpgradeResponse()
{
    const qsizetype end = socketBuffer_.indexOf("\r\n\r\n");
    if (end < 0) return false;
    const QByteArray headers = socketBuffer_.left(end + 4);
    socketBuffer_.remove(0, end + 4);
    const QList<QByteArray> lines = headers.split('\n');
    const QByteArray statusLine = lines.value(0).trimmed();
    if (!statusLine.startsWith("HTTP/1.1 101 ") && statusLine != "HTTP/1.1 101") {
        const bool authFailure = statusLine.contains(" 401 ") || statusLine.contains(" 403 ");
        authenticationBlocked_ = authFailure;
        setState(authFailure ? State::AuthenticationFailed : State::Error,
                 authFailure ? "Authorization expired or was revoked. Pair this device again."
                             : "The server rejected the WebSocket connection.");
        if (socket_) socket_->abort();
        return false;
    }
    QByteArray accept;
    bool upgrade = false, connection = false;
    for (const QByteArray &rawLine : lines) {
        const qsizetype colon = rawLine.indexOf(':');
        if (colon < 0) continue;
        const QByteArray name = rawLine.left(colon).trimmed().toLower();
        const QByteArray value = rawLine.mid(colon + 1).trimmed();
        if (name == "sec-websocket-accept") accept = value;
        if (name == "upgrade" && value.toLower() == "websocket") upgrade = true;
        if (name == "connection" && value.toLower().contains("upgrade")) connection = true;
    }
    const QByteArray expected = QCryptographicHash::hash(
        websocketKey_ + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
    if (!upgrade || !connection || accept != expected) {
        authenticationBlocked_ = true;
        setState(State::AuthenticationFailed, "The server WebSocket identity check failed.");
        if (socket_) socket_->abort();
        return false;
    }
    upgraded_ = true;
    QJsonArray capabilities{"commands.v1", "effects.list.v1", "effects.status.v1"};
    sendJson({
        {"protocol", protocolName}, {"protocol_version", 1}, {"type", "hello"},
        {"device_id", deviceId_}, {"plugin_version", FX_VERSION},
        {"obs_version", QString::fromUtf8(obs_get_version_string())}, {"capabilities", capabilities},
    });
    return true;
}

bool RemoteClient::consumeFrames()
{
    while (true) {
        if (socketBuffer_.size() < 2) return true;
        const quint8 first = static_cast<quint8>(socketBuffer_[0]);
        const quint8 second = static_cast<quint8>(socketBuffer_[1]);
        const bool fin = (first & 0x80) != 0;
        const quint8 opcode = first & 0x0f;
        const bool control = opcode >= 0x08;
        if ((first & 0x70) || (second & 0x80) || (control && !fin)) {
            sendClose(1002, "Invalid frame"); return false;
        }
        quint64 length = second & 0x7f;
        qsizetype header = 2;
        if (length == 126) {
            if (socketBuffer_.size() < 4) return true;
            length = read16(socketBuffer_, 2); header = 4;
        } else if (length == 127) {
            if (socketBuffer_.size() < 10) return true;
            length = read64(socketBuffer_, 2); header = 10;
        }
        if ((control && length > 125) || length > maxMessageBytes
            || length > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
            sendClose(1009, "Message too large"); return false;
        }
        if (socketBuffer_.size() < header + static_cast<qsizetype>(length)) return true;
        QByteArray payload = socketBuffer_.mid(header, static_cast<qsizetype>(length));
        socketBuffer_.remove(0, header + static_cast<qsizetype>(length));
        if (opcode == 0x8) {
            if (payload.size() == 1) { sendClose(1002, "Invalid close frame"); return false; }
            const quint16 closeCode = payload.size() >= 2 ? read16(payload, 0) : 1005;
            if (closeCode == 1008) {
                authenticationBlocked_ = true;
                setState(State::AuthenticationFailed,
                         "The server rejected this device session. Reconnect after checking for an update, or pair again.");
            }
            if (socket_) { sendFrame(0x8, payload.left(125)); socket_->disconnectFromHost(); }
            return false;
        }
        if (opcode == 0x9) { sendFrame(0xA, payload); continue; }
        if (opcode == 0xA) continue;
        if (opcode == 0x2 || (opcode != 0x0 && opcode != 0x1)) {
            sendClose(1003, "Text JSON only"); return false;
        }
        if (opcode == 0x1) {
            if (fragmentOpcode_ != 0) { sendClose(1002, "Unexpected data frame"); return false; }
            if (fin) handleTextMessage(payload);
            else { fragmentOpcode_ = opcode; fragmentBuffer_ = payload; }
        } else {
            if (fragmentOpcode_ != 0x1) { sendClose(1002, "Unexpected continuation"); return false; }
            fragmentBuffer_ += payload;
            if (fragmentBuffer_.size() > maxMessageBytes) { sendClose(1009, "Message too large"); return false; }
            if (fin) {
                QByteArray complete = fragmentBuffer_;
                fragmentBuffer_.clear(); fragmentOpcode_ = 0;
                handleTextMessage(complete);
            }
        }
        if (!socket_ || socket_->state() == QAbstractSocket::UnconnectedState) return false;
    }
}

void RemoteClient::handleTextMessage(const QByteArray &payload)
{
    QJsonParseError error{};
    const QJsonDocument parsed = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) {
        sendClose(1008, "Malformed JSON"); return;
    }
    const QJsonObject object = parsed.object();
    if (!protocolFieldsValid(object)) { sendClose(1008, "Protocol mismatch"); return; }
    const QString type = object.value("type").toString();
    if (type == "welcome") { handleWelcome(object); return; }
    if (type == "heartbeat_ack") return;
    if (!welcomed_) { sendClose(1008, "Welcome required"); return; }
    handleCommand(object);
}

void RemoteClient::handleWelcome(const QJsonObject &message)
{
    if (welcomed_ || message.value("device_id").toString() != deviceId_) {
        sendClose(1008, "Invalid welcome"); return;
    }
    const int interval = std::clamp(message.value("heartbeat_seconds").toInt(30), 10, 90);
    const int serverMax = message.value("max_message_bytes").toInt(maxMessageBytes);
    if (serverMax <= 0 || serverMax > maxMessageBytes) { sendClose(1008, "Invalid limits"); return; }
    welcomed_ = true;
    phaseTimer_->stop();
    stabilityTimer_->start(30000);
    heartbeatTimer_->start(interval * 1000);
    setState(State::Connected);
    blog(LOG_INFO, "[ExcelProtocol FX] Secure reward connection ready");
}

void RemoteClient::handleCommand(const QJsonObject &message)
{
    const QStringList messageKeys = message.keys();
    const QSet<QString> keys(messageKeys.cbegin(), messageKeys.cend());
    const QSet<QString> expected{"protocol", "protocol_version", "request_id", "expires_at", "command"};
    const QString requestId = message.value("request_id").toString();
    if (keys != expected || requestId.isEmpty() || requestId.size() > 128 || !message.value("command").isObject()) {
        sendClose(1008, "Invalid command envelope"); return;
    }
    QJsonObject response;
    const QDateTime expires = parseIsoDate(message.value("expires_at").toString());
    if (!expires.isValid() || expires.toUTC() <= QDateTime::currentDateTimeUtc()) {
        response = {{"ok", false}, {"status", "error"}, {"error", "expired_command"}};
    } else {
        const QJsonObject command = message.value("command").toObject();
        response = normalizeResponse(command, manager_.command(command));
    }
    sendJson({
        {"protocol", protocolName}, {"protocol_version", 1},
        {"request_id", requestId}, {"response", response},
    });
}

QJsonObject RemoteClient::normalizeResponse(const QJsonObject &command, QJsonObject response) const
{
    const QString type = command.value("type").toString();
    if (!response.contains("status")) response.insert("status", response.value("ok").toBool() ? "ok" : "error");
    if (type == "get_active_effects") {
        const QJsonValue active = response.take("active");
        response = {{"ok", true}, {"status", "ok"}, {"active_effects", active.isArray() ? active : QJsonArray{}}};
    } else if (type == "get_status") {
        response = {
            {"ok", response.value("ok").toBool()}, {"status", response.value("status").toString("ok")},
            {"enabled", response.value("enabled").toBool()},
            {"queue_size", response.value("queued_count").toInt()},
        };
    }
    return response;
}

void RemoteClient::sendJson(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > maxMessageBytes) { sendClose(1009, "Message too large"); return; }
    sendFrame(0x1, payload);
}

void RemoteClient::sendFrame(quint8 opcode, const QByteArray &payload)
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) return;
    QByteArray frame;
    frame.reserve(payload.size() + 8);
    frame.append(static_cast<char>(0x80 | (opcode & 0x0f)));
    if (payload.size() <= 125) {
        frame.append(static_cast<char>(0x80 | payload.size()));
    } else {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((payload.size() >> 8) & 0xff));
        frame.append(static_cast<char>(payload.size() & 0xff));
    }
    QByteArray mask(4, Qt::Uninitialized);
    const quint32 random = QRandomGenerator::global()->generate();
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<char>((random >> (i * 8)) & 0xff);
    frame += mask;
    const qsizetype payloadStart = frame.size();
    frame += payload;
    for (qsizetype i = 0; i < payload.size(); ++i) frame[payloadStart + i] = payload[i] ^ mask[i % 4];
    socket_->write(frame);
}

void RemoteClient::sendClose(quint16 code, const QString &reason)
{
    QByteArray payload;
    payload.append(static_cast<char>((code >> 8) & 0xff));
    payload.append(static_cast<char>(code & 0xff));
    payload += reason.toUtf8().left(123);
    sendFrame(0x8, payload);
    if (socket_) socket_->disconnectFromHost();
}

void RemoteClient::scheduleReconnect(const QString &reason)
{
    heartbeatTimer_->stop();
    if (shuttingDown_ || authenticationBlocked_ || !paired() || reconnectTimer_->isActive()) return;
    const int exponent = std::min(reconnectAttempt_, 6);
    const int ceilingMs = std::min(60000, 1000 * (1 << exponent));
    const int delayMs = QRandomGenerator::global()->bounded(ceilingMs + 1);
    ++reconnectAttempt_;
    setState(State::WaitingToReconnect, QString("%1 (retrying in %2.%3 s)")
        .arg(reason).arg(delayMs / 1000).arg((delayMs % 1000) / 100));
    reconnectTimer_->start(delayMs);
}

void RemoteClient::heartbeat()
{
    if (!welcomed_) return;
    sendJson({
        {"protocol", protocolName}, {"protocol_version", 1},
        {"type", "heartbeat"}, {"sent_at", QString::fromUtf8(isoNow())},
    });
}
} // namespace fx
