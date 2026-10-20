#include "effects.hpp"

#include <obs-module.h>
#include <util/bmem.h>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScreen>
#include <QThread>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fx {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int maxDuration = 30000;
constexpr int maxQueue = 16;
constexpr int emergencyHotkeyId = 0x4558;
double unit(QRandomGenerator &random) { return random.generateDouble(); }
double between(QRandomGenerator &random, double low, double high) { return low + unit(random) * (high - low); }
double clamp(double value, double low, double high) { return std::clamp(value, low, high); }

QColor cyan(int alpha = 255) { return QColor(33, 231, 245, alpha); }

void text(QPainter &painter, const QRectF &rect, const QString &value, int size,
          QColor color = Qt::white, int flags = Qt::AlignLeft | Qt::AlignVCenter)
{
    QFont font("Segoe UI", size);
    font.setPixelSize(size);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, flags, value);
}

void paintEffect(QPainter &p, const ActiveEffect &effect, qint64 now, double master)
{
    QRandomGenerator random(effect.seed);
    const double seconds = static_cast<double>(now - effect.startedMs) / 1000.0;
    const double progress = clamp(static_cast<double>(now - effect.startedMs) / effect.durationMs, 0, 1);
    const double fade = clamp((1.0 - progress) * 5, 0, 1);
    const double strength = clamp(effect.intensity * master, 0, 1);
    p.save();
    p.setOpacity(fade);
    p.setPen(Qt::NoPen);

    if (effect.id == "crash") {
        const int style = std::clamp(effect.parameters.value("style").toInt(0), 0, 2);
        p.setOpacity(fade * clamp(strength + 0.3, 0, 1));
        const QRectF panel = style == 1 ? QRectF(310, 285, 1300, 500) : QRectF(0, 0, 1920, 1080);
        p.fillRect(panel, style == 2 ? QColor(9, 16, 19) : QColor(1, 65, 89));
        const QStringList errors{"SKILL_ISSUE_DETECTED", "AIM_ASSIST_NOT_FOUND", "BRAIN.exe HAS STOPPED WORKING",
                                 "CRITICAL GAMER FAILURE", "USER.EXCELLENT NOT RESPONDING"};
        const QString error = errors.at(static_cast<int>(effect.seed % static_cast<quint32>(errors.size())));
        const qreal left = style == 1 ? 370 : 175;
        const qreal top = style == 1 ? 325 : 205;
        text(p, QRectF(left, top, 1300, 75), style == 2 ? ">_ RECOVERY PROTOCOL" : ":(  Excellent.exe", 62, cyan());
        text(p, QRectF(left, top + 100, 1400, 65), "Excellent.exe has stopped responding.", 38);
        text(p, QRectF(left, top + 185, 1400, 55), error, 28, cyan());
        text(p, QRectF(left, top + 250, 1400, 55), QString("Restoring gamer confidence... %1%").arg(static_cast<int>(progress * 100)), 26);
        text(p, QRectF(left, top + 320, 1400, 50), "EXCELPROTOCOL FX  /  FICTIONAL EFFECT  /  AUTO RECOVERY", 18, QColor(152, 209, 218));
        p.fillRect(QRectF(left, top + 390, 1050 * progress, 5), cyan());
    } else if (effect.id == "ink_splatter") {
        const int count = std::clamp(effect.parameters.value("splatters").toInt(3 + static_cast<int>(strength * 8)), 1, 12);
        for (int i = 0; i < count; ++i) {
            const QPointF center(between(random, 100, 1820), between(random, 100, 980));
            const double radius = between(random, 95, 245) * (0.6 + strength * 0.6);
            const double rotation = between(random, 0, 360);
            const double age = seconds - i * 0.15;
            QPainterPath shape;
            for (int j = 0; j <= 28; ++j) {
                const double angle = j * 2 * pi / 28;
                const double r = radius * between(random, 0.6, 1.1);
                const QPointF point(std::cos(angle) * r, std::sin(angle) * r);
                if (j == 0) shape.moveTo(point); else shape.lineTo(point);
            }
            shape.closeSubpath();
            p.save();
            p.translate(center);
            p.rotate(rotation);
            const double growth = clamp(age * 5, 0, 1);
            p.scale(growth, growth);
            p.setBrush(QColor(4, 13, 23, static_cast<int>(170 + strength * 85)));
            p.drawPath(shape);
            for (int j = 0; j < 13; ++j) {
                const double angle = between(random, 0, 2 * pi), distance = radius * between(random, 1, 1.6);
                const double blob = between(random, 5, 22);
                p.drawEllipse(QPointF(std::cos(angle) * distance, std::sin(angle) * distance), blob, blob * 0.75);
            }
            p.restore();
            if (age > 0 && effect.parameters.value("drips").toBool(true)) {
                p.setPen(QPen(QColor(4, 13, 23, 195), 9, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(center + QPointF(radius * 0.3, radius * 0.6),
                           center + QPointF(radius * 0.3, radius * 0.6 + std::min(age * 18, 125.0)));
                p.setPen(Qt::NoPen);
            }
        }
    } else if (effect.id == "screen_cracks") {
        const QPointF center(between(random, 500, 1420), between(random, 260, 820));
        for (int ray = 0; ray < 18; ++ray) {
            QPointF previous = center;
            const double angle = ray * 2 * pi / 18 + between(random, -0.1, 0.1);
            for (int segment = 1; segment <= 6; ++segment) {
                const double distance = segment * between(random, 50, 95) * (0.4 + strength);
                const QPointF next = center + QPointF(std::cos(angle) * distance + between(random, -18, 18),
                                                     std::sin(angle) * distance + between(random, -18, 18));
                p.setPen(QPen(QColor(0, 0, 0, 100), 4));
                p.drawLine(previous + QPointF(2, 2), next + QPointF(2, 2));
                p.setPen(QPen(QColor(211, 245, 255, 220), 1.6));
                p.drawLine(previous, next);
                if (segment % 2 == 0) p.drawLine(next, next + QPointF(std::cos(angle + 0.8) * 75, std::sin(angle + 0.8) * 75));
                previous = next;
            }
        }
    } else if (effect.id == "error_invasion") {
        const QStringList messages{"Your gaming license has been revoked.", "Unexpected lack of skill.", "Please reinstall your reflexes.",
                                   "Aim calibration: no aim detected.", "Cannot locate victory.dll."};
        const int count = 3 + static_cast<int>(strength * 10);
        for (int i = 0; i < count; ++i) {
            const double x = between(random, 35, 1415), y = between(random, 35, 810);
            if (seconds < i * 0.32) continue;
            p.setPen(QPen(cyan(), 2));
            p.setBrush(QColor(22, 27, 37, 245));
            p.drawRoundedRect(QRectF(x, y, 470, 225), 8, 8);
            p.fillRect(QRectF(x + 1, y + 1, 468, 40), QColor(0, 87, 107));
            text(p, QRectF(x + 15, y + 3, 425, 35), "Excellent.exe  -  Fictional error", 20);
            text(p, QRectF(x + 22, y + 63, 426, 85), messages.at(i % messages.size()), 25, Qt::white, Qt::AlignLeft | Qt::TextWordWrap);
            p.setPen(QPen(cyan(), 1));
            p.setBrush(QColor(0, 61, 76));
            p.drawRoundedRect(QRectF(x + 310, y + 164, 135, 40), 4, 4);
            text(p, QRectF(x + 310, y + 164, 135, 40), "Recovering...", 17, cyan(), Qt::AlignCenter);
        }
    } else if (effect.id == "corruption") {
        // Localized artifacts only: never sample or modify game pixels, and no full-screen flashes.
        for (int i = 0; i < 20; ++i) {
            const double x = between(random, 0, 1800), y = between(random, 0, 1040);
            const double width = between(random, 60, 400), height = between(random, 3, 24);
            const double drift = std::sin(seconds * 2 + i) * 45;
            p.fillRect(QRectF(x + drift, y, width, height), QColor(i % 2 ? 255 : 5, i % 2 ? 25 : 230, 230, static_cast<int>(strength * 115)));
        }
        p.setPen(QPen(QColor(0, 170, 190, static_cast<int>(strength * 65)), 1));
        for (int y = 0; y < 1080; y += 9) p.drawLine(0, y, 1920, y);
        text(p, QRectF(65, 55, 1200, 50), "0xEXCELLENT // SIGNAL INTEGRITY DEGRADED", 24, cyan(190));
    } else if (effect.id == "darkness") {
        QRadialGradient gradient(QPointF(960 + std::sin(seconds * 0.4) * 120, 540), 1000);
        gradient.setColorAt(0, QColor(0, 0, 0, 0));
        gradient.setColorAt(0.25, QColor(0, 0, 0, 15));
        gradient.setColorAt(1, QColor(0, 3, 9, static_cast<int>(80 + 170 * strength)));
        p.fillRect(QRectF(0, 0, 1920, 1080), gradient);
    } else if (effect.id == "creepy_eyes") {
        for (int i = 0; i < 6; ++i) {
            double x = between(random, 80, 1720), y = i % 2 ? between(random, 860, 1000) : between(random, 60, 180);
            x += std::sin(seconds + i) * 14;
            const double blink = std::pow(std::max(0.0, std::sin(seconds * 0.8 + i * 1.7)), 12);
            const double open = 1 - blink * 0.95;
            for (int eye = 0; eye < 2; ++eye) {
                const QPointF center(x + eye * 65, y);
                QRadialGradient glow(center, 50);
                glow.setColorAt(0, cyan(static_cast<int>(180 * strength)));
                glow.setColorAt(1, cyan(0));
                p.setBrush(glow); p.setPen(Qt::NoPen); p.drawEllipse(center, 50, 40 * open);
                p.setBrush(cyan()); p.drawEllipse(center, 20, 11 * open);
                p.setBrush(QColor(2, 14, 18)); p.drawEllipse(center, 5, 10 * open);
            }
        }
    } else if (effect.id == "fog") {
        for (int i = 0; i < 18; ++i) {
            const QPointF center(between(random, -100, 2000) + std::sin(seconds * 0.25 + i) * 75,
                                 between(random, -100, 1180) + std::cos(seconds * 0.2 + i) * 30);
            const double radius = between(random, 180, 450);
            QRadialGradient fog(center, radius);
            fog.setColorAt(0, QColor(191, 223, 226, static_cast<int>(strength * 75)));
            fog.setColorAt(1, QColor(191, 223, 226, 0));
            p.setBrush(fog); p.setPen(Qt::NoPen); p.drawEllipse(center, radius, radius * 0.8);
        }
        for (int i = 0; i < 40; ++i) {
            const QPointF center(between(random, 0, 1920), between(random, 0, 1080));
            p.setPen(QPen(QColor(215, 244, 247, static_cast<int>(strength * 100)), 1.5));
            p.setBrush(QColor(203, 227, 230, 18));
            p.drawEllipse(center, 4, 6);
        }
    } else if (effect.id == "mouse_invasion") {
        for (int i = 0; i < 8 + static_cast<int>(strength * 25); ++i) {
            const double x = between(random, 100, 1820), y = between(random, 100, 980), phase = between(random, 0, 2 * pi);
            p.save(); p.translate(x + std::sin(seconds * 1.4 + phase) * 90, y + std::cos(seconds * 1.1 + phase) * 75);
            const double size = between(random, 0.8, 1.6); p.scale(size, size);
            QPainterPath cursor; cursor.moveTo(0, 0); cursor.lineTo(0, 40); cursor.lineTo(11, 30);
            cursor.lineTo(20, 48); cursor.lineTo(28, 44); cursor.lineTo(19, 26); cursor.lineTo(34, 26); cursor.closeSubpath();
            p.setPen(QPen(QColor(10, 22, 30), 2)); p.setBrush(i % 3 ? QColor(235, 249, 255) : cyan()); p.drawPath(cursor); p.restore();
        }
    } else if (effect.id == "jumpscare_lite") {
        const double appearance = clamp(seconds * 7, 0, 1);
        p.setOpacity(fade * appearance * clamp(strength + 0.2, 0, 1));
        p.translate(960, 540);
        p.scale(0.75 + appearance * 0.25, 0.75 + appearance * 0.25);
        p.setBrush(QColor(5, 15, 24, 240)); p.setPen(QPen(cyan(), 8));
        p.drawRoundedRect(QRectF(-255, -240, 510, 470), 115, 115);
        p.setBrush(cyan()); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(-105, -70), 60, 35); p.drawEllipse(QPointF(105, -70), 60, 35);
        p.setPen(QPen(cyan(), 13, Qt::SolidLine, Qt::RoundCap)); p.drawArc(QRectF(-130, -10, 260, 150), 180 * 16, 180 * 16);
        text(p, QRectF(-300, 260, 600, 60), "BOO.exe", 42, cyan(), Qt::AlignCenter);
    }
    p.restore();
}
} // namespace

