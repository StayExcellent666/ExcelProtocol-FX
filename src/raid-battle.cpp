#include "raid-battle.hpp"

#include <QFont>
#include <QJsonArray>
#include <QPainter>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace fx {
namespace {
constexpr qint64 battleDurationMs = 180000;
constexpr qint64 resultsDurationMs = 12000;
constexpr int maximumViewers = 1000000;
constexpr int maximumWarriors = 30;
constexpr int maximumProjectiles = 240;

void label(QPainter &painter, const QRectF &rect, const QString &value, int pixels,
           const QColor &color = Qt::white, int flags = Qt::AlignLeft | Qt::AlignVCenter)
{
    QFont font("Cascadia Mono");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(pixels);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, flags, value);
}

double distance(const QPointF &a, const QPointF &b)
{
    const QPointF delta = b - a;
    return std::hypot(delta.x(), delta.y());
}
}

QString RaidBattle::safeName(QString value)
{
    value = value.trimmed();
    value.removeIf([](QChar character) { return character.isNull() || character.category() == QChar::Other_Control; });
    return value.left(32);
}

QString RaidBattle::phaseName(Phase phase)
{
    switch (phase) {
    case Phase::Idle: return "idle";
    case Phase::Fighting: return "fighting";
    case Phase::Results: return "results";
    }
    return "idle";
}

QString RaidBattle::roadblockName(Roadblock roadblock)
{
    switch (roadblock) {
    case Roadblock::None: return "none";
    case Roadblock::InfiniteLoop: return "infinite_loop";
    case Roadblock::MergeConflict: return "merge_conflict";
    }
    return "none";
}

bool RaidBattle::start(int viewers, qint64 nowMs, bool testMode)
{
    if (viewers < 1 || viewers > maximumViewers || phase_ == Phase::Fighting) return false;
    clear();
    phase_ = Phase::Fighting;
    viewers_ = viewers;
    maximumHp_ = std::floor(1000.0 * std::pow(static_cast<double>(viewers), 0.7));
    hp_ = maximumHp_;
    startedMs_ = lastTickMs_ = nowMs;
    testMode_ = testMode;
    seed_ = testMode ? 0x45584345u : QRandomGenerator::global()->generate();
    QRandomGenerator random(seed_);
    const QStringList bugs{"SyntaxError", "NullReference", "StackOverflow", "MemoryLeak", "RaceCondition", "NaNException"};
    const int count = std::clamp(viewers * 2, 8, 60);
    enemies_.reserve(count);
    for (int index = 0; index < count; ++index) {
        enemies_.push_back({
            bugs.at(random.bounded(bugs.size())),
            QPointF(1340 + random.generateDouble() * 500, 145 + random.generateDouble() * 765),
            QPointF((random.generateDouble() - 0.5) * 90, (random.generateDouble() - 0.5) * 90),
        });
    }
    if (onSound) onSound("raid-start");
    return true;
}

double RaidBattle::damageMultiplier() const
{
    return viewers_ <= 10 && maximumHp_ > 0 && hp_ / maximumHp_ < 0.3 ? 1.5 : 1.0;
}

void RaidBattle::spawnAttack(const QString &username, const QString &weapon, int damage)
{
    QRandomGenerator random(seed_ ^ (++sequence_ * 0x9e3779b9u));
    QPointF origin;
    if (!warriors_.contains(username)) {
        const int slot = warriors_.size() < maximumWarriors ? static_cast<int>(warriors_.size())
                                                            : static_cast<int>(sequence_ % maximumWarriors);
        origin = QPointF(75 + (slot % 5) * 225 + random.generateDouble() * 28,
                         175 + (slot / 5) * 135 + random.generateDouble() * 22);
        if (warriors_.size() < maximumWarriors) warriors_.insert(username, {username, weapon, origin});
    } else {
        warriors_[username].weapon = weapon;
        origin = warriors_[username].position;
    }
    if (projectiles_.size() >= maximumProjectiles) projectiles_.removeFirst();
    const QColor color = weapon == "Ban Hammer" ? QColor(255, 51, 102)
        : weapon == "Railgun" ? QColor(30, 238, 255)
        : weapon == "Plasma Rifle" ? QColor(102, 255, 90) : QColor(255, 213, 70);
    projectiles_.push_back({username, weapon, origin + QPointF(34, 18),
        QPointF(1390 + random.generateDouble() * 400, 160 + random.generateDouble() * 720),
        static_cast<double>(damage), color});
}

