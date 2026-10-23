#include "update-controller.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <obs.h>
#include <obs-frontend-api.h>
#include <memory>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef FX_VERSION
#define FX_VERSION "1.0.0"
#endif
#ifndef FX_GITHUB_REPOSITORY
#define FX_GITHUB_REPOSITORY ""
#endif

namespace fx {
namespace {
struct Transfer {
    QByteArray bytes;
    std::unique_ptr<QSaveFile> file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 received = 0;
    QString error;
};
QString channel(QSettings *settings)
{
    return settings->value(QStringLiteral("updates/channel"), QStringLiteral("stable")).toString() == QStringLiteral("beta")
               ? QStringLiteral("beta") : QStringLiteral("stable");
}
QJsonDocument readJson(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    return error.error == QJsonParseError::NoError ? document : QJsonDocument{};
}
#ifdef _WIN32
// Windows paths cannot contain quotes; quoted arguments never reach a shell.
std::wstring quote(const QString &value)
{
    std::wstring output = L"\"";
    size_t slashes = 0;
    for (wchar_t character : value.toStdWString()) {
        if (character == L'\\') {
            ++slashes;
        } else {
            output.append(slashes * (character == L'\"' ? 2 : 1), L'\\');
            slashes = 0;
            if (character == L'\"')
                output += L'\\';
            output += character;
        }
    }
    output.append(slashes * 2, L'\\');
    return output + L'\"';
}
#endif
} // namespace

UpdateController::UpdateController(QObject *parent)
    : QObject(parent),
      m_settings(new QSettings(QStringLiteral("ExcelProtocol"), QStringLiteral("ExcelProtocol-FX"), this)),
      m_network(new QNetworkAccessManager(this)), m_timer(new QTimer(this)),
      m_repository(QString::fromLatin1(FX_GITHUB_REPOSITORY))
{
    m_timer->setInterval(24 * 60 * 60 * 1000);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_settings->value(QStringLiteral("updates/autoCheck"), true).toBool())
            check();
    });
    m_status = update::validRepository(m_repository)
        ? tr("Installed version: %1. Updates have not been checked.").arg(QString::fromLatin1(FX_VERSION))
        : tr("Updates unavailable: this build has no official GitHub repository.");
}

UpdateController::~UpdateController()
{
    shutdown();
}

QWidget *UpdateController::createWidget(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    auto *layout = new QVBoxLayout(widget);
    auto *check = new QCheckBox(tr("Automatically Check for Updates"), widget);
    check->setChecked(m_settings->value(QStringLiteral("updates/autoCheck"), true).toBool());
    auto *install = new QCheckBox(tr("Automatically Download and Install Updates"), widget);
    install->setChecked(m_settings->value(QStringLiteral("updates/autoInstall"), true).toBool());
    auto *explanation = new QLabel(tr("Updates are verified before installation. When one is ready, choose whether to restart OBS now or install after you close it later. Windows may request administrator permission."), widget);
    explanation->setWordWrap(true);
    auto *channels = new QComboBox(widget);
    channels->addItem(tr("Stable releases"), QStringLiteral("stable"));
    channels->addItem(tr("Stable and beta releases"), QStringLiteral("beta"));
    channels->setCurrentIndex(channel(m_settings) == QStringLiteral("beta") ? 1 : 0);
    auto *row = new QHBoxLayout;
    m_checkButton = new QPushButton(tr("Check for Updates"), widget);
    m_installButton = new QPushButton(tr("Download Update"), widget);
    m_cancelButton = new QPushButton(tr("Cancel Queued Update"), widget);
    row->addWidget(m_checkButton);
    row->addWidget(m_installButton);
    row->addWidget(m_cancelButton);
    m_statusLabel = new QLabel(m_status, widget);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(check);
    layout->addWidget(install);
    layout->addWidget(explanation);
    layout->addWidget(channels);
    layout->addLayout(row);
    layout->addWidget(m_statusLabel);
    layout->addStretch();
    connect(check, &QCheckBox::toggled, this, [this](bool enabled) {
        m_settings->setValue(QStringLiteral("updates/autoCheck"), enabled);
        m_settings->sync();
    });
    connect(install, &QCheckBox::toggled, this, [this](bool enabled) {
        m_settings->setValue(QStringLiteral("updates/autoInstall"), enabled);
        m_settings->sync();
        if (!enabled && m_stageAutomatic)
            cancelStage();
        if (enabled && m_offer && !m_busy && !m_staged)
            stage(true);
    });
    connect(channels, &QComboBox::currentIndexChanged, this, [this, channels] {
        m_settings->setValue(QStringLiteral("updates/channel"), channels->currentData().toString());
        m_settings->sync();
        cancelStage();
        m_offer.reset();
        setStatus(tr("Release channel changed. Check for updates to continue."));
        updateButtons();
    });
    connect(m_checkButton, &QPushButton::clicked, this, [this] { this->check(); });
    connect(m_installButton, &QPushButton::clicked, this, [this] {
        if (m_staged) {
            m_restartPromptShown = false;
            promptRestart();
        } else {
            stage(false);
        }
    });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { cancelStage(); });
    const bool configured = update::validRepository(m_repository);
    check->setEnabled(configured);
    install->setEnabled(configured);
    channels->setEnabled(configured);
    updateButtons();
    return widget;
}

