#include "update-policy.hpp"

#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace fx::update {
namespace {
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
bool secureUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.userInfo().isEmpty() && url.fragment().isEmpty() &&
           (url.port() == -1 || url.port() == 443);
}
bool validDigest(const QString &digest)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[a-f0-9]{64}\\z"));
    return pattern.match(digest).hasMatch();
}
bool validSize(const QJsonValue &value, qint64 maximum, qint64 *result)
{
    if (!value.isDouble())
        return false;
    const double size = value.toDouble();
    if (!std::isfinite(size) || std::floor(size) != size || size <= 0 || size > double(maximum))
        return false;
    *result = qint64(size);
    return true;
}
QString assetUrl(const QString &repository, const QString &tag, const QString &name)
{
    return QStringLiteral("https://github.com/%1/releases/download/%2/%3").arg(repository, tag, name);
}
bool parseAsset(const QJsonObject &object, const QString &name,
                const QString &repository, const QString &tag,
                qint64 maximum, Asset *asset)
{
    const QString digest = object.value(QStringLiteral("digest")).toString();
    const QString url = object.value(QStringLiteral("browser_download_url")).toString();
    if (object.value(QStringLiteral("name")).toString() != name ||
        object.value(QStringLiteral("state")).toString() != QStringLiteral("uploaded") ||
        !digest.startsWith(QStringLiteral("sha256:")) ||
        !validDigest(digest.mid(7)) || url != assetUrl(repository, tag, name) ||
        !validSize(object.value(QStringLiteral("size")), maximum, &asset->size))
        return false;
    asset->name = name;
    asset->url = QUrl(url, QUrl::StrictMode);
    asset->sha256 = digest.mid(7);
    return true;
}
} // namespace

std::optional<Version> parseVersion(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-beta\\.(0|[1-9][0-9]*))?\\z"));
    const auto match = pattern.match(value);
    if (!match.hasMatch())
        return std::nullopt;
    Version version;
    int *parts[] = {&version.major, &version.minor, &version.patch, &version.beta};
    for (int index = 0; index < 4; ++index) {
        if (index == 3 && match.captured(4).isEmpty())
            continue;
        bool ok = false;
        *parts[index] = match.captured(index + 1).toInt(&ok);
        if (!ok)
            return std::nullopt;
    }
    return version;
}

int compare(const Version &a, const Version &b)
{
    const int left[] = {a.major, a.minor, a.patch};
    const int right[] = {b.major, b.minor, b.patch};
    for (int i = 0; i < 3; ++i)
        if (left[i] != right[i])
            return left[i] < right[i] ? -1 : 1;
    if (a.beta == b.beta)
        return 0;
    if (a.beta == -1)
        return 1;
    if (b.beta == -1)
        return -1;
    return a.beta < b.beta ? -1 : 1;
}

bool validRepository(const QString &repository)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/ExcelProtocol-FX\\z"));
    return pattern.match(repository).hasMatch();
}

bool safeRedirect(const QUrl &original, const QUrl &destination)
{
    if (!secureUrl(original) || !secureUrl(destination))
        return false;
    if (original.host() != QStringLiteral("github.com") && original.host() != QStringLiteral("api.github.com"))
        return false;
    if (destination == original)
        return true;
    // API redirects may change the repository identity. Do not follow them.
    if (original.host() != QStringLiteral("github.com"))
        return false;
    const auto host = destination.host();
    return host == QStringLiteral("release-assets.githubusercontent.com") ||
           host == QStringLiteral("objects.githubusercontent.com") ||
           host == QStringLiteral("github-releases.githubusercontent.com");
}