QString RaidBattle::chat(const QString &rawUsername, const QString &rawRole,
                         const QString &message, qint64 nowMs)
{
    if (phase_ != Phase::Fighting) return "no_active_raid";
    const QString username = safeName(rawUsername);
    if (username.isEmpty() || message.trimmed().isEmpty() || message.size() > 500) return "invalid_raid_chat";
    const QString role = rawRole.trimmed().toLower();
    if (!role.isEmpty() && role != "default" && role != "viewer" && role != "sub"
        && role != "subscriber" && role != "vip" && role != "mod") return "invalid_raid_role";
    ++interactionCount_;
    const QString command = message.trimmed().toLower();
    if (roadblock_ == Roadblock::InfiniteLoop && command == "break;") {
        roadblock_ = Roadblock::None;
        if (onSound) onSound("raid-unlock");
        return "roadblock_cleared";
    }
    if (roadblock_ == Roadblock::MergeConflict && command == "!git push --force") {
        roadblock_ = Roadblock::None;
        if (onSound) onSound("raid-unlock");
        return "roadblock_cleared";
    }
    bool environmentTriggered = false;
    if (message.contains(QString::fromUtf8("🔥"))) {
        firewallUntilMs_ = std::min(nowMs + 10000, std::max(firewallUntilMs_, nowMs) + 5000);
        if (onSound) onSound("raid-fire");
        environmentTriggered = true;
    }
    if (message.contains(QString::fromUtf8("❄️"))) {
        iceUntilMs_ = std::max(iceUntilMs_, nowMs + 5000);
        if (onSound) onSound("raid-ice");
        environmentTriggered = true;
    }
    int damage = 10;
    QString weapon = "Blaster";
    if (role == "mod") { damage = 50; weapon = "Ban Hammer"; }
    else if (role == "vip") { damage = 35; weapon = "Railgun"; }
    else if (role == "sub" || role == "subscriber") { damage = 20; weapon = "Plasma Rifle"; }
    spawnAttack(username, weapon, damage);
    if (!environmentTriggered && nowMs - lastShotSoundMs_ >= 180) {
        lastShotSoundMs_ = nowMs;
        if (onSound) onSound("raid-shot");
    }
    return roadblock_ == Roadblock::None ? "attack_queued" : "attack_blocked";
}

void RaidBattle::deployRoadblock(qint64 nowMs)
{
    roadblockDeployed_ = true;
    roadblockStartedMs_ = nowMs;
    roadblock_ = (seed_ & 1u) ? Roadblock::InfiniteLoop : Roadblock::MergeConflict;
    if (onSound) onSound("raid-alert");
}

void RaidBattle::applyDamage(const QString &username, double damage, qint64 nowMs)
{
    if (phase_ != Phase::Fighting || roadblock_ != Roadblock::None || damage <= 0) return;
    const int applied = std::max(1, qRound(damage * damageMultiplier()));
    hp_ = std::max(0.0, hp_ - applied);
    if (!username.isEmpty()) damageLedger_[username] += applied;
    if (hp_ <= 0) finish(true, nowMs);
}