void UpdateController::start()
{
    if (m_started || m_stopping)
        return;
    m_started = true;
    const QString previous = m_settings->value(QStringLiteral("updates/resultFile")).toString();
    if (!previous.isEmpty()) {
        QFile result(previous);
        if (result.open(QIODevice::ReadOnly) && result.size() <= 16 * 1024) {
            setStatus(tr("Previous update: %1").arg(QString::fromUtf8(result.readAll()).trimmed()));
            m_settings->remove(QStringLiteral("updates/resultFile"));
        }
    }
    if (!update::validRepository(m_repository))
        return;
    m_timer->start();
    QTimer::singleShot(20000, this, [this] {
        if (!m_stopping && m_settings->value(QStringLiteral("updates/autoCheck"), true).toBool())
            check();
    });
}

void UpdateController::setStatus(const QString &text)
{
    m_status = text;
    if (m_statusLabel)
        m_statusLabel->setText(text);
}

void UpdateController::updateButtons()
{
    if (m_checkButton)
        m_checkButton->setEnabled(!m_busy && !m_staged && !m_stopping && update::validRepository(m_repository));
    if (m_installButton)
        m_installButton->setEnabled(m_staged || (m_offer.has_value() && !m_busy && !m_stopping));
    if (m_installButton)
        m_installButton->setText(m_staged ? tr("Restart OBS to Install") : tr("Download Update"));
    if (m_cancelButton)
        m_cancelButton->setEnabled(!m_stageDirectory.isEmpty() && !m_stopping);
}