std::optional<Offer> releaseOffer(const QJsonObject &release,
                                  const QString &repository,
                                  const QString &currentVersion,
                                  const QString &channel, QString *error)
{
    if (!validRepository(repository) || (channel != QStringLiteral("stable") && channel != QStringLiteral("beta"))) {
        fail(error, QStringLiteral("The official update repository or channel is not configured."));
        return std::nullopt;
    }
    const auto current = parseVersion(currentVersion);
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    const auto version = tag.startsWith(QLatin1Char('v')) ? parseVersion(tag.mid(1)) : std::nullopt;
    if (!current || !version || !release.value(QStringLiteral("draft")).isBool() ||
        release.value(QStringLiteral("draft")).toBool() ||
        !release.value(QStringLiteral("prerelease")).isBool() ||
        release.value(QStringLiteral("prerelease")).toBool() != (version->beta >= 0) ||
        (channel == QStringLiteral("stable") && version->beta >= 0)) {
        fail(error, QStringLiteral("The release is not a supported stable or beta version."));
        return std::nullopt;
    }
    if (compare(*version, *current) <= 0) {
        fail(error, QStringLiteral("No newer compatible version is available."));
        return std::nullopt;
    }
    const QString releaseUrl = QStringLiteral("https://github.com/%1/releases/tag/%2").arg(repository, tag);
    if (release.value(QStringLiteral("html_url")).toString() != releaseUrl) {
        fail(error, QStringLiteral("The release does not belong to the official repository."));
        return std::nullopt;
    }
    Offer offer;
    offer.version = tag.mid(1);
    offer.tag = tag;
    offer.channel = version->beta >= 0 ? QStringLiteral("beta") : QStringLiteral("stable");
    offer.releaseUrl = QUrl(releaseUrl);
    const QString installerName = QStringLiteral("ExcelProtocol-FX-%1-windows-x64-setup.exe").arg(offer.version);
    int installers = 0;
    int manifests = 0;
    for (const auto &value : release.value(QStringLiteral("assets")).toArray()) {
        const auto object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (name == installerName) {
            ++installers;
            if (!parseAsset(object, name, repository, tag, MaximumInstallerBytes, &offer.installer)) {
                fail(error, QStringLiteral("The installer lacks valid GitHub SHA-256 integrity metadata."));
                return std::nullopt;
            }
        } else if (name == QStringLiteral("update-manifest.json")) {
            ++manifests;
            if (!parseAsset(object, name, repository, tag, MaximumManifestBytes, &offer.manifest)) {
                fail(error, QStringLiteral("The manifest lacks valid GitHub SHA-256 integrity metadata."));
                return std::nullopt;
            }
        }
    }
    if (installers != 1 || manifests != 1) {
        fail(error, QStringLiteral("The release must contain exactly one installer and update manifest."));
        return std::nullopt;
    }
    return offer;
}

bool validateManifest(const QJsonObject &manifest, const Offer &offer,
                      const QString &repository, const QString &obsVersion,
                      QString *error)
{
    const auto schema = manifest.value(QStringLiteral("schema_version"));
    if (!schema.isDouble() || schema.toDouble() != 1.0 ||
        manifest.value(QStringLiteral("version")).toString() != offer.version ||
        manifest.value(QStringLiteral("tag")).toString() != offer.tag ||
        manifest.value(QStringLiteral("channel")).toString() != offer.channel ||
        manifest.value(QStringLiteral("repository")).toString() != repository ||
        manifest.value(QStringLiteral("release_url")).toString() != offer.releaseUrl.toString() ||
        manifest.value(QStringLiteral("platform")).toString() != QStringLiteral("windows-x64"))
        return fail(error, QStringLiteral("The manifest identity, version, channel or platform is invalid."));
    const auto obs = manifest.value(QStringLiteral("obs")).toObject();
    const auto minimum = parseVersion(obs.value(QStringLiteral("minimum")).toString());
    const auto maximum = parseVersion(obs.value(QStringLiteral("maximum_exclusive")).toString());
    const auto running = parseVersion(obsVersion);
    if (!minimum || !maximum || !running || minimum->beta >= 0 || maximum->beta >= 0 || running->beta >= 0 ||
        compare(*minimum, *maximum) >= 0 || compare(*running, *minimum) < 0 || compare(*running, *maximum) >= 0)
        return fail(error, QStringLiteral("This release does not support the running OBS version."));
    const auto installer = manifest.value(QStringLiteral("assets")).toObject().value(QStringLiteral("installer")).toObject();
    qint64 size = 0;
    if (installer.value(QStringLiteral("name")).toString() != offer.installer.name ||
        installer.value(QStringLiteral("url")).toString() != offer.installer.url.toString() ||
        installer.value(QStringLiteral("sha256")).toString() != offer.installer.sha256 ||
        !validSize(installer.value(QStringLiteral("size")), MaximumInstallerBytes, &size) || size != offer.installer.size)
        return fail(error, QStringLiteral("Installer metadata does not match the GitHub release asset."));
    return true;
}
} // namespace fx::update
