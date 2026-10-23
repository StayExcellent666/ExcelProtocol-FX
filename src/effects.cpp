#include "effects.hpp"
#include "raid-audio.hpp"

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

QJsonValue typedSetting(QSettings &settings, const QString &key, const QJsonValue &fallback)
{
    const QVariant stored = settings.value(key, fallback.toVariant());
    if (key.endsWith("/style") || key.endsWith("/splatters")) return stored.toInt();
    if (fallback.isBool()) {
        const QString text = stored.toString().trimmed().toLower();
        if (text == "false" || text == "0" || text == "no" || text == "off") return false;
        if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
        return stored.toBool();
    }
    if (fallback.isDouble()) return stored.toDouble();
    return QJsonValue::fromVariant(stored);
}

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
        p.setOpacity(fade);
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
        const double inkOpacity = clamp(effect.parameters.value("opacity").toDouble(1.0), 0.1, 1.0);
        const double inkSize = clamp(effect.parameters.value("size").toDouble(1.0), 0.5, 1.5);
        const int inkAlpha = qRound(255 * inkOpacity);
        for (int i = 0; i < count; ++i) {
            const QPointF center(between(random, 100, 1820), between(random, 100, 980));
            const double radius = between(random, 95, 245) * (0.6 + strength * 0.6) * inkSize;
            const double rotation = between(random, 0, 360);
            const double age = seconds - i * 0.15;
            QPainterPath shape;
            QVector<QPointF> outline;
            constexpr int points = 20;
            outline.reserve(points);
            for (int j = 0; j < points; ++j) {
                const double angle = j * 2 * pi / points;
                const double r = radius * between(random, 0.78, 1.08);
                outline.push_back(QPointF(std::cos(angle) * r, std::sin(angle) * r));
            }
            shape.moveTo((outline.back() + outline.front()) / 2.0);
            for (int j = 0; j < points; ++j)
                shape.quadTo(outline[j], (outline[j] + outline[(j + 1) % points]) / 2.0);
            shape.closeSubpath();
            p.save();
            p.translate(center);
            p.rotate(rotation);
            const double growth = clamp(age * 5, 0, 1);
            p.scale(growth, growth);
            p.setBrush(QColor(4, 13, 23, inkAlpha));
            p.drawPath(shape);
            for (int j = 0; j < 13; ++j) {
                const double angle = between(random, 0, 2 * pi), distance = radius * between(random, 1, 1.6);
                const double blob = between(random, 5, 22);
                p.drawEllipse(QPointF(std::cos(angle) * distance, std::sin(angle) * distance), blob, blob);
            }
            p.restore();
            if (age > 0 && effect.parameters.value("drips").toBool(true)) {
                p.setPen(QPen(QColor(4, 13, 23, inkAlpha), 9, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(center + QPointF(radius * 0.3, radius * 0.6),
                           center + QPointF(radius * 0.3, radius * 0.6 + std::min(age * 18, 125.0)));
                p.setPen(Qt::NoPen);
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
            p.setBrush(QColor(22, 27, 37));
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
        const int blocks = 8 + static_cast<int>(strength * 20);
        for (int i = 0; i < blocks; ++i) {
            const double x = between(random, 0, 1800), y = between(random, 0, 1040);
            const double width = between(random, 60, 400) * (0.55 + strength * 0.45);
            const double height = between(random, 3, 24) * (0.55 + strength * 0.45);
            const double drift = std::sin(seconds * 2 + i) * 45;
            p.fillRect(QRectF(x + drift, y, width, height), QColor(i % 2 ? 255 : 5, i % 2 ? 25 : 230, 230));
        }
        p.setPen(QPen(QColor(0, 170, 190), 1));
        const int scanlineGap = std::max(9, 24 - static_cast<int>(strength * 15));
        for (int y = 0; y < 1080; y += scanlineGap) p.drawLine(0, y, 1920, y);
        text(p, QRectF(65, 55, 1200, 50), "0xEXCELLENT // SIGNAL INTEGRITY DEGRADED", 24, cyan());
    } else if (effect.id == "darkness") {
        QRadialGradient gradient(QPointF(960 + std::sin(seconds * 0.4) * 120, 540), 1000);
        gradient.setColorAt(0, QColor(0, 0, 0, static_cast<int>(85 + 75 * strength)));
        gradient.setColorAt(0.25, QColor(0, 0, 0, static_cast<int>(115 + 80 * strength)));
        gradient.setColorAt(1, QColor(0, 3, 9));
        p.fillRect(QRectF(0, 0, 1920, 1080), gradient);
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
    } else if (effect.id == "neon_lightning") {
        const QColor electric(41, 239, 255);
        const QColor violet(170, 74, 255);
        for (int bolt = 0; bolt < 5 + static_cast<int>(strength * 5); ++bolt) {
            const bool horizontal = bolt % 2 == 0;
            QPainterPath path;
            QPointF point(horizontal ? -30.0 : between(random, 120, 1800),
                          horizontal ? between(random, 90, 990) : -30.0);
            path.moveTo(point);
            for (int segment = 1; segment <= 10; ++segment) {
                const double phase = seconds * (5.0 + strength * 3.0) + bolt * 0.7 + segment;
                point = horizontal
                    ? QPointF(segment * 204.0 - 40.0, point.y() + std::sin(phase) * between(random, 22, 65))
                    : QPointF(point.x() + std::sin(phase) * between(random, 22, 65), segment * 116.0 - 40.0);
                path.lineTo(point);
            }
            p.setPen(QPen(QColor(0, 226, 255, 45), 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
            p.setPen(QPen(bolt % 3 ? electric : violet, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        p.setPen(QPen(QColor(137, 248, 255, 90), 1));
        for (int i = 0; i < 24; ++i) {
            const QPointF spark(between(random, 0, 1920), between(random, 0, 1080));
            p.drawLine(spark, spark + QPointF(between(random, -18, 18), between(random, -18, 18)));
        }
    } else if (effect.id == "data_storm") {
        p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 8, 13, static_cast<int>(55 + strength * 70)));
        QFont code("Consolas", 22);
        code.setPixelSize(22);
        code.setBold(true);
        p.setFont(code);
        const QString glyphs = "01ABCDEF<>/{}[]#";
        const int columns = 18 + static_cast<int>(strength * 12);
        for (int column = 0; column < columns; ++column) {
            const double x = (column + 0.5) * 1920.0 / columns;
            const double speed = between(random, 95, 240) * (0.65 + strength * 0.7);
            const double head = std::fmod(between(random, 0, 1080) + seconds * speed, 1320.0) - 120.0;
            const int trail = 6 + static_cast<int>(strength * 8);
            for (int row = 0; row < trail; ++row) {
                const int alpha = std::max(20, 240 - row * (210 / trail));
                p.setPen(row == 0 ? QColor(225, 255, 255) : QColor(31, 232, 214, alpha));
                const int glyph = static_cast<int>((effect.seed + column * 11 + row * 7 + static_cast<int>(seconds * 8)) % glyphs.size());
                p.drawText(QPointF(x, head - row * 34), QString(glyphs.at(glyph)));
            }
        }
        text(p, QRectF(45, 995, 900, 40), "// DATA STORM: PACKET INTEGRITY LOST", 21, QColor(106, 255, 238));
        p.fillRect(QRectF(45, 1041, 520 * (0.45 + strength * 0.55), 4), QColor(31, 232, 214));
    } else if (effect.id == "security_lockdown") {
        const QColor alert(255, 47, 112);
        const double sweep = std::fmod(seconds * (250 + strength * 260), 1220.0) - 70.0;
        p.fillRect(QRectF(0, 0, 1920, 52), QColor(10, 12, 19, 230));
        p.fillRect(QRectF(0, 1028, 1920, 52), QColor(10, 12, 19, 230));
        for (int x = -80; x < 2000; x += 100) {
            p.setBrush((x / 100) % 2 ? alert : QColor(20, 24, 33));
            p.setPen(Qt::NoPen);
            QPolygonF stripe;
            stripe << QPointF(x, 0) << QPointF(x + 55, 0) << QPointF(x + 95, 52) << QPointF(x + 40, 52);
            p.drawPolygon(stripe);
            QPolygonF lower;
            lower << QPointF(x, 1028) << QPointF(x + 55, 1028) << QPointF(x + 95, 1080) << QPointF(x + 40, 1080);
            p.drawPolygon(lower);
        }
        QLinearGradient scan(0, sweep - 65, 0, sweep + 65);
        scan.setColorAt(0, QColor(255, 47, 112, 0));
        scan.setColorAt(0.5, QColor(255, 47, 112, static_cast<int>(70 + strength * 80)));
        scan.setColorAt(1, QColor(255, 47, 112, 0));
        p.fillRect(QRectF(0, sweep - 65, 1920, 130), scan);
        p.translate(960, 540);
        p.rotate(seconds * 28);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 47, 112, 165), 4));
        p.drawEllipse(QPointF(0, 0), 210, 210);
        p.drawArc(QRectF(-285, -285, 570, 570), 20 * 16, 105 * 16);
        p.drawArc(QRectF(-285, -285, 570, 570), 200 * 16, 105 * 16);
        p.rotate(-seconds * 28);
        text(p, QRectF(-500, -58, 1000, 90), "SECURITY LOCKDOWN", 48, Qt::white, Qt::AlignCenter);
        text(p, QRectF(-500, 32, 1000, 44), "UNAUTHORIZED INPUT DETECTED", 21, alert, Qt::AlignCenter);
    } else if (effect.id == "gravity_well") {
        const QPointF center(960 + std::sin(seconds * 0.55) * 95, 540 + std::cos(seconds * 0.43) * 55);
        QRadialGradient voidGradient(center, 380);
        voidGradient.setColorAt(0, QColor(0, 0, 0));
        voidGradient.setColorAt(0.32, QColor(4, 2, 16, 225));
        voidGradient.setColorAt(0.68, QColor(79, 23, 125, static_cast<int>(85 + strength * 70)));
        voidGradient.setColorAt(1, QColor(19, 229, 238, 0));
        p.setBrush(voidGradient);
        p.drawEllipse(center, 380, 380);
        p.setBrush(Qt::NoBrush);
        for (int ring = 0; ring < 6; ++ring) {
            p.save();
            p.translate(center);
            p.rotate((ring % 2 ? -1 : 1) * seconds * (18 + ring * 5));
            p.scale(1.0, 0.30 + ring * 0.035);
            p.setPen(QPen(ring % 2 ? QColor(183, 75, 255, 185) : cyan(185), 2.5 + ring * 0.35));
            p.drawArc(QRectF(-190 - ring * 37, -190 - ring * 37, 380 + ring * 74, 380 + ring * 74),
                      (ring * 43) * 16, (185 + ring * 17) * 16);
            p.restore();
        }
        for (int i = 0; i < 55; ++i) {
            const double angle = between(random, 0, 2 * pi) + seconds * between(random, 0.25, 1.2);
            const double radius = between(random, 120, 650) * (1.0 - 0.06 * std::sin(seconds * 2 + i));
            const QPointF particle = center + QPointF(std::cos(angle) * radius, std::sin(angle) * radius * 0.48);
            p.setBrush(i % 4 ? cyan(190) : QColor(204, 96, 255, 220));
            p.drawEllipse(particle, between(random, 2, 7), between(random, 2, 7));
        }
    } else if (effect.id == "jumpscare_lite") {
        const double appearance = clamp(seconds * 10, 0, 1);
        const double pulse = 1.0 + std::sin(seconds * 16) * 0.025;
        const double jitterX = std::sin(seconds * 73) * 7 * strength;
        const double jitterY = std::cos(seconds * 61) * 5 * strength;
        QRadialGradient darkness(QPointF(960, 540), 1050);
        darkness.setColorAt(0, QColor(8, 0, 13, 135));
        darkness.setColorAt(0.48, QColor(1, 2, 7, 215));
        darkness.setColorAt(1, QColor(0, 0, 0, 250));
        p.fillRect(QRectF(0, 0, 1920, 1080), darkness);
        p.setOpacity(fade * appearance);
        p.save();
        p.translate(960 + jitterX, 520 + jitterY);
        p.scale((0.42 + appearance * 0.78) * pulse, (0.42 + appearance * 0.78) * pulse);
        QPainterPath head;
        head.moveTo(-255, -275); head.quadTo(0, -410, 255, -275);
        head.lineTo(220, 155); head.quadTo(0, 330, -220, 155); head.closeSubpath();
        p.setBrush(QColor(3, 4, 9));
        p.setPen(QPen(QColor(255, 33, 105, 85), 24, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(head.translated(-8, 5));
        p.setPen(QPen(cyan(), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(head);
        QPainterPath leftEye; leftEye.moveTo(-190, -105); leftEye.lineTo(-35, -48); leftEye.lineTo(-175, 5); leftEye.closeSubpath();
        QPainterPath rightEye; rightEye.moveTo(190, -105); rightEye.lineTo(35, -48); rightEye.lineTo(175, 5); rightEye.closeSubpath();
        p.setBrush(QColor(232, 255, 255)); p.setPen(Qt::NoPen); p.drawPath(leftEye); p.drawPath(rightEye);
        p.setBrush(QColor(255, 25, 94));
        p.drawEllipse(QPointF(-105, -49), 13, 34); p.drawEllipse(QPointF(105, -49), 13, 34);
        QPainterPath mouth; mouth.moveTo(-175, 72); mouth.quadTo(0, 230, 175, 72); mouth.quadTo(0, 280, -175, 72); mouth.closeSubpath();
        p.setBrush(QColor(0, 0, 0)); p.setPen(QPen(QColor(255, 33, 105), 6)); p.drawPath(mouth);
        p.setBrush(QColor(223, 255, 255)); p.setPen(Qt::NoPen);
        for (int tooth = -4; tooth <= 4; ++tooth) {
            const double x = tooth * 34.0;
            QPolygonF fang; fang << QPointF(x - 14, 96) << QPointF(x + 14, 96) << QPointF(x, 148 + (tooth % 2 ? 22 : 0));
            p.drawPolygon(fang);
        }
        p.restore();
        p.setOpacity(fade * appearance);
        for (int i = 0; i < 14; ++i) {
            const double y = between(random, 0, 1080);
            const double drift = std::sin(seconds * 26 + i) * 75;
            p.fillRect(QRectF(between(random, 0, 1200) + drift, y, between(random, 120, 620), between(random, 2, 8)),
                       i % 3 ? QColor(30, 238, 241, 95) : QColor(255, 33, 105, 120));
        }
        text(p, QRectF(0, 935, 1920, 70), "DON'T LOOK AWAY", 42, QColor(255, 44, 112), Qt::AlignCenter);
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
    : QObject(parent), settings_(settingsPath(), QSettings::IniFormat), frame_(sharedFrame()),
      raidAudio_(std::make_unique<RaidAudio>())
{
    definitions_ = {
        {"crash", "Excellent.exe Crashed", "Fictional crash with automatic recovery.", "System", 5000, 1000, 15000, 15000, 0.65, 1, true, true, {{"style", 0}}},
        {"ink_splatter", "Ink Splatter", "Rounded organic ink shapes and gradual drips.", "Obstruction", 8000, 1000, 30000, 10000, 0.65, 1, true, false, {{"splatters", 5}, {"drips", true}, {"opacity", 1.0}, {"size", 1.0}}},
        {"error_invasion", "Error Window Invasion", "Fictional error windows recover automatically.", "System", 7000},
        {"corruption", "System Corruption", "Localized digital artifacts, scanlines, and text.", "System", 6000},
        {"darkness", "Darkness Protocol", "Animated transparent edge shadows.", "Atmosphere", 8000},
        {"fog", "Fogged Screen", "Drifting translucent fog and condensation droplets.", "Atmosphere", 8000},
        {"mouse_invasion", "Fake Mouse Invasion", "Visual cursors only; never controls your mouse.", "Playful", 6000},
        {"neon_lightning", "Neon Lightning", "Animated cyan and violet electrical arcs tear across the screen.", "Energy", 5000},
        {"data_storm", "Data Storm", "A dense cascade of glowing code rains through the scene.", "Cyber", 7000},
        {"security_lockdown", "Security Lockdown", "Hazard borders, a scanning beam, and a rotating security reticle.", "System", 6500},
        {"gravity_well", "Gravity Well", "A dark neon vortex pulls orbiting particles toward its core.", "Space", 8000},
        {"jumpscare_lite", "Protocol Jumpscare", "A sudden distorted face reveal with darkened edges and glitch trails; no strobe.", "Horror", 1800, 500, 4500, 15000, 0.7, 0.5, true, true, {}},
    };
    clock_.start();
    overlay_ = std::make_unique<DesktopOverlay>([this] { clear(); });
    raid_.onSound = [this](const QString &name) { playRaidSound(name); };
    loadSettings();
    raidAudio_->setEnabled(raidSoundsEnabled_);
    raidAudio_->setVolume(raidSoundVolume_);
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
    raidEnabled_ = settings_.value("raid/enabled", true).toBool();
    raidSoundsEnabled_ = settings_.value("raid/sounds", true).toBool();
    raidSoundVolume_ = std::clamp(settings_.value("raid/soundVolume", 70).toInt(), 0, 100);
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
            it.value() = typedSetting(settings_, prefix + it.key(), it.value());
    }
}

void EffectManager::saveSettings()
{
    settings_.setValue("effects/enabled", enabled_);
    settings_.setValue("display/desktop", desktopEnabled_);
    settings_.setValue("display/stream", streamEnabled_);
    settings_.setValue("raid/enabled", raidEnabled_);
    settings_.setValue("raid/sounds", raidSoundsEnabled_);
    settings_.setValue("raid/soundVolume", raidSoundVolume_);
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
        if (it.key() == "opacity" && (!it->isDouble() || !std::isfinite(it->toDouble()) || it->toDouble() < 0.1 || it->toDouble() > 1.0)) return result(id, "invalid_parameter", origin);
        if (it.key() == "size" && (!it->isDouble() || !std::isfinite(it->toDouble()) || it->toDouble() < 0.5 || it->toDouble() > 1.5)) return result(id, "invalid_parameter", origin);
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

QString EffectManager::startTestRaid(int viewers)
{
    if (stopped_ || !enabled_ || !raidEnabled_) return result("raid", "disabled", "dock");
    if (!raid_.start(viewers, clock_.elapsed(), true)) return result("raid", "raid_active", "dock");
    timer_.start();
    notifyChanged();
    return result("raid", "raid_started", "dock");
}

void EffectManager::stopRaid()
{
    raid_.clear();
    if (raidAudio_) raidAudio_->stop();
    tick();
    notifyChanged();
}

void EffectManager::setRaidEnabled(bool value)
{
    raidEnabled_ = value;
    if (!value) stopRaid();
    saveSettings();
    notifyChanged();
}

void EffectManager::setRaidSoundsEnabled(bool value)
{
    raidSoundsEnabled_ = value;
    if (raidAudio_) raidAudio_->setEnabled(value);
    saveSettings();
    notifyChanged();
}

void EffectManager::setRaidSoundVolume(int value)
{
    raidSoundVolume_ = std::clamp(value, 0, 100);
    if (raidAudio_) raidAudio_->setVolume(raidSoundVolume_);
    saveSettings();
    notifyChanged();
}

void EffectManager::addRaidAudioOutput(obs_source_t *source)
{
    if (raidAudio_) raidAudio_->addOutputSource(source);
}

void EffectManager::removeRaidAudioOutput(obs_source_t *source)
{
    if (raidAudio_) raidAudio_->removeOutputSource(source);
}

QJsonObject EffectManager::raidStatus() const
{
    return raid_.status(clock_.elapsed());
}

void EffectManager::playRaidSound(const QString &name)
{
#if defined(_WIN32) && !defined(FX_TESTING)
    if (!raidSoundsEnabled_ || !raidAudio_) return;
    static const QHash<QString, QStringList> variants{
        {"raid-shot-blaster", {"raid-shot-blaster-1", "raid-shot-blaster-2"}},
        {"raid-shot-plasma", {"raid-shot-plasma-1", "raid-shot-plasma-2"}},
        {"raid-shot-railgun", {"raid-shot-railgun-1", "raid-shot-railgun-2"}},
        {"raid-shot-hammer", {"raid-shot-hammer-1", "raid-shot-hammer-2"}},
        {"raid-enemy-hit", {"raid-enemy-hit-1", "raid-enemy-hit-2", "raid-enemy-hit-3"}},
        {"raid-enemy-death", {"raid-enemy-death-1", "raid-enemy-death-2", "raid-enemy-death-3"}},
        {"raid-enemy-spawn", {"raid-enemy-spawn-1", "raid-enemy-spawn-2"}},
    };
    QString selected = name;
    if (variants.contains(name)) {
        const QStringList &choices = variants[name];
        selected = choices.at(QRandomGenerator::global()->bounded(choices.size()));
    }
    const QByteArray relative = "sounds/" + selected.toUtf8() + ".wav";
    char *rawPath = obs_module_file(relative.constData());
    if (!rawPath) return;
    const QString path = QString::fromUtf8(rawPath);
    bfree(rawPath);
    QString group = name;
    double gain = 0.34;
    int priority = 1;
    if (name == "raid-shot-blaster") { group = "weapon"; gain = 0.16; priority = 0; }
    else if (name == "raid-shot-plasma") { group = "weapon"; gain = 0.19; priority = 1; }
    else if (name == "raid-shot-railgun") { group = "weapon"; gain = 0.23; priority = 2; }
    else if (name == "raid-shot-hammer") { group = "weapon"; gain = 0.27; priority = 3; }
    else if (name == "raid-enemy-hit") { group = "enemy"; gain = 0.045; priority = 0; }
    else if (name == "raid-enemy-death") { group = "enemy"; gain = 0.085; priority = 2; }
    else if (name == "raid-enemy-spawn") { group = "enemy"; gain = 0.06; priority = 1; }
    else if (name == "raid-victory" || name == "raid-failure" || name == "raid-start") {
        group = "stinger"; gain = 0.38; priority = 4;
    } else { group = "effect"; gain = 0.14; priority = 3; }
    if (!path.isEmpty()) raidAudio_->play(path, group, gain, priority);
    if (name == "raid-start") {
        char *rawMusicPath = obs_module_file("sounds/raid-battle-music.wav");
        if (rawMusicPath) {
            const QString musicPath = QString::fromUtf8(rawMusicPath);
            bfree(rawMusicPath);
            raidAudio_->play(musicPath, "music", 0.8, 0, true);
        }
    }
#else
    Q_UNUSED(name);
#endif
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
    if (raidAudio_) raidAudio_->cleanup();
    const auto oldSize = active_.size();
    const bool raidChanged = raid_.tick(now);
    active_.erase(std::remove_if(active_.begin(), active_.end(), [now](const ActiveEffect &effect) {
        return now - effect.startedMs >= std::min(effect.durationMs, maxDuration);
    }), active_.end());
    if (active_.isEmpty() && !raid_.visible()) {
        timer_.stop(); overlay_->hide(); overlay_->setFrame({}); publish({});
        if (!queue_.isEmpty()) {
            const auto next = queue_.dequeue();
            trigger(next.value("effect").toString(), next.value("duration_ms").toInt(-1), next.value("intensity").toDouble(-1), next.value("parameters").toObject(), "queue");
        }
    } else {
        const int targetInterval = raid_.visible() && active_.isEmpty() ? 50 : 33;
        if (timer_.interval() != targetInterval) timer_.setInterval(targetInterval);
        QImage image = active_.isEmpty() ? QImage(renderSize_, QImage::Format_ARGB32)
                                        : renderEffects(renderSize_, active_, now, overallIntensity_);
        if (image.isNull()) { clear(); return; }
        if (active_.isEmpty()) image.fill(Qt::transparent);
        if (raid_.visible()) {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(renderSize_.width() / 1920.0, renderSize_.height() / 1080.0);
            raid_.paint(painter, now);
        }
        publish(image);
        if (desktopEnabled_) { overlay_->setFrame(image); if (!overlay_->isVisible()) overlay_->show(); }
        else overlay_->hide();
    }
    if (active_.size() != oldSize || raidChanged) notifyChanged();
}

void EffectManager::clear()
{
    active_.clear(); queue_.clear(); raid_.clear(); timer_.stop();
    if (raidAudio_) raidAudio_->stop();
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
void EffectManager::setDesktopEnabled(bool value) { desktopEnabled_ = value; if (!active_.isEmpty() || raid_.visible()) tick(); else if (!value && overlay_) overlay_->hide(); saveSettings(); notifyChanged(); }
void EffectManager::setStreamEnabled(bool value) { streamEnabled_ = value; if (!active_.isEmpty() || raid_.visible()) tick(); else publish({}); saveSettings(); notifyChanged(); }
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
    if (!active_.isEmpty() || raid_.visible()) tick();
    notifyChanged();
}

QString EffectManager::displayDescription() const
{
    return QString("Shared render surface: %1 x %2, 30 fps for effects / 20 fps for Raid Battle only.\n%3 ExcelProtocol FX source(s) currently loaded.")
        .arg(renderSize_.width()).arg(renderSize_.height()).arg(frame_->sourceCount.load());
}

QJsonObject EffectManager::command(const QJsonObject &request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString type = request.value("type").toString();
    const QString eventId = request.value("event_id").toString();
    const bool mutation = type == "trigger_effect" || type == "trigger_random" || type == "clear_effects"
        || type == "cancel_effect" || type == "raid_start" || type == "raid_chat" || type == "raid_end";
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
    if (type == "raid_start") {
        if (!raidEnabled_ || !enabled_) return reply(false, "disabled");
        const QJsonValue viewers = request.value("viewers");
        if (!viewers.isDouble() || viewers.toDouble() != viewers.toInt() || viewers.toInt() < 1 || viewers.toInt() > 1000000)
            return reply(false, "invalid_raid_viewers");
        if (!raid_.start(viewers.toInt(), clock_.elapsed(), false)) return reply(false, "raid_active");
        timer_.start(); notifyChanged();
        QJsonObject response = reply(true, "raid_started"); response.insert("raid", raidStatus()); return response;
    }
    if (type == "raid_chat") {
        if (!request.value("username").isString() || !request.value("role").isString() || !request.value("message").isString())
            return reply(false, "invalid_raid_chat");
        const QString status = raid_.chat(request.value("username").toString(), request.value("role").toString(),
                                          request.value("message").toString(), clock_.elapsed());
        const bool accepted = status == "attack_queued" || status == "attack_blocked"
            || status == "attack_cooldown" || status == "roadblock_cleared";
        QJsonObject response = reply(accepted, status); response.insert("raid", raidStatus()); return response;
    }
    if (type == "raid_end") {
        if (!request.value("victory").isBool()) return reply(false, "invalid_raid_result");
        const QJsonObject before = raidStatus();
        if (before.value("phase").toString() != "fighting") return reply(false, "no_active_raid");
        const bool engaged = before.value("interaction_count").toInt() > 0;
        if (!raid_.finish(request.value("victory").toBool() || engaged, clock_.elapsed())) return reply(false, "raid_still_active");
        notifyChanged();
        QJsonObject response = reply(true, engaged || request.value("victory").toBool() ? "raid_victory" : "raid_failed");
        response.insert("raid", raidStatus()); return response;
    }
    if (type == "get_active_effects") {
        QJsonArray values;
        for (const auto &effect : active_) values.push_back(QJsonObject{{"effect", effect.id}, {"remaining_ms", std::max<qint64>(0, effect.durationMs - (clock_.elapsed() - effect.startedMs))}, {"intensity", effect.intensity}});
        return {{"ok", true}, {"active", values}};
    }
    if (type == "list_effects") {
        QJsonArray values;
        for (const auto &def : definitions_) {
            QJsonObject parameters;
            if (def.id == "crash") {
                parameters.insert("style", QJsonObject{{"type", "integer"}, {"label", "Style"},
                    {"min", 0}, {"max", 2}, {"step", 1}, {"default", def.parameters.value("style")}});
            } else if (def.id == "ink_splatter") {
                parameters.insert("splatters", QJsonObject{{"type", "integer"}, {"label", "Splatters"},
                    {"min", 1}, {"max", 12}, {"step", 1}, {"default", def.parameters.value("splatters").toInt(5)}});
                parameters.insert("drips", QJsonObject{{"type", "boolean"}, {"label", "Animate drips"},
                    {"default", def.parameters.value("drips").toBool(true)}});
                parameters.insert("opacity", QJsonObject{{"type", "number"}, {"label", "Ink opacity"},
                    {"min", 0.1}, {"max", 1.0}, {"step", 0.05}, {"default", def.parameters.value("opacity").toDouble(1.0)}});
                parameters.insert("size", QJsonObject{{"type", "number"}, {"label", "Splatter size"},
                    {"control", "range"}, {"min", 0.5}, {"max", 1.5}, {"step", 0.05}, {"default", def.parameters.value("size").toDouble(1.0)}});
            }
            values.push_back(QJsonObject{
                {"id", def.id}, {"name", def.name}, {"description", def.description}, {"category", def.category},
                {"enabled", def.enabled}, {"default_duration_ms", def.durationMs},
                {"min_duration_ms", def.minDurationMs}, {"max_duration_ms", def.maxDurationMs},
                {"default_cooldown_ms", def.cooldownMs}, {"default_intensity", def.intensity},
                {"default_random_weight", def.weight}, {"parameters", parameters},
            });
        }
        return {{"ok", true}, {"effects", values}};
    }
    if (type == "get_status") return {{"ok", true}, {"enabled", enabled_}, {"active_count", static_cast<int>(active_.size())}, {"queued_count", static_cast<int>(queue_.size())}, {"desktop", desktopEnabled_}, {"stream", streamEnabled_}, {"source_count", frame_->sourceCount.load()}};
    if (type == "get_raid_status") return {{"ok", true}, {"raid", raidStatus()}};
    return reply(false, "unknown_command");
}

} // namespace fx