void UpdateController::fetch(const QUrl &url, qint64 maximum, const QString &destination,
                             const QString &expectedDigest, qint64 expectedSize,
                             DownloadResult complete, int redirects, QUrl original)
{
    if (original.isEmpty())
        original = url;
    if (redirects > 4 || !update::safeRedirect(original, url)) {
        complete({}, tr("The download was redirected outside the allowed GitHub hosts."));
        return;
    }
    const quint64 generation = m_generation;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setRawHeader("User-Agent", "ExcelProtocol-FX/" FX_VERSION);
    request.setRawHeader("Accept", url.host() == QStringLiteral("api.github.com") ? "application/vnd.github+json" : "application/octet-stream");
    if (url.host() == QStringLiteral("api.github.com"))
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(30000);
    auto *reply = m_network->get(request);
    reply->setReadBufferSize(128 * 1024);
    m_reply = reply;
    auto transfer = std::make_shared<Transfer>();
    if (!destination.isEmpty()) {
        transfer->file = std::make_unique<QSaveFile>(destination);
        if (!transfer->file->open(QIODevice::WriteOnly)) {
            reply->abort();
            reply->deleteLater();
            complete({}, tr("Cannot create the update staging file."));
            return;
        }
    }
    auto drain = [reply, transfer, maximum] {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            while (reply->bytesAvailable() > 0) {
                transfer->received += reply->read(64 * 1024).size();
                if (transfer->received > maximum) {
                    transfer->error = QStringLiteral("The download exceeds the allowed size.");
                    reply->abort();
                    break;
                }
            }
            return;
        }
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > maximum) {
            transfer->error = QStringLiteral("The download exceeds the allowed size.");
            reply->abort();
            return;
        }
        while (reply->bytesAvailable() > 0) {
            const auto bytes = reply->read(64 * 1024);
            transfer->received += bytes.size();
            if (transfer->received > maximum) {
                transfer->error = QStringLiteral("The download exceeds the allowed size.");
                reply->abort();
                return;
            }
            transfer->hash.addData(bytes);
            if (transfer->file) {
                if (transfer->file->write(bytes) != bytes.size()) {
                    transfer->error = QStringLiteral("Writing the update download failed.");
                    reply->abort();
                    return;
                }
            } else {
                transfer->bytes.append(bytes);
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, drain);
    // In addition to an idle timeout, bound the total lifetime of each request.
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply, transfer] {
        transfer->error = QStringLiteral("The update request timed out.");
        reply->abort();
    });
    deadline->start(destination.isEmpty() ? 60000 : 10 * 60 * 1000);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, transfer, drain, generation, original, maximum, destination,
             expectedDigest, expectedSize, complete = std::move(complete), redirects] {
        reply->deleteLater();
        if (m_stopping || generation != m_generation)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) {
            const QUrl target = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            if (transfer->file) {
                transfer->file->cancelWriting();
                transfer->file.reset();
            }
            fetch(target, maximum, destination, expectedDigest, expectedSize, complete, redirects + 1, original);
            return;
        }
        drain();
        if (!transfer->error.isEmpty()) {
            complete({}, transfer->error);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            complete({}, status == 404 ? tr("No published release is available from the official repository.")
                                      : tr("GitHub request failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        if ((expectedSize > 0 && transfer->received != expectedSize) ||
            (!expectedDigest.isEmpty() && QString::fromLatin1(transfer->hash.result().toHex()) != expectedDigest)) {
            complete({}, tr("The download failed SHA-256 or size verification. It will not be installed."));
            return;
        }
        if (transfer->file && !transfer->file->commit()) {
            complete({}, tr("Saving the verified update failed."));
            return;
        }
        complete(transfer->bytes, {});
    });
}

void UpdateController::check()
{
    if (m_busy || m_staged || m_stopping || !update::validRepository(m_repository))
        return;
    ++m_generation;
    m_busy = true;
    m_offer.reset();
    updateButtons();
    setStatus(tr("Checking GitHub for updates…"));
    const QString selected = channel(m_settings);
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases%2").arg(
        m_repository, selected == QStringLiteral("stable") ? QStringLiteral("/latest") : QStringLiteral("?per_page=30")));
    fetch(url, update::MaximumApiBytes, {}, {}, 0, [this, selected](const QByteArray &bytes, const QString &error) {
        if (!error.isEmpty()) {
            m_busy = false;
            setStatus(error);
            updateButtons();
            return;
        }
        const auto document = readJson(bytes);
        QString reason = tr("No newer compatible version is available.");
        std::optional<update::Offer> newest;
        if (selected == QStringLiteral("stable") && document.isObject()) {
            newest = update::releaseOffer(document.object(), m_repository, QString::fromLatin1(FX_VERSION), selected, &reason);
        } else if (selected == QStringLiteral("beta") && document.isArray() && document.array().size() <= 30) {
            for (const auto &release : document.array()) {
                const auto candidate = update::releaseOffer(release.toObject(), m_repository, QString::fromLatin1(FX_VERSION), selected);
                if (candidate && (!newest || update::compare(*update::parseVersion(candidate->version), *update::parseVersion(newest->version)) > 0))
                    newest = candidate;
            }
        } else {
            reason = tr("GitHub returned an invalid release response.");
        }
        if (!newest) {
            m_busy = false;
            setStatus(reason);
            updateButtons();
            return;
        }
        inspectRelease(*newest);
    });
}

