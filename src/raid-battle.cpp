#include "raid-battle.hpp"

#include <QFont>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
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
constexpr int maximumRaiders = 48;
constexpr int maximumProjectiles = 240;
constexpr int maximumVisibleEnemies = 5;

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

QColor raiderColor(int index)
{
    static const QColor colors[]{QColor(33, 231, 245), QColor(112, 255, 115), QColor(255, 206, 55),
                                  QColor(185, 105, 255), QColor(255, 100, 155), QColor(80, 155, 255)};
    return colors[index % 6];
}

void paintRaider(QPainter &painter, const QPointF &p, int style, const QColor &color,
                 const QPointF &aim, bool active, double pulse)
{
    painter.save();
    painter.translate(p);
    if (active) {
        painter.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 110), 2));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), static_cast<int>(25 + pulse * 30)));
        painter.drawEllipse(QPointF(15, 21), 25 + pulse * 4, 25 + pulse * 4);
    }
    painter.setPen(QPen(QColor(3, 22, 29), 2));
    painter.setBrush(color);
    if (style == 0) {
        painter.drawRoundedRect(QRectF(5, 5, 22, 30), 5, 5);
        painter.setBrush(QColor(3, 14, 23)); painter.drawRect(QRectF(9, 11, 14, 8));
        painter.setPen(QPen(color, 3)); painter.drawLine(7, 36, 3, 45); painter.drawLine(24, 36, 29, 45);
    } else if (style == 1) {
        painter.drawEllipse(QRectF(5, 3, 22, 18)); painter.drawRoundedRect(QRectF(8, 20, 17, 19), 3, 3);
        painter.setPen(QPen(QColor(3, 14, 23), 3)); painter.drawLine(10, 12, 14, 12); painter.drawLine(19, 12, 23, 12);
    } else if (style == 2) {
        QPolygonF hood{QPointF(16, 1), QPointF(29, 18), QPointF(25, 39), QPointF(7, 39), QPointF(3, 18)};
        painter.drawPolygon(hood); painter.setBrush(QColor(3, 14, 23)); painter.drawEllipse(QPointF(16, 18), 7, 6);
    } else {
        painter.drawRect(QRectF(5, 7, 23, 28)); painter.setBrush(QColor(3, 14, 23));
        painter.drawRect(QRectF(9, 11, 15, 10)); painter.setPen(QPen(color, 3));
        painter.drawLine(7, 4, 2, 0); painter.drawLine(26, 4, 31, 0);
    }
    QPointF direction = aim - p - QPointF(26, 22);
    const double length = std::max(1.0, std::hypot(direction.x(), direction.y()));
    direction /= length;
    painter.setPen(QPen(color.lighter(125), active ? 5 : 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(25, 22), QPointF(25, 22) + direction * (active ? 28 : 21));
    painter.restore();
}