class DesktopOverlay final : public QWidget {
public:
    explicit DesktopOverlay(std::function<void()> clear)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                              Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus), clear_(std::move(clear))
    {
        setObjectName("ExcelProtocolFXDesktopOverlay");
        setWindowTitle("ExcelProtocol FX Overlay");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
#ifdef _WIN32
        const auto hwnd = reinterpret_cast<HWND>(winId());
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        hotkeyAvailable_ = RegisterHotKey(hwnd, emergencyHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F12) != 0;
#endif
    }
    ~DesktopOverlay() override
    {
#ifdef _WIN32
        if (hotkeyAvailable_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), emergencyHotkeyId);
#endif
    }
    void setFrame(const QImage &frame) { image_ = frame; update(); }
    bool hotkeyAvailable() const { return hotkeyAvailable_; }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        if (!image_.isNull()) painter.drawImage(rect(), image_);
    }
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override
    {
#ifdef _WIN32
        const auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == emergencyHotkeyId) { clear_(); *result = 0; return true; }
        if (msg->message == WM_NCHITTEST) { *result = HTTRANSPARENT; return true; }
        if (msg->message == WM_MOUSEACTIVATE) { *result = MA_NOACTIVATE; return true; }
#endif
        return QWidget::nativeEvent(type, message, result);
    }
