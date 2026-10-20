#include "update-policy.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

namespace {
int failures = 0;
void require(bool condition, const char *message)
{
    if (!condition) {
        qCritical() << message;
        ++failures;
    }
}
const QString repo = QStringLiteral("StayExcellent666/ExcelProtocol-FX");
const QString digest(64, QLatin1Char('a'));
QJsonObject asset(const QString &version, const QString &name, int size)
{
    return {{"name", name}, {"state", "uploaded"}, {"digest", "sha256:" + digest}, {"size", size},
            {"browser_download_url", QStringLiteral("https://github.com/%1/releases/download/v%2/%3").arg(repo, version, name)}};
}
QJsonObject release(const QString &version = QStringLiteral("1.1.0"))
{
    return {{"tag_name", "v" + version}, {"draft", false}, {"prerelease", version.contains("-beta.")},
            {"html_url", QStringLiteral("https://github.com/%1/releases/tag/v%2").arg(repo, version)},
            {"assets", QJsonArray{asset(version, "update-manifest.json", 1024),
                                 asset(version, QStringLiteral("ExcelProtocol-FX-%1-windows-x64-setup.exe").arg(version), 4096)}}};
}
QJsonObject manifest(const fx::update::Offer &offer)
{
    return {{"schema_version", 1}, {"version", offer.version}, {"tag", offer.tag}, {"channel", offer.channel},
            {"repository", repo}, {"platform", "windows-x64"}, {"release_url", offer.releaseUrl.toString()},
            {"obs", QJsonObject{{"minimum", "32.2.2"}, {"maximum_exclusive", "33.0.0"}}},
            {"assets", QJsonObject{{"installer", QJsonObject{{"name", offer.installer.name}, {"url", offer.installer.url.toString()},
                                                          {"sha256", offer.installer.sha256}, {"size", 4096}}}}}};
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    using namespace fx::update;
    require(parseVersion("1.0.0").has_value(), "valid stable version rejected");
    require(parseVersion("1.2.0-beta.12").has_value(), "valid beta version rejected");
    for (const auto *invalid : {"1.0", "v1.0.0", "01.0.0", "1.0.0-beta", "1.0.0-beta.01", "1.0.0-rc.1", "1.0.0+evil", "99999999999.0.0", "1.0.0\n"})
        require(!parseVersion(invalid), invalid);
    require(compare(*parseVersion("1.0.0-beta.2"), *parseVersion("1.0.0-beta.11")) < 0, "beta numeric order");
    require(compare(*parseVersion("1.0.0"), *parseVersion("1.0.0-beta.99")) > 0, "stable promotion order");
    require(compare(*parseVersion("1.10.0"), *parseVersion("1.9.99")) > 0, "numeric version order");
    require(validRepository(repo), "official repository rejected");
    require(!validRepository("") && !validRepository("owner/elsewhere") && !validRepository("../ExcelProtocol-FX"), "repository identity validation");
    const QUrl original("https://github.com/StayExcellent666/ExcelProtocol-FX/releases/download/v1.1.0/setup.exe");
    require(safeRedirect(original, QUrl("https://release-assets.githubusercontent.com/signed-asset?token=123")), "GitHub asset redirect rejected");
    for (const auto *url : {"http://release-assets.githubusercontent.com/file", "https://release-assets.githubusercontent.com.evil.example/file", "https://github.com/evil/other", "https://user@release-assets.githubusercontent.com/file", "https://release-assets.githubusercontent.com:444/file"})
        require(!safeRedirect(original, QUrl(url)), url);
    require(!safeRedirect(QUrl("https://evil.example/a"), QUrl("https://evil.example/a")), "foreign initial host allowed");
    require(!safeRedirect(QUrl("https://api.github.com/repos/a/b/releases/latest"), original), "API identity redirect allowed");
    const auto offer = releaseOffer(release(), repo, "1.0.0", "stable");
    require(offer.has_value(), "valid update rejected");
    if (!offer)
        return 1;
    require(!releaseOffer(release(), repo, "1.1.0", "stable"), "same version offered");
    require(!releaseOffer(release(), repo, "2.0.0", "stable"), "downgrade offered");
    require(!releaseOffer(release("1.2.0-beta.1"), repo, "1.0.0", "stable"), "beta offered on stable channel");
    require(releaseOffer(release("1.2.0-beta.1"), repo, "1.0.0", "beta").has_value(), "beta channel rejected beta");
    require(releaseOffer(release(), repo, "1.0.0", "beta").has_value(), "beta channel rejected stable promotion");
    auto badRelease = release();
    badRelease["draft"] = true;
    require(!releaseOffer(badRelease, repo, "1.0.0", "stable"), "draft offered");
    badRelease = release();
    badRelease["prerelease"] = true;
    require(!releaseOffer(badRelease, repo, "1.0.0", "beta"), "prerelease flag mismatch accepted");
    badRelease = release();
    auto assets = badRelease["assets"].toArray();
    auto installer = assets[1].toObject();
    installer.remove("digest");
    assets[1] = installer;
    badRelease["assets"] = assets;
    require(!releaseOffer(badRelease, repo, "1.0.0", "stable"), "missing API digest accepted");
    installer["digest"] = "sha256:" + digest;
    installer["browser_download_url"] = "https://evil.example/setup.exe";
    assets[1] = installer;
    badRelease["assets"] = assets;
    require(!releaseOffer(badRelease, repo, "1.0.0", "stable"), "foreign asset accepted");
    installer = release()["assets"].toArray()[1].toObject();
    installer["size"] = double(MaximumInstallerBytes + 1);
    assets[1] = installer;
    badRelease["assets"] = assets;
    require(!releaseOffer(badRelease, repo, "1.0.0", "stable"), "oversize asset accepted");
    badRelease = release();
    assets = badRelease["assets"].toArray();
    assets.append(assets[1]);
    badRelease["assets"] = assets;
    require(!releaseOffer(badRelease, repo, "1.0.0", "stable"), "duplicate asset accepted");
    auto goodManifest = manifest(*offer);
    require(validateManifest(goodManifest, *offer, repo, "32.2.2"), "valid manifest rejected");
    require(!validateManifest(goodManifest, *offer, repo, "32.2.1"), "unsupported old OBS accepted");
    require(!validateManifest(goodManifest, *offer, repo, "33.0.0"), "exclusive OBS bound ignored");
    require(!validateManifest(goodManifest, *offer, repo, "33.0.0-beta.1"), "untested OBS beta accepted");
    auto badManifest = goodManifest;
    badManifest["schema_version"] = 1.5;
    require(!validateManifest(badManifest, *offer, repo, "32.2.2"), "fractional schema accepted");
    badManifest = goodManifest;
    badManifest["platform"] = "windows-arm64";
    require(!validateManifest(badManifest, *offer, repo, "32.2.2"), "foreign architecture accepted");
    badManifest = goodManifest;
    badManifest["repository"] = "evil/ExcelProtocol-FX";
    require(!validateManifest(badManifest, *offer, repo, "32.2.2"), "manifest repository mismatch accepted");
    badManifest = goodManifest;
    installer = badManifest["assets"].toObject()["installer"].toObject();
    installer["sha256"] = QString(64, QLatin1Char('b'));
    badManifest["assets"] = QJsonObject{{"installer", installer}};
    require(!validateManifest(badManifest, *offer, repo, "32.2.2"), "API/manifest digest mismatch accepted");
    qInfo() << "Update policy failures:" << failures;
    return failures == 0 ? 0 : 1;
}
