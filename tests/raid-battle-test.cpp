#include "raid-battle.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
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
    check(status.value("max_hp").toInt() == static_cast<int>(std::floor(1000.0 * std::pow(8.0, 0.7))), "HP follows viewer curve");
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
    check(battle.start(6, 0, true), "test raid starts");
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

    return failures ? 1 : 0;
}