private:
    QImage image_;
    std::function<void()> clear_;
    bool hotkeyAvailable_ = false;
};

std::shared_ptr<FrameState> sharedFrame()
{
    static const auto state = std::make_shared<FrameState>();
    return state;
}

QString settingsPath()
{
#ifdef FX_TESTING
    return QDir::tempPath() + "/excelprotocol-fx-test-" + QString::number(QCoreApplication::applicationPid()) + ".ini";
#else
    char *path = obs_module_config_path("settings.ini");
    const QString value = QString::fromUtf8(path ? path : "");
    bfree(path);
    if (!value.isEmpty()) QDir().mkpath(QFileInfo(value).absolutePath());
    return value;
#endif
}

QImage renderEffects(const QSize &size, const QVector<ActiveEffect> &effects, qint64 nowMs, double intensity)
{
    if (effects.isEmpty() || size.isEmpty()) return {};
    // BGRA byte order on Windows, straight alpha for OBS's default source shader.
    QImage image(size, QImage::Format_ARGB32);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / 1920.0, size.height() / 1080.0);
    for (const auto &effect : effects) paintEffect(painter, effect, nowMs, intensity);
    return image;
}

EffectManager::EffectManager(QObject *parent)
    : QObject(parent), settings_(settingsPath(), QSettings::IniFormat), frame_(sharedFrame())
{
    definitions_ = {
        {"crash", "Excellent.exe Crashed", "Fictional crash with automatic recovery.", "System", 5000, 1000, 15000, 15000, 0.65, 1, true, true, {{"style", 0}}},
        {"ink_splatter", "Ink Splatter", "Seeded organic ink shapes and gradual drips.", "Obstruction", 8000, 1000, 30000, 10000, 0.65, 1, true, false, {{"splatters", 5}, {"drips", true}}},
        {"screen_cracks", "Screen Cracks", "Simulated glass impact and fading fracture lines.", "Surface", 7000},
        {"error_invasion", "Error Window Invasion", "Fictional error windows recover automatically.", "System", 7000},
        {"corruption", "System Corruption", "Localized digital artifacts, scanlines, and text.", "System", 6000},
        {"darkness", "Darkness Protocol", "Animated transparent edge shadows.", "Atmosphere", 8000},
        {"creepy_eyes", "Creepy Eyes", "Blinking neon cyan eyes near the screen edges.", "Atmosphere", 7000},
        {"fog", "Fogged Screen", "Drifting translucent fog and condensation droplets.", "Atmosphere", 8000},
        {"mouse_invasion", "Fake Mouse Invasion", "Visual cursors only; never controls your mouse.", "Playful", 6000},
        {"jumpscare_lite", "Jumpscare Lite", "Brief playful cyan face; no flashes or sound.", "Playful", 1500, 500, 4000, 15000, 0.5, 0.5, true, true, {}},
    };
    clock_.start();
    overlay_ = std::make_unique<DesktopOverlay>([this] { clear(); });
    loadSettings();
    timer_.setInterval(33);
    timer_.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] {
        try { tick(); } catch (...) { clear(); blog(LOG_ERROR, "[ExcelProtocol FX] Renderer failure: all effects cleared"); }
    });
    auto watchScreen = [this](QScreen *screen) {
        QObject::connect(screen, &QScreen::geometryChanged, this, [this] { applyDisplaySettings(); });
        QObject::connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] { applyDisplaySettings(); });
    };
    for (auto *screen : QGuiApplication::screens()) watchScreen(screen);
    QObject::connect(qApp, &QGuiApplication::screenAdded, this, [this, watchScreen](QScreen *screen) { watchScreen(screen); applyDisplaySettings(); });
    QObject::connect(qApp, &QGuiApplication::screenRemoved, this, [this] { clear(); applyDisplaySettings(); });
    applyDisplaySettings();
}

