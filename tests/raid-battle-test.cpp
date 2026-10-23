#include "raid-battle.hpp"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QStringList>
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char *message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    fx::RaidBattle battle;
    check(!battle.start(0, 100), "zero-viewer raid rejected");
    check(battle.start(8, 100, false), "valid raid starts");
    QJsonObject status = battle.status(100);
    check(status.value("phase").toString() == "fighting", "started raid is fighting");
    check(status.value("max_hp").toInt() == static_cast<int>(std::floor(1500.0 * std::pow(8.0, 0.7))), "HP follows the 50-percent-harder viewer curve");
    check(!battle.finish(false, 200), "inactive raid cannot be failed before the deadline");
    check(battle.chat("Coder", "viewer", "", 200) == "invalid_raid_chat", "empty chat is not participation");
    check(battle.chat("Coder", "admin", "hello", 200) == "invalid_raid_role", "unknown role rejected");
    check(battle.status(200).value("interaction_count").toInt() == 0, "invalid chat is not participation");
    battle.tick(180100);
    status = battle.status(180100);
    check(status.value("phase").toString() == "results" && !status.value("victory").toBool(), "no-action raid is the only failure");

    battle.clear();
    check(battle.start(5000, 100, false), "large raid starts");
    check(battle.chat("SoloCoder", "viewer", "hello", 200) == "attack_queued", "viewer attack accepted");
    battle.tick(175100);
    status = battle.status(175100);
    check(status.value("phase").toString() == "results" && status.value("victory").toBool(), "any participation guarantees victory");

    battle.clear();
    check(battle.start(34, 0, true), "34-viewer test raid starts");
    status = battle.status(0);
    check(status.value("warrior_count").toInt() == 34, "every raider is represented when the raid fits the field cap");
    check(status.value("enemy_count").toInt() > 8, "viewer count scales enemy variation");
    battle.clear();

    QStringList soundCues;
    battle.onSound = [&soundCues](const QString &name) { soundCues.push_back(name); };
    check(battle.start(12, 0, false), "weapon sound test raid starts");
    soundCues.clear();
    check(battle.chat("HammerMod", "mod", "go", 200) == "attack_queued", "mod attack accepted");
    check(soundCues.contains("raid-shot-hammer"), "Ban Hammer has a dedicated sound cue");
    check(battle.chat("RailVip", "vip", "go", 400) == "attack_queued", "vip attack accepted");
    check(soundCues.contains("raid-shot-railgun"), "Railgun has a dedicated sound cue");
    check(battle.chat("PlasmaSub", "sub", "go", 600) == "attack_queued", "sub attack accepted");
    check(soundCues.contains("raid-shot-plasma"), "Plasma Rifle has a dedicated sound cue");
    check(battle.chat("BlasterViewer", "viewer", "go", 800) == "attack_queued", "viewer attack accepted");
    check(soundCues.contains("raid-shot-blaster"), "Blaster has a dedicated sound cue");
    check(battle.chat("BlasterViewer", "viewer", "again", 1200) == "attack_cooldown", "per-viewer attack spam is cooled down");
    check(battle.chat("OtherViewer", "viewer", "go", 1250) == "attack_queued", "one viewer's cooldown does not block another viewer");
    check(battle.chat("BlasterViewer", "viewer", "again", 1700) == "attack_queued", "viewer can attack after cooldown");
    battle.onSound = {};
    battle.clear();

    QStringList testRaidCues;
    battle.onSound = [&testRaidCues](const QString &name) { testRaidCues.push_back(name); };
    check(battle.start(8, 0, true), "role-varied test raid starts");
    for (qint64 now = 200; now <= 6000; now += 200) battle.tick(now);
    check(testRaidCues.contains("raid-shot-hammer"), "test raid guarantees Ban Hammer attacks");
    check(testRaidCues.contains("raid-shot-railgun"), "test raid guarantees Railgun attacks");
    battle.onSound = {};
    battle.clear();

    QStringList soloTestCues;
    battle.onSound = [&soloTestCues](const QString &name) { soloTestCues.push_back(name); };
    check(battle.start(1, 0, true), "single-viewer test raid starts");
    for (qint64 now = 200; now <= 2200; now += 200) battle.tick(now);
    check(soloTestCues.contains("raid-shot-hammer") && soloTestCues.contains("raid-shot-railgun"),
          "single-viewer test raid rotates Ban Hammer and Railgun attacks");
    battle.onSound = {};
    battle.clear();

    check(battle.start(34, 0, true), "test raid starts");
    for (qint64 now = 33; now < 20000; now += 33) battle.tick(now);
    battle.tick(20000);
    status = battle.status(20000);
    check(status.value("roadblock").toString() == "infinite_loop", "deterministic test roadblock deployed");
    check(battle.chat("Debugger", "mod", "break;", 20100) == "roadblock_cleared", "exact loop command clears roadblock");
    check(battle.chat("IceMage", "vip", QString::fromUtf8("freeze ❄️"), 20200) == "attack_queued", "ice emote attack accepted");
    check(battle.chat("FireMage", "sub", QString::fromUtf8("burn 🔥"), 20300) == "attack_queued", "fire emote attack accepted");
    status = battle.status(20300);
    check(status.value("ice_active").toBool() && status.value("firewall_active").toBool(), "environmental hazards activate");

    for (qint64 now = 20333; now < 23000; now += 33) battle.tick(now);
    status = battle.status(23000);
    const QJsonArray leaders = status.value("leaderboard").toArray();
    check(!leaders.isEmpty(), "projectile collisions record leaderboard damage");
    check(status.value("enemies_defeated").toInt() > 0, "focused enemies visibly die during the simulated raid");

    battle.clear();
    check(battle.start(8, 0, false), "text-command hazard raid starts");
    check(battle.chat("TextFire", "viewer", "!fire", 100) == "attack_queued", "fire text command accepted");
    check(battle.chat("TextFire", "viewer", "!ice", 200) == "attack_cooldown", "ice ability remains responsive during shot cooldown");
    status = battle.status(200);
    check(status.value("ice_active").toBool() && status.value("firewall_active").toBool(), "text commands activate hazards");

    battle.clear();
    check(battle.start(1, 0, false), "projectile-finish regression raid starts");
    qint64 finishAt = 0;
    for (qint64 now = 50; now <= 90000 && battle.fighting(); now += 50) {
        if (now % 200 == 0) battle.chat(QString("HammerMod%1").arg((now / 200) % 5), "mod", "attack", now);
        battle.tick(now);
        if (!battle.fighting()) finishAt = now;
    }
    check(finishAt > 0 && battle.status(finishAt).value("phase").toString() == "results",
          "final projectile enters results without corrupting raid state");
    battle.tick(finishAt + 12000);
    check(!battle.visible(), "result cleanup after a projectile victory is safe");

    check(battle.start(34, 0, true), "preview raid restarts after regression cleanup");
    for (qint64 now = 33; now < 23000; now += 33) battle.tick(now);

    QImage image(1920, 1080, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    battle.paint(painter, 23000);
    painter.end();
    if (argc == 2) check(image.save(QString::fromLocal8Bit(argv[1])), "raid preview image saved");
    int maximumAlpha = 0;
    for (int y = 0; y < image.height(); y += 8)
        for (int x = 0; x < image.width(); x += 8) maximumAlpha = std::max(maximumAlpha, qAlpha(image.pixel(x, y)));
    check(maximumAlpha > 0, "raid renderer produces a transparent overlay frame");

    QElapsedTimer renderBenchmark;
    renderBenchmark.start();
    constexpr int benchmarkFrames = 60;
    for (int frame = 0; frame < benchmarkFrames; ++frame) {
        image.fill(Qt::transparent);
        QPainter framePainter(&image);
        battle.paint(framePainter, 23000 + frame * 50);
    }
    std::cout << "Raid renderer 1080p: "
              << static_cast<double>(renderBenchmark.nsecsElapsed()) / 1000000.0 / benchmarkFrames
              << " ms/frame\n";

    return failures ? 1 : 0;
}
