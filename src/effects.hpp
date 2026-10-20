#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QSettings>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

class QScreen;

namespace fx {

struct EffectDefinition {
    QString id, name, description, category;
    int durationMs = 5000;
    int minDurationMs = 500;
    int maxDurationMs = 30000;
    int cooldownMs = 10000;
    double intensity = 0.65;
    double weight = 1.0;
    bool enabled = true;
    bool exclusive = false;
    QJsonObject parameters;
};

struct ActiveEffect {
    QString id;
    quint32 seed = 0;
    qint64 startedMs = 0;
    int durationMs = 0;
    double intensity = 0;
    QJsonObject parameters;
};

// QImage copies are immutable after publication. OBS's graphics thread never
// touches a QWidget or the effect manager; it only takes this short lock.
struct FrameState {
    std::mutex mutex;
    QImage image;
    quint64 serial = 0;
    std::atomic_uint width{1920}, height{1080};
    std::atomic_int sourceCount{0};
};

std::shared_ptr<FrameState> sharedFrame();
QString settingsPath();
QImage renderEffects(const QSize &size, const QVector<ActiveEffect> &effects,
                     qint64 nowMs, double overallIntensity);

class DesktopOverlay;

class EffectManager : public QObject {
public:
    explicit EffectManager(QObject *parent = nullptr);
    ~EffectManager() override;

    QVector<EffectDefinition> &definitions() { return definitions_; }
    const QVector<EffectDefinition> &definitions() const { return definitions_; }
    const QVector<ActiveEffect> &active() const { return active_; }
    QSettings &settings() { return settings_; }
    QString trigger(const QString &id, int durationMs = -1, double intensity = -1,
                    const QJsonObject &parameters = {}, const QString &origin = "dock");
    QString triggerRandom(const QString &origin = "dock");
    QString triggerChaos(const QString &origin = "dock");
    QJsonObject command(const QJsonObject &request);
    void clear();
    void cancel(const QString &id);
    void shutdown();
    void saveSettings();
    void applyDisplaySettings();
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    bool desktopEnabled() const { return desktopEnabled_; }
    bool streamEnabled() const { return streamEnabled_; }
    void setDesktopEnabled(bool value);
    void setStreamEnabled(bool value);
    double overallIntensity() const { return overallIntensity_; }
    void setOverallIntensity(double value);
    int maxSimultaneous() const { return maximumSimultaneous_; }
    void setMaxSimultaneous(int value);
    QString monitorName() const { return monitorName_; }
    void setMonitorName(const QString &value);
    QString displayDescription() const;
    bool emergencyHotkeyAvailable() const;
    std::function<void()> onChanged;
    std::function<void(const QString &)> onResult;

private:
    void tick();
    void publish(const QImage &image);
    void notifyChanged();
    void loadSettings();
    EffectDefinition *definition(const QString &id);
    QString result(const QString &id, const QString &message, const QString &origin);
    bool compatible(const EffectDefinition &definition) const;

    QSettings settings_;
    QVector<EffectDefinition> definitions_;
    QVector<ActiveEffect> active_;
    QHash<QString, qint64> lastTriggered_;
    QHash<QString, qint64> eventIds_;
    QQueue<QJsonObject> queue_;
    QElapsedTimer clock_;
    QTimer timer_;
    std::unique_ptr<DesktopOverlay> overlay_;
    std::shared_ptr<FrameState> frame_;
    QSize renderSize_{1920, 1080};
    QString monitorName_;
    QString previousRandom_;
    bool enabled_ = true;
    bool desktopEnabled_ = true;
    bool streamEnabled_ = true;
    bool stopped_ = false;
    double overallIntensity_ = 0.7;
    int maximumSimultaneous_ = 3;
};

} // namespace fx