EffectManager::~EffectManager() { shutdown(); }

void EffectManager::loadSettings()
{
    enabled_ = settings_.value("effects/enabled", true).toBool();
    desktopEnabled_ = settings_.value("display/desktop", true).toBool();
    streamEnabled_ = settings_.value("display/stream", true).toBool();
    overallIntensity_ = clamp(settings_.value("effects/intensity", 0.7).toDouble(), 0.05, 1);
    maximumSimultaneous_ = std::clamp(settings_.value("effects/maxSimultaneous", 3).toInt(), 1, 4);
    monitorName_ = settings_.value("display/monitor").toString();
    for (auto &effect : definitions_) {
        const QString prefix = "library/" + effect.id + "/";
        effect.enabled = settings_.value(prefix + "enabled", effect.enabled).toBool();
        effect.durationMs = std::clamp(settings_.value(prefix + "durationMs", effect.durationMs).toInt(), effect.minDurationMs, effect.maxDurationMs);
        effect.cooldownMs = std::clamp(settings_.value(prefix + "cooldownMs", effect.cooldownMs).toInt(), 1000, 600000);
        effect.intensity = clamp(settings_.value(prefix + "intensity", effect.intensity).toDouble(), 0.05, 1);
        effect.weight = clamp(settings_.value(prefix + "weight", effect.weight).toDouble(), 0, 100);
        for (auto it = effect.parameters.begin(); it != effect.parameters.end(); ++it)
            it.value() = QJsonValue::fromVariant(settings_.value(prefix + it.key(), it.value().toVariant()));
    }
}