void RaidBattle::driveTestRaid(qint64 nowMs)
{
    if (!testMode_ || phase_ != Phase::Fighting) return;
    if (nowMs - lastTestAttackMs_ >= 650) {
        lastTestAttackMs_ = nowMs;
        static const QStringList names{"ByteKnight", "PixelPilot", "StackSmash", "NeonCoder", "CacheQueen", "NullHunter"};
        static const QStringList roles{"default", "sub", "vip", "default", "mod", "sub"};
        const int index = static_cast<int>(sequence_ % static_cast<quint32>(names.size()));
        QString message = "ship it";
        if ((sequence_ % 17) == 7) message += QString::fromUtf8(" 🔥");
        if ((sequence_ % 19) == 9) message += QString::fromUtf8(" ❄️");
        chat(names[index], roles[index], message, nowMs);
    }
    if (roadblock_ != Roadblock::None && nowMs - roadblockStartedMs_ >= 3500) {
        chat("TestDebugger", "mod", roadblock_ == Roadblock::InfiniteLoop ? "break;" : "!git push --force", nowMs);
    }
}

bool RaidBattle::tick(qint64 nowMs)
{
    const Phase previousPhase = phase_;
    const Roadblock previousRoadblock = roadblock_;
    if (phase_ == Phase::Results) {
        if (nowMs - finishedMs_ >= resultsDurationMs) clear();
        return previousPhase != phase_;
    }
    if (phase_ != Phase::Fighting) return false;
    const double seconds = std::clamp((nowMs - lastTickMs_) / 1000.0, 0.0, 0.1);
    lastTickMs_ = nowMs;
    driveTestRaid(nowMs);
    if (!roadblockDeployed_ && nowMs - startedMs_ >= 20000) deployRoadblock(nowMs);

    const double speed = nowMs < iceUntilMs_ ? 0.5 : 1.0;
    for (Enemy &enemy : enemies_) {
        enemy.position += enemy.velocity * seconds * speed;
        if (enemy.position.x() < 1320 || enemy.position.x() > 1840) enemy.velocity.rx() *= -1;
        if (enemy.position.y() < 130 || enemy.position.y() > 930) enemy.velocity.ry() *= -1;
        enemy.position.setX(std::clamp(enemy.position.x(), 1320.0, 1840.0));
        enemy.position.setY(std::clamp(enemy.position.y(), 130.0, 930.0));
    }

    for (qsizetype index = projectiles_.size(); index > 0; --index) {
        Projectile &projectile = projectiles_[index - 1];
        const QPointF delta = projectile.target - projectile.position;
        const double remaining = distance(projectile.position, projectile.target);
        const double step = (projectile.weapon == "Railgun" ? 1800.0 : 1050.0) * seconds;
        if (remaining <= std::max(18.0, step)) {
            applyDamage(projectile.username, projectile.damage, nowMs);
            projectiles_.removeAt(index - 1);
        } else if (remaining > 0) {
            projectile.position += delta / remaining * step;
        }
    }

    if (phase_ == Phase::Fighting && roadblock_ == Roadblock::None) {
        // A single participant can secure the code before the deadline. More
        // participants and more attacks still finish the battle much sooner.
        const double participationAssist = warriors_.isEmpty() ? 0.0 : maximumHp_ / 150.0;
        const double passive = (participationAssist + warriors_.size() * 4.0) * seconds;
        const double firewall = nowMs < firewallUntilMs_ ? 18.0 * seconds : 0.0;
        if (passive + firewall > 0) applyDamage({}, passive + firewall, nowMs);
    }
    // Participation always earns the celebratory outcome. This final assist
    // also prevents an uncleared roadblock from turning an engaged raid into a loss.
    if (phase_ == Phase::Fighting && interactionCount_ > 0 && nowMs - startedMs_ >= battleDurationMs - 5000) {
        hp_ = 0;
        finish(true, nowMs);
    } else if (phase_ == Phase::Fighting && nowMs - startedMs_ >= battleDurationMs) {
        finish(false, nowMs);
    }
    return previousPhase != phase_ || previousRoadblock != roadblock_;
}

