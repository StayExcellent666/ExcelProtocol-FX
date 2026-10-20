#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <optional>

namespace fx::update {
constexpr qint64 MaximumInstallerBytes = 250LL * 1024 * 1024;
constexpr qint64 MaximumManifestBytes = 1024 * 1024;
constexpr qint64 MaximumApiBytes = 4LL * 1024 * 1024;

// Supported releases are strict SemVer core versions or -beta.N prereleases.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int beta = -1;
};
std::optional<Version> parseVersion(const QString &value);
int compare(const Version &a, const Version &b);
bool validRepository(const QString &repository);
bool safeRedirect(const QUrl &original, const QUrl &destination);

struct Asset {
    QString name;
    QUrl url;
    QString sha256;
    qint64 size = 0;
};
struct Offer {
    QString version;
    QString tag;
    QString channel;
    QUrl releaseUrl;
    Asset installer;
    Asset manifest;
};

// Fail closed on missing digests, oversized files, renamed repositories and
// inconsistent release metadata. No URL or command is inferred from free text.
std::optional<Offer> releaseOffer(const QJsonObject &release,
                                  const QString &repository,
                                  const QString &currentVersion,
                                  const QString &channel,
                                  QString *error = nullptr);
bool validateManifest(const QJsonObject &manifest, const Offer &offer,
                      const QString &repository, const QString &obsVersion,
                      QString *error = nullptr);
} // namespace fx::update
