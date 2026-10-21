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
    struct Warrior {
        QString name;
        QString weapon;
        QPointF position;
    };
    struct Projectile {
        QString username;
        QString weapon;
        QPointF position;
        QPointF target;
        double damage = 0;
        QColor color;
    };
    struct Enemy {
        QString label;
        QPointF position;
        QPointF velocity;
    };
    struct Score {
        QString username;
        int damage = 0;
    };

    void deployRoadblock(qint64 nowMs);
    void applyDamage(const QString &username, double damage, qint64 nowMs);
    void spawnAttack(const QString &username, const QString &weapon, int damage);
    void driveTestRaid(qint64 nowMs);
    void buildLeaderboard();
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
    bool roadblockDeployed_ = false;
    bool victory_ = false;
    bool testMode_ = false;
    quint32 seed_ = 0;
    quint32 sequence_ = 0;
    int interactionCount_ = 0;
    QHash<QString, Warrior> warriors_;
    QHash<QString, int> damageLedger_;
    QVector<Projectile> projectiles_;
    QVector<Enemy> enemies_;
    QVector<Score> leaderboard_;
};

} // namespace fx