void RaidBattle::buildLeaderboard()
{
    leaderboard_.clear();
    leaderboard_.reserve(damageLedger_.size());
    for (auto it = damageLedger_.cbegin(); it != damageLedger_.cend(); ++it)
        leaderboard_.push_back({it.key(), it.value()});
    std::sort(leaderboard_.begin(), leaderboard_.end(), [](const Score &a, const Score &b) {
        return a.damage != b.damage ? a.damage > b.damage : a.username.compare(b.username, Qt::CaseInsensitive) < 0;
    });
    if (leaderboard_.size() > 5) leaderboard_.resize(5);
}

bool RaidBattle::finish(bool victory, qint64 nowMs)
{
    if (phase_ != Phase::Fighting) return false;
    if (!victory && interactionCount_ > 0) victory = true;
    if (!victory && nowMs - startedMs_ < battleDurationMs) return false;
    phase_ = Phase::Results;
    victory_ = victory;
    finishedMs_ = nowMs;
    roadblock_ = Roadblock::None;
    projectiles_.clear();
    buildLeaderboard();
    if (onSound) onSound(victory ? "raid-victory" : "raid-failure");
    return true;
}

void RaidBattle::clear()
{
    phase_ = Phase::Idle;
    roadblock_ = Roadblock::None;
    viewers_ = 0;
    maximumHp_ = hp_ = 0;
    startedMs_ = finishedMs_ = lastTickMs_ = roadblockStartedMs_ = 0;
    iceUntilMs_ = firewallUntilMs_ = lastTestAttackMs_ = 0;
    roadblockDeployed_ = victory_ = testMode_ = false;
    sequence_ = 0;
    interactionCount_ = 0;
    lastShotSoundMs_ = -1000;
    warriors_.clear();
    damageLedger_.clear();
    projectiles_.clear();
    enemies_.clear();
    leaderboard_.clear();
}

QJsonObject RaidBattle::status(qint64 nowMs) const
{
    QJsonArray leaders;
    QVector<Score> scores = leaderboard_;
    if (phase_ == Phase::Fighting) {
        for (auto it = damageLedger_.cbegin(); it != damageLedger_.cend(); ++it) scores.push_back({it.key(), it.value()});
        std::sort(scores.begin(), scores.end(), [](const Score &a, const Score &b) { return a.damage > b.damage; });
        if (scores.size() > 5) scores.resize(5);
    }
    for (const Score &score : scores) leaders.push_back(QJsonObject{{"username", score.username}, {"damage", score.damage}});
    return {
        {"phase", phaseName(phase_)}, {"viewers", viewers_},
        {"hp", qRound(hp_)}, {"max_hp", qRound(maximumHp_)},
        {"remaining_ms", phase_ == Phase::Fighting ? static_cast<int>(std::max<qint64>(0, battleDurationMs - (nowMs - startedMs_))) : 0},
        {"roadblock", roadblockName(roadblock_)}, {"ice_active", phase_ == Phase::Fighting && nowMs < iceUntilMs_},
        {"firewall_active", phase_ == Phase::Fighting && nowMs < firewallUntilMs_},
        {"warrior_count", warriors_.size()}, {"victory", phase_ == Phase::Results && victory_},
        {"interaction_count", interactionCount_}, {"test_mode", testMode_}, {"leaderboard", leaders},
    };
}

