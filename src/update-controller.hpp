#pragma once

#include "update-policy.hpp"
#include <QObject>
#include <QPointer>
#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSettings;
class QTimer;
class QWidget;

namespace fx {
class UpdateController : public QObject {
public:
    explicit UpdateController(QObject *parent = nullptr);
    ~UpdateController() override;
    QWidget *createWidget(QWidget *parent);
    void start();
    void shutdown();

private:
    using DownloadResult = std::function<void(const QByteArray &, const QString &)>;
    void check();
    void inspectRelease(const update::Offer &offer);
    void stage(bool automatic);
    void cancelStage();
    void setStatus(const QString &text);
    void updateButtons();
    void fetch(const QUrl &url, qint64 maximum, const QString &destination,
               const QString &expectedDigest, qint64 expectedSize,
               DownloadResult complete, int redirects = 0, QUrl original = {});
    bool launchHelper();

    QSettings *m_settings;
    QNetworkAccessManager *m_network;
    QTimer *m_timer;
    QPointer<QNetworkReply> m_reply;
    QPointer<QLabel> m_statusLabel;
    QPointer<QPushButton> m_checkButton;
    QPointer<QPushButton> m_installButton;
    QPointer<QPushButton> m_cancelButton;
    QString m_status;
    QString m_repository;
    QString m_stageDirectory;
    QString m_stagedDigest;
    QString m_obsRoot;
    std::optional<update::Offer> m_offer;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_stopping = false;
    bool m_started = false;
    bool m_staged = false;
    bool m_stageAutomatic = false;
};
} // namespace fx