void EffectManager::saveSettings()
{
    settings_.setValue("effects/enabled", enabled_);
    settings_.setValue("display/desktop", desktopEnabled_);
    settings_.setValue("display/stream", streamEnabled_);
    settings_.setValue("display/monitor", monitorName_);
    settings_.setValue("effects/intensity", overallIntensity_);
    settings_.setValue("effects/maxSimultaneous", maximumSimultaneous_);
    for (const auto &effect : definitions_) {
        const QString prefix = "library/" + effect.id + "/";
        settings_.setValue(prefix + "enabled", effect.enabled);
        settings_.setValue(prefix + "durationMs", effect.durationMs);
        settings_.setValue(prefix + "cooldownMs", effect.cooldownMs);
        settings_.setValue(prefix + "intensity", effect.intensity);
        settings_.setValue(prefix + "weight", effect.weight);
        for (auto it = effect.parameters.begin(); it != effect.parameters.end(); ++it) settings_.setValue(prefix + it.key(), it.value().toVariant());
    }
    settings_.sync();
}

EffectDefinition *EffectManager::definition(const QString &id)
{
    for (auto &value : definitions_) if (value.id == id) return &value;
    return nullptr;
}

bool EffectManager::compatible(const EffectDefinition &candidate) const
{
    if (candidate.exclusive && !active_.isEmpty()) return false;
    for (const auto &active : active_)
        for (const auto &def : definitions_)
            if (active.id == candidate.id || (def.id == active.id && def.exclusive)) return false;
    return true;
}

QString EffectManager::result(const QString &id, const QString &message, const QString &origin)
{
    // Never log command content, viewer names, or credentials.
    blog(LOG_INFO, "[ExcelProtocol FX] Effect %s: %s (%s)", id.toUtf8().constData(), message.toUtf8().constData(), origin.toUtf8().constData());
    if (onResult) onResult(message);
    return message;
}