void RaidBattle::paint(QPainter &painter, qint64 nowMs) const
{
    if (phase_ == Phase::Idle) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor cyan(33, 231, 245), red(255, 51, 102), gold(255, 205, 55), panel(5, 12, 22, 238);

    if (phase_ == Phase::Results) {
        const double appear = std::clamp((nowMs - finishedMs_) / 450.0, 0.0, 1.0);
        painter.setOpacity(appear);
        painter.setPen(QPen(victory_ ? cyan : red, 3));
        painter.setBrush(panel);
        painter.drawRoundedRect(QRectF(520, 170, 880, 740), 18, 18);
        label(painter, QRectF(560, 210, 800, 70), victory_ ? "HORDE DEFEATED: CODE SECURED" : "COMPILATION FAILED", 38,
              victory_ ? cyan : red, Qt::AlignCenter);
        label(painter, QRectF(610, 290, 700, 45), "TOP DAMAGE DEALERS", 23, QColor(170, 194, 210), Qt::AlignCenter);
        if (leaderboard_.isEmpty()) label(painter, QRectF(610, 405, 700, 60), "NO DAMAGE RECORDED", 24, QColor(170, 194, 210), Qt::AlignCenter);
        for (int index = 0; index < leaderboard_.size(); ++index) {
            const double rowAppear = std::clamp((nowMs - finishedMs_ - 400 - index * 180) / 320.0, 0.0, 1.0);
            painter.setOpacity(appear * rowAppear);
            const qreal x = 610 + (1.0 - rowAppear) * 80;
            const qreal y = 365 + index * 92;
            painter.setPen(QPen(index == 0 ? gold : QColor(48, 75, 92), 2));
            painter.setBrush(QColor(10, 24, 35, 245));
            painter.drawRoundedRect(QRectF(x, y, 700, 70), 8, 8);
            label(painter, QRectF(x + 22, y, 490, 70), QString("#%1  %2%3").arg(index + 1)
                  .arg(leaderboard_[index].username).arg(index == 0 ? "  // THE BUG CRUSHER" : ""), 21, index == 0 ? gold : Qt::white);
            label(painter, QRectF(x + 520, y, 150, 70), QString("%1 DMG").arg(leaderboard_[index].damage), 20,
                  index == 0 ? gold : cyan, Qt::AlignRight | Qt::AlignVCenter);
        }
        painter.restore();
        return;
    }

    painter.setPen(QPen(QColor(42, 78, 94), 2));
    painter.setBrush(QColor(4, 11, 20, 224));
    painter.drawRoundedRect(QRectF(420, 30, 1080, 100), 12, 12);
    label(painter, QRectF(450, 41, 750, 38), "TWITCH RAID // CYBER THREAT DEFENSE", 24, cyan);
    const double ratio = maximumHp_ > 0 ? std::clamp(hp_ / maximumHp_, 0.0, 1.0) : 0;
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(25, 34, 47)); painter.drawRoundedRect(QRectF(450, 84, 1020, 25), 5, 5);
    painter.setBrush(roadblock_ == Roadblock::None ? red : gold); painter.drawRoundedRect(QRectF(450, 84, 1020 * ratio, 25), 5, 5);
    label(painter, QRectF(1200, 40, 270, 35), QString("%1 / %2 HP").arg(qRound(hp_)).arg(qRound(maximumHp_)), 20, Qt::white, Qt::AlignRight | Qt::AlignVCenter);
    const int secondsLeft = std::max(0, static_cast<int>((battleDurationMs - (nowMs - startedMs_) + 999) / 1000));
    label(painter, QRectF(40, 35, 340, 45), QString("RAIDERS: %1").arg(viewers_), 21, cyan);
    label(painter, QRectF(40, 80, 340, 40), QString("TIME: %1:%2").arg(secondsLeft / 60).arg(secondsLeft % 60, 2, 10, QLatin1Char('0')), 21, Qt::white);

    if (nowMs < iceUntilMs_) {
        painter.setPen(QPen(QColor(70, 190, 255, 65), 2));
        for (int y = 0; y < 1080; y += 18) painter.drawLine(0, y, 1920, y);
        painter.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 80, 190, 25));
        label(painter, QRectF(40, 130, 360, 35), "ICE PATCH // BUG SPEED -50%", 17, QColor(100, 210, 255));
    }
    if (nowMs < firewallUntilMs_) {
        QRadialGradient fire(QPointF(1540, 600), 340);
        fire.setColorAt(0, QColor(255, 105, 0, 85)); fire.setColorAt(1, QColor(255, 30, 0, 0));
        painter.setPen(QPen(QColor(255, 105, 20, 180), 4)); painter.setBrush(fire);
        painter.drawEllipse(QPointF(1540, 600), 340, 340);
        label(painter, QRectF(1370, 935, 420, 35), "FIREWALL DAMAGE ACTIVE", 19, QColor(255, 130, 30), Qt::AlignCenter);
    }

    for (const Enemy &enemy : enemies_) {
        painter.setPen(QPen(nowMs < iceUntilMs_ ? QColor(70, 190, 255) : red, 2));
        painter.setBrush(QColor(13, 13, 24, 235));
        painter.drawRoundedRect(QRectF(enemy.position.x() - 7, enemy.position.y() - 24, 155, 38), 5, 5);
        label(painter, QRectF(enemy.position.x(), enemy.position.y() - 23, 142, 34), enemy.label, 15,
              nowMs < iceUntilMs_ ? QColor(90, 210, 255) : red);
    }

    for (auto it = warriors_.cbegin(); it != warriors_.cend(); ++it) {
        const Warrior &warrior = it.value();
        painter.setPen(QPen(QColor(4, 38, 48), 2)); painter.setBrush(cyan);
        painter.drawRoundedRect(QRectF(warrior.position.x(), warrior.position.y(), 30, 42), 5, 5);
        painter.setBrush(QColor(4, 13, 23)); painter.drawRect(QRectF(warrior.position.x() + 6, warrior.position.y() + 9, 18, 12));
        painter.setPen(QPen(cyan, 4)); painter.drawLine(warrior.position + QPointF(27, 23), warrior.position + QPointF(48, 23));
        label(painter, QRectF(warrior.position.x() - 10, warrior.position.y() - 27, 210, 24), warrior.name, 14, Qt::white);
        label(painter, QRectF(warrior.position.x() - 10, warrior.position.y() + 45, 210, 22), warrior.weapon, 12, QColor(140, 210, 220));
    }

    for (const Projectile &projectile : projectiles_) {
        const QPointF direction = projectile.target - projectile.position;
        const double length = std::max(1.0, std::hypot(direction.x(), direction.y()));
        const QPointF tail = projectile.position - direction / length * (projectile.weapon == "Railgun" ? 90 : 32);
        painter.setPen(QPen(projectile.color, projectile.weapon == "Ban Hammer" ? 9 : 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(tail, projectile.position);
        painter.setBrush(projectile.color); painter.setPen(Qt::NoPen);
        painter.drawEllipse(projectile.position, projectile.weapon == "Ban Hammer" ? 10 : 6, projectile.weapon == "Ban Hammer" ? 10 : 6);
    }

    if (roadblock_ != Roadblock::None) {
        const double pulse = 0.75 + 0.25 * std::sin((nowMs - roadblockStartedMs_) / 170.0);
        painter.setOpacity(pulse);
        painter.setPen(QPen(red, 4)); painter.setBrush(QColor(32, 5, 12, 245));
        painter.drawRoundedRect(QRectF(470, 360, 980, 300), 14, 14);
        if (roadblock_ == Roadblock::InfiniteLoop) {
            painter.setPen(QPen(gold, 12, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(QRectF(845, 395, 230, 145), static_cast<int>((nowMs / 4) % (360 * 16)), 270 * 16);
            label(painter, QRectF(540, 530, 840, 50), "INFINITE LOOP FREEZE", 34, gold, Qt::AlignCenter);
            label(painter, QRectF(540, 585, 840, 40), "CHAT: type  break;", 24, Qt::white, Qt::AlignCenter);
        } else {
            label(painter, QRectF(540, 405, 840, 65), "<<<<<<< HEAD", 42, red, Qt::AlignCenter);
            label(painter, QRectF(540, 490, 840, 50), "MERGE CONFLICT BLOCKER", 31, gold, Qt::AlignCenter);
            label(painter, QRectF(540, 565, 840, 40), "CHAT: type  !git push --force", 24, Qt::white, Qt::AlignCenter);
        }
        painter.setOpacity(1);
    } else if (damageMultiplier() > 1.0) {
        label(painter, QRectF(650, 140, 620, 38), "AGAINST ALL ODDS // DAMAGE x1.5", 20, gold, Qt::AlignCenter);
    }
    painter.restore();
}

} // namespace fx