void paintEnemyShape(QPainter &painter, int kind, const QPointF &p, const QColor &color,
                     quint32 seed, double motion)
{
    painter.save();
    painter.translate(p);
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(10, 13, 25, 240));
    if (kind == 0) {
        painter.drawEllipse(QRectF(-38, -27, 76, 54));
        for (int side : {-1, 1}) for (int leg = 0; leg < 3; ++leg) {
            const qreal y = -18 + leg * 18;
            painter.drawLine(QPointF(side * 30, y), QPointF(side * (51 + leg * 3), y + (leg - 1) * 7));
        }
        painter.setBrush(color); painter.drawEllipse(QPointF(-14, -7), 5, 5); painter.drawEllipse(QPointF(14, -7), 5, 5);
        label(painter, QRectF(-25, 3, 50, 20), "{;}", 15, color, Qt::AlignCenter);
    } else if (kind == 1) {
        QPainterPath ghost; ghost.moveTo(-38, 30); ghost.cubicTo(-52, -13, -33, -43, 0, -45);
        ghost.cubicTo(34, -43, 50, -12, 38, 30); ghost.lineTo(25, 18); ghost.lineTo(12, 32);
        ghost.lineTo(0, 18); ghost.lineTo(-13, 32); ghost.lineTo(-26, 18); ghost.closeSubpath();
        painter.drawPath(ghost); painter.setBrush(color); painter.drawEllipse(QPointF(-13, -13), 6, 8); painter.drawEllipse(QPointF(13, -13), 6, 8);
    } else if (kind == 2) {
        for (int i = 0; i < 4; ++i) {
            const qreal shift = (i % 2) * 8 - 4;
            painter.drawRoundedRect(QRectF(-43 + shift, 24 - i * 20, 86, 17), 3, 3);
        }
        painter.setBrush(color); painter.drawRect(QRectF(-24, -51, 48, 13));
        painter.drawLine(QPointF(-44, 17), QPointF(-60, 35)); painter.drawLine(QPointF(44, 17), QPointF(60, 35));
    } else if (kind == 3) {
        for (int i = 0; i < 6; ++i) {
            const qreal x = -45 + i * 17; const qreal y = std::sin(motion + i * 0.8) * 8;
            painter.drawEllipse(QPointF(x, y), 13, 13);
        }
        painter.setBrush(color); painter.drawEllipse(QPointF(43, std::sin(motion + 4) * 8), 16, 16);
        painter.setBrush(QColor(4, 9, 18)); painter.drawEllipse(QPointF(49, -2), 4, 4);
    } else if (kind == 4) {
        QPolygonF body{QPointF(0, -34), QPointF(39, 0), QPointF(0, 34), QPointF(-39, 0)};
        painter.drawPolygon(body); painter.setBrush(color); painter.drawEllipse(QPointF(0, 0), 10, 10);
        painter.drawLine(QPointF(-32, -16), QPointF(-56, -28)); painter.drawLine(QPointF(32, -16), QPointF(56, -28));
        painter.drawEllipse(QPointF(-58, -29), 15, 5); painter.drawEllipse(QPointF(58, -29), 15, 5);
    } else {
        QPolygonF shard{QPointF(-44, -26), QPointF(-11, -43), QPointF(2, -24), QPointF(35, -39),
                         QPointF(45, -4), QPointF(23, 31), QPointF(-8, 24), QPointF(-39, 39), QPointF(-31, 4)};
        painter.drawPolygon(shard);
        QRandomGenerator random(seed);
        painter.setBrush(color); painter.setPen(Qt::NoPen);
        for (int i = 0; i < 8; ++i) painter.drawRect(QRectF(random.bounded(-58, 50), random.bounded(-47, 42), random.bounded(4, 13), random.bounded(3, 9)));
        label(painter, QRectF(-34, -15, 68, 30), "NaN", 18, QColor(4, 9, 18), Qt::AlignCenter);
    }
    painter.restore();
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

    const int displayedRaiders = std::clamp(viewers, 1, maximumRaiders);
    raiders_.reserve(displayedRaiders);
    const int columns = displayedRaiders <= 18 ? 6 : 8;
    const qreal horizontal = displayedRaiders <= 18 ? 170.0 : 132.0;
    const qreal vertical = displayedRaiders <= 18 ? 145.0 : 112.0;
    for (int index = 0; index < displayedRaiders; ++index) {
        const QPointF position(55 + (index % columns) * horizontal + random.generateDouble() * 12,
                               205 + (index / columns) * vertical + random.generateDouble() * 10);
        raiders_.push_back({QString(), "Blaster", position, raiderColor(index), index % 4, 0});
    }

    const QStringList bugs{"Syntax Crawler", "Null Wraith", "Stack Brute", "Memory Leech", "Race Drone", "NaN Glitch"};
    const int enemyCount = std::clamp(6 + qRound(std::sqrt(static_cast<double>(viewers)) * 1.9), 8, 36);
    enemies_.reserve(enemyCount);
    double allocated = 0;
    for (int index = 0; index < enemyCount; ++index) {
        const double unitHp = index + 1 == enemyCount ? maximumHp_ - allocated : std::floor(maximumHp_ / enemyCount);
        allocated += unitHp;
        const int kind = random.bounded(bugs.size());
        const qint64 spawnAt = nowMs + index * (viewers > 100 ? 420 : 650);
        enemies_.push_back({index, kind, bugs.at(kind), QPointF(2010 + random.bounded(0, 220), 250 + random.bounded(0, 610)),
                            QPointF(), unitHp, unitHp, spawnAt, nowMs, 0, random.generate(), EnemyState::Pending});
    }
    if (!enemies_.isEmpty()) {
        enemies_[0].state = EnemyState::Entering;
        enemies_[0].stateAtMs = nowMs;
    }
    if (onSound) onSound("raid-start");
    return true;
}