void UpdateController::inspectRelease(const update::Offer &offer)
{
    fetch(offer.manifest.url, update::MaximumManifestBytes, {}, offer.manifest.sha256, offer.manifest.size,
          [this, offer](const QByteArray &bytes, const QString &downloadError) {
        QString error = downloadError;
        const auto document = readJson(bytes);
        if (error.isEmpty() && (!document.isObject() ||
            !update::validateManifest(document.object(), offer, m_repository, QString::fromUtf8(obs_get_version_string()), &error))) {
            if (error.isEmpty())
                error = tr("The update manifest is invalid.");
        }
        m_busy = false;
        if (!error.isEmpty()) {
            setStatus(error);
        } else {
            m_offer = offer;
            m_restartPromptShown = false;
            setStatus(tr("Version %1 (%2) is available. It can be downloaded and installed automatically.").arg(offer.version, offer.channel));
        }
        updateButtons();
        if (m_offer && m_settings->value(QStringLiteral("updates/autoInstall"), true).toBool())
            stage(true);
    });
}

void UpdateController::stage(bool automatic)
{
    if (!m_offer || m_busy || m_staged || m_stopping)
        return;
    const QString root = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../..")).canonicalPath();
    if (root.isEmpty() || !QFileInfo::exists(root + QStringLiteral("/bin/64bit/obs64.exe"))) {
        setStatus(tr("The OBS installation path could not be verified. Install the update manually."));
        return;
    }
    m_stageDirectory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        QStringLiteral("/ExcelProtocol/ExcelProtocol-FX/updates/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString helper = root + QStringLiteral("/data/obs-plugins/excelprotocol-fx/excelprotocol-fx-updater.exe");
    if (!QDir().mkpath(m_stageDirectory) || !QFile::copy(helper, m_stageDirectory + QStringLiteral("/excelprotocol-fx-updater.exe"))) {
        cancelStage();
        setStatus(tr("The update helper is missing or the staging directory cannot be written. Reinstall the plugin to repair it."));
        return;
    }
    m_obsRoot = root;
    m_stageAutomatic = automatic;
    m_busy = true;
    ++m_generation;
    updateButtons();
    setStatus(tr("Downloading and verifying version %1…").arg(m_offer->version));
    const auto offer = *m_offer;
    fetch(offer.installer.url, update::MaximumInstallerBytes, m_stageDirectory + QStringLiteral("/setup.exe"),
          offer.installer.sha256, offer.installer.size,
          [this, offer](const QByteArray &, const QString &error) {
        m_busy = false;
        if (!error.isEmpty()) {
            cancelStage();
            setStatus(error);
        } else {
            m_stagedDigest = offer.installer.sha256;
            m_staged = true;
            setStatus(tr("Version %1 is verified and ready. Restart OBS now or leave it queued until OBS closes.").arg(offer.version));
            QTimer::singleShot(0, this, [this] { promptRestart(); });
        }
        updateButtons();
    });
}

void UpdateController::promptRestart()
{
    if (!m_staged || m_busy || m_stopping || m_restartPromptShown)
        return;
    m_restartPromptShown = true;
    QMessageBox prompt(m_statusLabel ? m_statusLabel->window() : nullptr);
    prompt.setIcon(QMessageBox::Question);
    prompt.setWindowTitle(tr("ExcelProtocol FX Update Ready"));
    prompt.setText(tr("ExcelProtocol FX %1 is ready to install.").arg(m_offer ? m_offer->version : QString()));
    const bool outputActive = obs_frontend_streaming_active() || obs_frontend_recording_active();
    prompt.setInformativeText(outputActive
        ? tr("OBS is currently streaming or recording. Restarting now will stop the active output. Choose Later to install when you finish.")
        : tr("Restart OBS now to install the verified update automatically, or choose Later to install when OBS closes."));
    auto *restart = prompt.addButton(tr("Restart OBS Now"), QMessageBox::AcceptRole);
    auto *later = prompt.addButton(tr("Later"), QMessageBox::RejectRole);
    restart->setEnabled(!outputActive);
    prompt.setDefaultButton(later);
    prompt.exec();
    if (prompt.clickedButton() != restart) {
        setStatus(tr("Update ready. It will install after OBS closes, or click Restart OBS to Install when ready."));
        updateButtons();
        return;
    }
    if (!launchHelper(true)) {
        m_restartPromptShown = false;
        setStatus(tr("The update helper could not start. The update remains queued for later."));
        updateButtons();
        return;
    }
    m_helperLaunched = true;
    m_staged = false;
    m_offer.reset();
    setStatus(tr("Closing OBS to install the update. OBS will reopen automatically when installation finishes."));
    updateButtons();
    QTimer::singleShot(0, this, [this] {
        auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
        if (!mainWindow || !mainWindow->close())
            setStatus(tr("OBS did not close. The verified update will wait for the next normal shutdown."));
    });
}

void UpdateController::cancelStage()
{
    ++m_generation;
    if (m_reply)
        m_reply->abort();
    m_staged = false;
    m_busy = false;
    m_stageAutomatic = false;
    m_restartPromptShown = false;
    if (!m_stageDirectory.isEmpty()) {
        QFile::remove(m_stageDirectory + QStringLiteral("/setup.exe"));
        QFile::remove(m_stageDirectory + QStringLiteral("/excelprotocol-fx-updater.exe"));
        QDir().rmdir(m_stageDirectory);
    }
    m_stageDirectory.clear();
    m_stagedDigest.clear();
    setStatus(tr("Queued update cancelled."));
    updateButtons();
}

bool UpdateController::launchHelper(bool restartObs)
{
#ifdef _WIN32
    HANDLE waitHandle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId());
    if (!waitHandle)
        return false;
    SIZE_T attributesSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributesSize);
    std::vector<unsigned char> attributes(attributesSize);
    auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributesSize)) {
        CloseHandle(waitHandle);
        return false;
    }
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &waitHandle, sizeof(waitHandle), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(list);
        CloseHandle(waitHandle);
        return false;
    }
    const QString helper = QDir::toNativeSeparators(m_stageDirectory + QStringLiteral("/excelprotocol-fx-updater.exe"));
    const QString installer = QDir::toNativeSeparators(m_stageDirectory + QStringLiteral("/setup.exe"));
    std::wstring command = quote(helper) + L" --wait-handle " +
        std::to_wstring(reinterpret_cast<uintptr_t>(waitHandle)) + L" --parent-pid " + std::to_wstring(GetCurrentProcessId()) +
        L" --installer " + quote(installer) + L" --sha256 " + quote(m_stagedDigest) +
        L" --obs-root " + quote(QDir::toNativeSeparators(m_obsRoot)) +
        L" --restart-obs " + std::wstring(restartObs ? L"1" : L"0");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    const auto nativeHelper = helper.toStdWString();
    const auto directory = QDir::toNativeSeparators(m_stageDirectory).toStdWString();
    const BOOL created = CreateProcessW(nativeHelper.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(list);
    CloseHandle(waitHandle);
    if (!created)
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    m_settings->setValue(QStringLiteral("updates/resultFile"), m_stageDirectory + QStringLiteral("/result.txt"));
    m_settings->sync();
    return true;
#else
    return false;
#endif
}

void UpdateController::shutdown()
{
    if (m_stopping)
        return;
    m_stopping = true;
    m_timer->stop();
    ++m_generation;
    if (m_reply)
        m_reply->abort();
    if (m_staged && !m_helperLaunched && !launchHelper(false))
        blog(LOG_WARNING, "[ExcelProtocol-FX] The update helper could not start; no update was installed.");
}
} // namespace fx