QString EffectManager::trigger(const QString &id, int durationMs, double intensity, const QJsonObject &parameters, const QString &origin)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (stopped_ || !enabled_) return result(id, "disabled", origin);
    auto *def = definition(id);
    if (!def) return result(id, "unknown_effect", origin);
    if (!def->enabled) return result(id, "effect_disabled", origin);
    if (active_.size() >= maximumSimultaneous_) return result(id, "busy", origin);
    if (!compatible(*def)) return result(id, "conflict", origin);
    const qint64 now = clock_.elapsed();
    if (lastTriggered_.contains(id) && now - lastTriggered_.value(id) < def->cooldownMs) return result(id, "cooldown", origin);
    if (durationMs != -1 && (durationMs < def->minDurationMs || durationMs > def->maxDurationMs || durationMs > maxDuration))
        return result(id, "invalid_duration", origin);
    if (!std::isfinite(intensity) || (intensity != -1 && (intensity < 0.05 || intensity > 1))) return result(id, "invalid_intensity", origin);
    const double chosenIntensity = intensity == -1 ? def->intensity : intensity;
    double combinedIntensity = chosenIntensity;
    for (const auto &active : active_) combinedIntensity += active.intensity;
    const double maximumCombined = clamp(settings_.value("chaos/maxCombinedIntensity", 1.8).toDouble(), 0.1, 3.0);
    if (combinedIntensity > maximumCombined) return result(id, "intensity_limit", origin);
    QJsonObject selected = def->parameters;
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        if (!selected.contains(it.key())) return result(id, "invalid_parameter", origin);
        if (it.key() == "splatters" && (!it->isDouble() || it->toDouble() != it->toInt() || it->toInt() < 1 || it->toInt() > 12)) return result(id, "invalid_parameter", origin);
        if (it.key() == "style" && (!it->isDouble() || it->toDouble() != it->toInt() || it->toInt() < 0 || it->toInt() > 2)) return result(id, "invalid_parameter", origin);
        if (it.key() == "drips" && !it->isBool()) return result(id, "invalid_parameter", origin);
        selected.insert(it.key(), it.value());
    }
    active_.push_back({id, QRandomGenerator::global()->generate(), now,
                       durationMs == -1 ? def->durationMs : durationMs, chosenIntensity, selected});
    lastTriggered_.insert(id, now);
    timer_.start(); tick(); notifyChanged();
    return result(id, "triggered", origin);
}

QString EffectManager::triggerRandom(const QString &origin)
{
    QVector<const EffectDefinition *> choices;
    double total = 0;
    for (const auto &def : definitions_) {
        if (!def.enabled || def.weight <= 0 || !compatible(def)) continue;
        if (lastTriggered_.contains(def.id) && clock_.elapsed() - lastTriggered_.value(def.id) < def.cooldownMs) continue;
        if (def.id == previousRandom_ && definitions_.size() > 1) continue;
        choices.push_back(&def); total += def.weight;
    }
    if (choices.isEmpty()) return result("random", "no_available_effect", origin);
    double choice = QRandomGenerator::global()->generateDouble() * total;
    for (const auto *def : choices) {
        choice -= def->weight;
        if (choice <= 0) { const auto response = trigger(def->id, -1, -1, {}, origin); if (response == "triggered") previousRandom_ = def->id; return response; }
    }
    return "no_available_effect";
}

QString EffectManager::triggerChaos(const QString &origin)
{
    if (!enabled_ || stopped_) return result("chaos", "disabled", origin);
    const int maximum = std::clamp(settings_.value("chaos/maximum", 3).toInt(), 1, maximumSimultaneous_);
    const int minimum = std::clamp(settings_.value("chaos/minimum", 2).toInt(), 1, maximum);
    const int count = QRandomGenerator::global()->bounded(minimum, maximum + 1);
    const int duration = std::clamp(settings_.value("chaos/durationMs", 7000).toInt(), 1000, 15000);
    int triggered = 0;
    QVector<const EffectDefinition *> choices;
    for (const auto &def : definitions_) {
        if (def.enabled && !def.exclusive && def.weight > 0 && compatible(def) &&
            settings_.value("chaos/allowed/" + def.id, true).toBool() &&
            (!lastTriggered_.contains(def.id) || clock_.elapsed() - lastTriggered_.value(def.id) >= def.cooldownMs)) choices.push_back(&def);
    }
    while (triggered < count && !choices.isEmpty()) {
        double total = 0; for (const auto *def : choices) total += def->weight;
        double weight = QRandomGenerator::global()->generateDouble() * total;
        int selected = 0;
        while (selected + 1 < choices.size() && (weight -= choices[selected]->weight) > 0) ++selected;
        const auto *def = choices.takeAt(selected);
        if (trigger(def->id, std::min(duration, def->maxDurationMs), std::min(def->intensity, 0.5), {}, origin) == "triggered") ++triggered;
    }
    return result("chaos", triggered ? QString("triggered_%1").arg(triggered) : "no_available_effect", origin);
}