double RaidBattle::damageMultiplier() const
{
    return viewers_ <= 10 && maximumHp_ > 0 && hp_ / maximumHp_ < 0.3 ? 1.5 : 1.0;
}

int RaidBattle::focusedEnemyId() const
{
    for (const Enemy &enemy : enemies_)
        if (enemy.state == EnemyState::Entering || enemy.state == EnemyState::Active) return enemy.id;
    return -1;
}

RaidBattle::Enemy *RaidBattle::enemyById(int id)
{
    for (Enemy &enemy : enemies_) if (enemy.id == id) return &enemy;
    return nullptr;
}

const RaidBattle::Enemy *RaidBattle::enemyById(int id) const
{
    for (const Enemy &enemy : enemies_) if (enemy.id == id) return &enemy;
    return nullptr;
}

void RaidBattle::spawnAttack(const QString &username, const QString &weapon, int damage, qint64 nowMs)
{
    if (raiders_.isEmpty()) return;
    int slot = raiderSlots_.value(username, -1);
    if (slot < 0) {
        slot = nextRaiderSlot_++ % raiders_.size();
        raiderSlots_.insert(username, slot);
    }
    Raider &raider = raiders_[slot];
    raider.name = username;
    raider.weapon = weapon;
    raider.activeUntilMs = nowMs + 2400;
    if (projectiles_.size() >= maximumProjectiles) projectiles_.removeFirst();
    const QColor color = weapon == "Ban Hammer" ? QColor(255, 51, 102)
        : weapon == "Railgun" ? QColor(30, 238, 255)
        : weapon == "Plasma Rifle" ? QColor(102, 255, 90) : QColor(255, 213, 70);
    projectiles_.push_back({username, weapon, raider.position + QPointF(35, 21), focusedEnemyId(),
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
    spawnAttack(username, weapon, damage, nowMs);
    if (!environmentTriggered && nowMs - lastShotSoundMs_ >= 110) {
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

void RaidBattle::applyDamage(const QString &username, int enemyId, double damage, qint64 nowMs)
{
    if (phase_ != Phase::Fighting || roadblock_ != Roadblock::None || damage <= 0) return;
    Enemy *enemy = enemyById(enemyId);
    if (!enemy || (enemy->state != EnemyState::Entering && enemy->state != EnemyState::Active))
        enemy = enemyById(focusedEnemyId());
    if (!enemy) return;
    const int requested = std::max(1, qRound(damage * damageMultiplier()));
    const int applied = std::max(0, std::min(requested, qCeil(enemy->hp)));
    if (applied <= 0) return;
    enemy->hp = std::max(0.0, enemy->hp - applied);
    hp_ = std::max(0.0, hp_ - applied);
    enemy->flashUntilMs = nowMs + 130;
    impacts_.push_back({enemy->position, QColor(255, 220, 80), nowMs, false, enemy->visualSeed ^ sequence_});
    if (!username.isEmpty()) damageLedger_[username] += applied;
    if (enemy->hp <= 0) {
        enemy->state = EnemyState::Dying;
        enemy->stateAtMs = nowMs;
        ++defeatedEnemies_;
        impacts_.push_back({enemy->position, QColor(255, 55, 110), nowMs, true, enemy->visualSeed});
        if (onSound) onSound("raid-enemy-death");
    } else if (nowMs - lastHitSoundMs_ >= 150) {
        lastHitSoundMs_ = nowMs;
        if (onSound) onSound("raid-enemy-hit");
    }
    if (hp_ <= 0 || defeatedEnemies_ == enemies_.size()) finish(true, nowMs);
}

void RaidBattle::driveTestRaid(qint64 nowMs)
{
    if (!testMode_ || phase_ != Phase::Fighting) return;
    const qint64 interval = std::clamp<qint64>(720 - viewers_ * 8, 170, 620);
    if (nowMs - lastTestAttackMs_ >= interval) {
        lastTestAttackMs_ = nowMs;
        const int member = static_cast<int>(sequence_++ % static_cast<quint32>(std::max(1, static_cast<int>(raiders_.size()))));
        const QString username = QString("Raider%1").arg(member + 1, 2, 10, QLatin1Char('0'));
        QString role = "default";
        if (member == 0) role = "mod";
        else if (member % 11 == 0) role = "vip";
        else if (member % 4 == 0) role = "sub";
        QString message = "ship it";
        if ((sequence_ % 17) == 7) message += QString::fromUtf8(" 🔥");
        if ((sequence_ % 19) == 9) message += QString::fromUtf8(" ❄️");
        chat(username, role, message, nowMs);
    }
    if (roadblock_ != Roadblock::None && nowMs - roadblockStartedMs_ >= 3500) {
        chat("TestDebugger", "mod", roadblock_ == Roadblock::InfiniteLoop ? "break;" : "!git push --force", nowMs);
    }
}

void RaidBattle::updateEnemies(qint64 nowMs, double seconds, double speed)
{
    int visible = 0;
    for (const Enemy &enemy : enemies_)
        if (enemy.state == EnemyState::Entering || enemy.state == EnemyState::Active) ++visible;
    for (Enemy &enemy : enemies_) {
        if (visible >= maximumVisibleEnemies) break;
        if (enemy.state == EnemyState::Pending && nowMs >= enemy.spawnAtMs) {
            enemy.state = EnemyState::Entering;
            enemy.stateAtMs = nowMs;
            ++visible;
            if (onSound && nowMs - lastSpawnSoundMs_ >= 380) {
                lastSpawnSoundMs_ = nowMs;
                onSound("raid-enemy-spawn");
            }
        }
    }
    int rank = 0;
    for (Enemy &enemy : enemies_) {
        if (enemy.state == EnemyState::Dying && nowMs - enemy.stateAtMs >= 820) enemy.state = EnemyState::Dead;
        if (enemy.state != EnemyState::Entering && enemy.state != EnemyState::Active) continue;
        const int row = rank / 3;
        const int column = rank % 3;
        enemy.destination = QPointF(1365 + column * 205 + row * 55, 310 + row * 260 + column * 95);
        const QPointF delta = enemy.destination - enemy.position;
        const double remaining = std::hypot(delta.x(), delta.y());
        if (remaining > 3) enemy.position += delta / remaining * std::min(remaining, (230.0 + rank * 14) * seconds * speed);
        if (remaining < 12) enemy.state = EnemyState::Active;
        ++rank;
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
    updateEnemies(nowMs, seconds, speed);

    for (qsizetype index = projectiles_.size(); index > 0; --index) {
        Projectile &projectile = projectiles_[index - 1];
        const Enemy *enemy = enemyById(projectile.enemyId);
        if (!enemy || (enemy->state != EnemyState::Entering && enemy->state != EnemyState::Active)) {
            projectile.enemyId = focusedEnemyId();
            enemy = enemyById(projectile.enemyId);
        }
        if (!enemy) { projectiles_.removeAt(index - 1); continue; }
        const QPointF delta = enemy->position - projectile.position;
        const double remaining = distance(projectile.position, enemy->position);
        const double step = (projectile.weapon == "Railgun" ? 1900.0 : 1150.0) * seconds;
        if (remaining <= std::max(24.0, step)) {
            applyDamage(projectile.username, projectile.enemyId, projectile.damage, nowMs);
            projectiles_.removeAt(index - 1);
        } else if (remaining > 0) {
            projectile.position += delta / remaining * step;
        }
    }

    for (qsizetype index = impacts_.size(); index > 0; --index)
        if (nowMs - impacts_[index - 1].startedMs > (impacts_[index - 1].death ? 900 : 300)) impacts_.removeAt(index - 1);

    if (phase_ == Phase::Fighting && roadblock_ == Roadblock::None) {
        const double participationAssist = interactionCount_ == 0 ? 0.0 : maximumHp_ / 150.0;
        const double activeRaiders = std::min(interactionCount_, static_cast<int>(raiders_.size())) * 3.0;
        const double passive = (participationAssist + activeRaiders) * seconds;
        const double firewall = nowMs < firewallUntilMs_ ? std::max(18.0, maximumHp_ / 220.0) * seconds : 0.0;
        if (passive + firewall > 0) applyDamage({}, focusedEnemyId(), passive + firewall, nowMs);
    }
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
    interactionCount_ = defeatedEnemies_ = nextRaiderSlot_ = 0;
    lastShotSoundMs_ = lastHitSoundMs_ = lastSpawnSoundMs_ = -1000;
    raiderSlots_.clear();
    raiders_.clear();
    damageLedger_.clear();
    projectiles_.clear();
    enemies_.clear();
    impacts_.clear();
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
        {"warrior_count", raiders_.size()}, {"represented_viewers_per_warrior", raiders_.isEmpty() ? 0 : qCeil(static_cast<double>(viewers_) / raiders_.size())},
        {"enemy_count", enemies_.size()}, {"enemies_defeated", defeatedEnemies_},
        {"victory", phase_ == Phase::Results && victory_}, {"interaction_count", interactionCount_},
        {"test_mode", testMode_}, {"leaderboard", leaders},
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
        painter.setPen(QPen(victory_ ? cyan : red, 3)); painter.setBrush(panel);
        painter.drawRoundedRect(QRectF(520, 170, 880, 740), 18, 18);
        label(painter, QRectF(560, 210, 800, 70), victory_ ? "HORDE DEFEATED: CODE SECURED" : "COMPILATION FAILED", 38,
              victory_ ? cyan : red, Qt::AlignCenter);
        label(painter, QRectF(610, 290, 700, 45), "TOP DAMAGE DEALERS", 23, QColor(170, 194, 210), Qt::AlignCenter);
        if (leaderboard_.isEmpty()) label(painter, QRectF(610, 405, 700, 60), "NO DAMAGE RECORDED", 24, QColor(170, 194, 210), Qt::AlignCenter);
        for (int index = 0; index < leaderboard_.size(); ++index) {
            const double rowAppear = std::clamp((nowMs - finishedMs_ - 400 - index * 180) / 320.0, 0.0, 1.0);
            painter.setOpacity(appear * rowAppear);
            const qreal x = 610 + (1.0 - rowAppear) * 80; const qreal y = 365 + index * 92;
            painter.setPen(QPen(index == 0 ? gold : QColor(48, 75, 92), 2)); painter.setBrush(QColor(10, 24, 35, 245));
            painter.drawRoundedRect(QRectF(x, y, 700, 70), 8, 8);
            label(painter, QRectF(x + 22, y, 490, 70), QString("#%1  %2%3").arg(index + 1)
                  .arg(leaderboard_[index].username).arg(index == 0 ? "  // THE BUG CRUSHER" : ""), 21, index == 0 ? gold : Qt::white);
            label(painter, QRectF(x + 520, y, 150, 70), QString("%1 DMG").arg(leaderboard_[index].damage), 20,
                  index == 0 ? gold : cyan, Qt::AlignRight | Qt::AlignVCenter);
        }
        painter.restore(); return;
    }

    painter.setPen(QPen(QColor(42, 78, 94), 2)); painter.setBrush(QColor(4, 11, 20, 224));
    painter.drawRoundedRect(QRectF(420, 30, 1080, 100), 12, 12);
    label(painter, QRectF(450, 41, 750, 38), "TWITCH RAID // CYBER THREAT DEFENSE", 24, cyan);
    const double ratio = maximumHp_ > 0 ? std::clamp(hp_ / maximumHp_, 0.0, 1.0) : 0;
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(25, 34, 47)); painter.drawRoundedRect(QRectF(450, 84, 1020, 25), 5, 5);
    painter.setBrush(roadblock_ == Roadblock::None ? red : gold); painter.drawRoundedRect(QRectF(450, 84, 1020 * ratio, 25), 5, 5);
    label(painter, QRectF(1200, 40, 270, 35), QString("%1 / %2 HP").arg(qRound(hp_)).arg(qRound(maximumHp_)), 20, Qt::white, Qt::AlignRight | Qt::AlignVCenter);
    const int secondsLeft = std::max(0, static_cast<int>((battleDurationMs - (nowMs - startedMs_) + 999) / 1000));
    label(painter, QRectF(40, 35, 340, 38), QString("RAIDERS: %1").arg(viewers_), 21, cyan);
    label(painter, QRectF(40, 72, 340, 34), QString("FIELD UNITS: %1").arg(raiders_.size()), 16, QColor(170, 210, 220));
    label(painter, QRectF(40, 105, 340, 34), QString("TIME: %1:%2").arg(secondsLeft / 60).arg(secondsLeft % 60, 2, 10, QLatin1Char('0')), 19, Qt::white);

    if (nowMs < iceUntilMs_) {
        painter.setPen(QPen(QColor(70, 190, 255, 65), 2));
        for (int y = 0; y < 1080; y += 18) painter.drawLine(0, y, 1920, y);
        painter.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 80, 190, 25));
        label(painter, QRectF(40, 142, 360, 35), "ICE PATCH // BUG SPEED -50%", 17, QColor(100, 210, 255));
    }
    if (nowMs < firewallUntilMs_) {
        QRadialGradient fire(QPointF(1540, 600), 340);
        fire.setColorAt(0, QColor(255, 105, 0, 85)); fire.setColorAt(1, QColor(255, 30, 0, 0));
        painter.setPen(QPen(QColor(255, 105, 20, 180), 4)); painter.setBrush(fire);
        painter.drawEllipse(QPointF(1540, 600), 340, 340);
        label(painter, QRectF(1370, 935, 420, 35), "FIREWALL DAMAGE ACTIVE", 19, QColor(255, 130, 30), Qt::AlignCenter);
    }

    const Enemy *focused = enemyById(focusedEnemyId());
    const QPointF aim = focused ? focused->position : QPointF(1480, 520);
    const double pulse = 0.5 + 0.5 * std::sin(nowMs / 110.0);
    for (const Raider &raider : raiders_) {
        const bool active = nowMs < raider.activeUntilMs;
        paintRaider(painter, raider.position, raider.style, raider.color, aim, active, pulse);
        if (active && !raider.name.isEmpty())
            label(painter, QRectF(raider.position.x() - 38, raider.position.y() - 22, 110, 18), raider.name, 11, Qt::white, Qt::AlignCenter);
    }

    for (const Enemy &enemy : enemies_) {
        if (enemy.state == EnemyState::Pending || enemy.state == EnemyState::Dead) continue;
        const bool dying = enemy.state == EnemyState::Dying;
        const double death = dying ? std::clamp((nowMs - enemy.stateAtMs) / 820.0, 0.0, 1.0) : 0.0;
        painter.save();
        if (dying) {
            painter.setOpacity(1.0 - death);
            painter.translate((death - 0.5) * 16, -death * 45);
            painter.scale(1.0 + death * 0.7, std::max(0.08, 1.0 - death));
        }
        const QColor enemyColor = nowMs < iceUntilMs_ ? QColor(70, 190, 255)
            : nowMs < enemy.flashUntilMs ? Qt::white
            : enemy.kind == 3 ? QColor(190, 85, 255) : enemy.kind == 4 ? QColor(255, 145, 35) : red;
        const QPointF bob(0, dying ? 0 : std::sin(nowMs / 210.0 + enemy.id) * 5);
        paintEnemyShape(painter, enemy.kind, enemy.position + bob, enemyColor, enemy.visualSeed, nowMs / 160.0);
        painter.restore();
        if (!dying) {
            const bool isTarget = focused && focused->id == enemy.id;
            if (isTarget) {
                painter.setPen(QPen(gold, 3)); painter.setBrush(Qt::NoBrush);
                const QRectF targetBox(enemy.position.x() - 72, enemy.position.y() - 70, 144, 140);
                painter.drawLine(targetBox.topLeft(), targetBox.topLeft() + QPointF(26, 0)); painter.drawLine(targetBox.topLeft(), targetBox.topLeft() + QPointF(0, 26));
                painter.drawLine(targetBox.bottomRight(), targetBox.bottomRight() - QPointF(26, 0)); painter.drawLine(targetBox.bottomRight(), targetBox.bottomRight() - QPointF(0, 26));
                label(painter, QRectF(enemy.position.x() - 70, enemy.position.y() - 96, 140, 22), "FOCUS TARGET", 13, gold, Qt::AlignCenter);
            }
            label(painter, QRectF(enemy.position.x() - 90, enemy.position.y() + 54, 180, 23), enemy.label, 13, enemyColor, Qt::AlignCenter);
            const double enemyRatio = enemy.maximumHp > 0 ? enemy.hp / enemy.maximumHp : 0;
            painter.setPen(Qt::NoPen); painter.setBrush(QColor(16, 22, 34, 230)); painter.drawRect(QRectF(enemy.position.x() - 58, enemy.position.y() + 79, 116, 7));
            painter.setBrush(enemyColor); painter.drawRect(QRectF(enemy.position.x() - 58, enemy.position.y() + 79, 116 * enemyRatio, 7));
        }
    }

    for (const Impact &impact : impacts_) {
        const qreal duration = impact.death ? 900.0 : 300.0;
        const qreal progress = std::clamp((nowMs - impact.startedMs) / duration, 0.0, 1.0);
        QRandomGenerator random(impact.seed);
        painter.setOpacity(1.0 - progress);
        painter.setPen(QPen(impact.color, impact.death ? 5 : 3, Qt::SolidLine, Qt::RoundCap));
        const int particles = impact.death ? 18 : 7;
        for (int i = 0; i < particles; ++i) {
            const double angle = random.generateDouble() * 6.283185307179586;
            const double reach = (20 + random.generateDouble() * (impact.death ? 115 : 42)) * progress;
            const QPointF end = impact.position + QPointF(std::cos(angle) * reach, std::sin(angle) * reach);
            painter.drawLine(end, end + QPointF(std::cos(angle) * 8, std::sin(angle) * 8));
        }
        if (impact.death) label(painter, QRectF(impact.position.x() - 100, impact.position.y() - 30 - progress * 60, 200, 40), "PROCESS KILLED", 17, impact.color, Qt::AlignCenter);
        painter.setOpacity(1.0);
    }

    for (const Projectile &projectile : projectiles_) {
        const Enemy *target = enemyById(projectile.enemyId);
        const QPointF direction = (target ? target->position : aim) - projectile.position;
        const double length = std::max(1.0, std::hypot(direction.x(), direction.y()));
        const QPointF tail = projectile.position - direction / length * (projectile.weapon == "Railgun" ? 90 : 32);
        painter.setPen(QPen(projectile.color, projectile.weapon == "Ban Hammer" ? 9 : 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(tail, projectile.position); painter.setBrush(projectile.color); painter.setPen(Qt::NoPen);
        painter.drawEllipse(projectile.position, projectile.weapon == "Ban Hammer" ? 10 : 6, projectile.weapon == "Ban Hammer" ? 10 : 6);
    }

    if (roadblock_ != Roadblock::None) {
        const double roadPulse = 0.75 + 0.25 * std::sin((nowMs - roadblockStartedMs_) / 170.0);
        painter.setOpacity(roadPulse); painter.setPen(QPen(red, 4)); painter.setBrush(QColor(32, 5, 12, 245));
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
