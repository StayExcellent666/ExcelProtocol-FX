#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <functional>

class QPainter;

namespace fx {

class RaidBattle {
public:
    enum class Phase { Idle, Fighting, Results };
    enum class Roadblock { None, InfiniteLoop, MergeConflict };

    bool start(int viewers, qint64 nowMs, bool testMode = false);
    QString chat(const QString &username, const QString &role, const QString &message, qint64 nowMs);
    bool finish(bool victory, qint64 nowMs);
    void clear();
    bool tick(qint64 nowMs);
    void paint(QPainter &painter, qint64 nowMs) const;

    bool visible() const { return phase_ != Phase::Idle; }
    bool fighting() const { return phase_ == Phase::Fighting; }
    bool testMode() const { return testMode_; }
    QJsonObject status(qint64 nowMs) const;

    std::function<void(const QString &)> onSound;

private:
    enum class EnemyState { Pending, Entering, Active, Dying, Dead };
    struct Raider {
        QString name;
        QString weapon;
        QPointF position;
        QColor color;
        int style = 0;
        qint64 activeUntilMs = 0;
    };
    struct Projectile {
        QString username;
        QString weapon;
        QPointF position;
        int enemyId = -1;
        double damage = 0;
        QColor color;
    };
    struct Enemy {
        int id = -1;
        int kind = 0;
        QString label;
        QPointF position;
        QPointF destination;
        double maximumHp = 0;
        double hp = 0;
        qint64 spawnAtMs = 0;
        qint64 stateAtMs = 0;
        qint64 flashUntilMs = 0;
        quint32 visualSeed = 0;
        EnemyState state = EnemyState::Pending;
    };
    struct Impact {
        QPointF position;
        QColor color;
        qint64 startedMs = 0;
        bool death = false;
        quint32 seed = 0;
    };
    struct Score {
        QString username;
        int damage = 0;
    };
    struct RecentAttack {
        QString username;
        QString weapon;
        int damage = 0;
        qint64 startedMs = 0;
    };

    void deployRoadblock(qint64 nowMs);
    void applyDamage(const QString &username, int enemyId, double damage, qint64 nowMs);
    void spawnAttack(const QString &username, const QString &weapon, int damage, qint64 nowMs);
    void driveTestRaid(qint64 nowMs);
    void buildLeaderboard();
    void updateEnemies(qint64 nowMs, double seconds, double speed);
    int focusedEnemyId() const;
    Enemy *enemyById(int id);
    const Enemy *enemyById(int id) const;
    double damageMultiplier() const;
    static QString safeName(QString value);
    static QString phaseName(Phase phase);
    static QString roadblockName(Roadblock roadblock);

    Phase phase_ = Phase::Idle;
    Roadblock roadblock_ = Roadblock::None;
    int viewers_ = 0;
    double maximumHp_ = 0;
    double hp_ = 0;
    qint64 startedMs_ = 0;
    qint64 finishedMs_ = 0;
    qint64 lastTickMs_ = 0;
    qint64 roadblockStartedMs_ = 0;
    qint64 iceUntilMs_ = 0;
    qint64 firewallUntilMs_ = 0;
    qint64 lastTestAttackMs_ = 0;
    qint64 lastShotSoundMs_ = -1000;
    qint64 lastHitSoundMs_ = -1000;
    qint64 lastSpawnSoundMs_ = -1000;
    bool roadblockDeployed_ = false;
    bool victory_ = false;
    bool testMode_ = false;
    quint32 seed_ = 0;
    quint32 sequence_ = 0;
    int interactionCount_ = 0;
    int defeatedEnemies_ = 0;
    int nextRaiderSlot_ = 0;
    QHash<QString, int> raiderSlots_;
    QHash<QString, qint64> lastAttackMs_;
    QHash<QString, int> damageLedger_;
    QVector<Raider> raiders_;
    QVector<Projectile> projectiles_;
    QVector<Enemy> enemies_;
    QVector<Impact> impacts_;
    QVector<Score> leaderboard_;
    QVector<RecentAttack> recentAttacks_;
};

} // namespace fx