void EffectManager::publish(const QImage &image)
{
    std::lock_guard<std::mutex> guard(frame_->mutex);
    frame_->image = streamEnabled_ ? image : QImage();
    ++frame_->serial;
}

void EffectManager::tick()
{
    const qint64 now = clock_.elapsed();
    const auto oldSize = active_.size();
    active_.erase(std::remove_if(active_.begin(), active_.end(), [now](const ActiveEffect &effect) {
        return now - effect.startedMs >= std::min(effect.durationMs, maxDuration);
    }), active_.end());
    if (active_.isEmpty()) {
        timer_.stop(); overlay_->hide(); overlay_->setFrame({}); publish({});
        if (!queue_.isEmpty()) {
            const auto next = queue_.dequeue();
            trigger(next.value("effect").toString(), next.value("duration_ms").toInt(-1), next.value("intensity").toDouble(-1), next.value("parameters").toObject(), "queue");
        }
    } else {
        const QImage image = renderEffects(renderSize_, active_, now, overallIntensity_);
        if (image.isNull()) { clear(); return; }
        publish(image);
        if (desktopEnabled_) { overlay_->setFrame(image); if (!overlay_->isVisible()) overlay_->show(); }
        else overlay_->hide();
    }
    if (active_.size() != oldSize) notifyChanged();
}

void EffectManager::clear()
{
    active_.clear(); queue_.clear(); timer_.stop();
    if (overlay_) { overlay_->hide(); overlay_->setFrame({}); }
    publish({}); notifyChanged();
}

void EffectManager::cancel(const QString &id)
{
    active_.erase(std::remove_if(active_.begin(), active_.end(), [&id](const auto &effect) { return effect.id == id; }), active_.end());
    for (qsizetype i = queue_.size(); i > 0; --i) if (queue_[i - 1].value("effect").toString() == id) queue_.removeAt(i - 1);
    tick(); notifyChanged();
}

void EffectManager::shutdown()
{
    if (stopped_) return;
    stopped_ = true; clear(); saveSettings(); overlay_.reset();
}

void EffectManager::notifyChanged() { if (onChanged) onChanged(); }
void EffectManager::setEnabled(bool value) { enabled_ = value; if (!value) clear(); saveSettings(); notifyChanged(); }
void EffectManager::setDesktopEnabled(bool value) { desktopEnabled_ = value; if (!active_.isEmpty()) tick(); else if (!value && overlay_) overlay_->hide(); saveSettings(); notifyChanged(); }
void EffectManager::setStreamEnabled(bool value) { streamEnabled_ = value; if (!active_.isEmpty()) tick(); else publish({}); saveSettings(); notifyChanged(); }
void EffectManager::setOverallIntensity(double value) { overallIntensity_ = clamp(value, 0.05, 1); saveSettings(); }
void EffectManager::setMaxSimultaneous(int value) { maximumSimultaneous_ = std::clamp(value, 1, 4); clear(); saveSettings(); }
void EffectManager::setMonitorName(const QString &value) { monitorName_ = value; clear(); applyDisplaySettings(); saveSettings(); }
bool EffectManager::emergencyHotkeyAvailable() const { return overlay_ && overlay_->hotkeyAvailable(); }

void EffectManager::applyDisplaySettings()
{
    if (!overlay_) return;
    auto *selected = QGuiApplication::primaryScreen();
    for (auto *screen : QGuiApplication::screens()) if (screen->name() == monitorName_) selected = screen;
    if (!selected) { clear(); return; }
    overlay_->setGeometry(selected->geometry());
    const QSize pixels = selected->geometry().size() * selected->devicePixelRatio();
    renderSize_ = pixels.scaled(QSize(2560, 1440), Qt::KeepAspectRatio);
    if (pixels.width() <= 2560 && pixels.height() <= 1440) renderSize_ = pixels;
    frame_->width.store(static_cast<unsigned>(renderSize_.width())); frame_->height.store(static_cast<unsigned>(renderSize_.height()));
    if (!active_.isEmpty()) tick();
    notifyChanged();
}

QString EffectManager::displayDescription() const
{
    return QString("Shared render surface: %1 x %2, 30 fps while active.\n%3 ExcelProtocol FX source(s) currently loaded.")
        .arg(renderSize_.width()).arg(renderSize_.height()).arg(frame_->sourceCount.load());
}

QJsonObject EffectManager::command(const QJsonObject &request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString type = request.value("type").toString();
    const QString eventId = request.value("event_id").toString();
    const bool mutation = type == "trigger_effect" || type == "trigger_random" || type == "clear_effects" || type == "cancel_effect";
    auto reply = [&](bool ok, const QString &status) { return QJsonObject{{"ok", ok}, {"status", status}, {"event_id", eventId}}; };
    if (mutation) {
        if (eventId.isEmpty() || eventId.size() > 128) return reply(false, "invalid_event_id");
        const qint64 now = clock_.elapsed();
        for (auto it = eventIds_.begin(); it != eventIds_.end();) { if (now - it.value() > 86400000) it = eventIds_.erase(it); else ++it; }
        if (eventIds_.contains(eventId)) return reply(false, "duplicate_event");
        if (eventIds_.size() >= 4096) return reply(false, "event_limit");
        eventIds_.insert(eventId, now);
    }
    if (type == "trigger_effect") {
        if (!request.value("effect").isString() || request.value("effect").toString().size() > 64) return reply(false, "invalid_effect");
        if (request.contains("duration_ms") && (!request.value("duration_ms").isDouble() || request.value("duration_ms").toDouble() != request.value("duration_ms").toInt() || request.value("duration_ms").toInt() < 0)) return reply(false, "invalid_duration");
        if (request.contains("intensity") && (!request.value("intensity").isDouble() || request.value("intensity").toDouble() < 0.05 || request.value("intensity").toDouble() > 1)) return reply(false, "invalid_intensity");
        if (request.contains("parameters") && !request.value("parameters").isObject()) return reply(false, "invalid_parameters");
        const QString status = trigger(request.value("effect").toString(), request.value("duration_ms").toInt(-1), request.value("intensity").toDouble(-1), request.value("parameters").toObject(), "integration");
        if ((status == "busy" || status == "conflict") && request.value("queue").toBool() && queue_.size() < maxQueue) {
            queue_.enqueue(request); return reply(true, "queued");
        }
        return reply(status == "triggered", status);
    }
    if (type == "trigger_random") { const auto status = triggerRandom("integration"); return reply(status == "triggered", status); }
    if (type == "clear_effects") { clear(); return reply(true, "cleared"); }
    if (type == "cancel_effect") { if (!definition(request.value("effect").toString())) return reply(false, "unknown_effect"); cancel(request.value("effect").toString()); return reply(true, "cancelled"); }
    if (type == "get_active_effects") {
        QJsonArray values;
        for (const auto &effect : active_) values.push_back(QJsonObject{{"effect", effect.id}, {"remaining_ms", std::max<qint64>(0, effect.durationMs - (clock_.elapsed() - effect.startedMs))}, {"intensity", effect.intensity}});
        return {{"ok", true}, {"active", values}};
    }
    if (type == "list_effects") {
        QJsonArray values;
        for (const auto &def : definitions_) values.push_back(QJsonObject{{"id", def.id}, {"name", def.name}, {"description", def.description}, {"category", def.category}, {"enabled", def.enabled}, {"duration_ms", def.durationMs}, {"min_duration_ms", def.minDurationMs}, {"max_duration_ms", def.maxDurationMs}, {"cooldown_ms", def.cooldownMs}, {"intensity", def.intensity}, {"weight", def.weight}, {"parameters", def.parameters}});
        return {{"ok", true}, {"effects", values}};
    }
    if (type == "get_status") return {{"ok", true}, {"enabled", enabled_}, {"active_count", static_cast<int>(active_.size())}, {"queued_count", static_cast<int>(queue_.size())}, {"desktop", desktopEnabled_}, {"stream", streamEnabled_}, {"source_count", frame_->sourceCount.load()}};
    return reply(false, "unknown_command");
}

} // namespace fx
