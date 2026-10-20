#include "effects.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QThread>
#include <iostream>
#include <limits>

static int failed = 0;
static void check(bool condition, const char *message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failed; }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QFile::remove(fx::settingsPath());
    {
        fx::EffectManager manager;
        manager.setDesktopEnabled(false);
        check(manager.active().empty(), "no startup activation");
        for (const auto &def : manager.definitions()) {
            const QVector<fx::ActiveEffect> effects{{def.id, 42901, 0, 5000, 0.7, def.parameters}};
            const auto first = fx::renderEffects(QSize(640, 360), effects, 1000, 0.8);
            const auto second = fx::renderEffects(QSize(640, 360), effects, 1000, 0.8);
            check(!first.isNull(), "each effect renders");
            check(first == second, "same seed/time yields same frame");
            bool visible = false;
            for (int y = 0; y < first.height() && !visible; ++y)
                for (int x = 0; x < first.width(); ++x) if (qAlpha(first.pixel(x, y))) { visible = true; break; }
            check(visible, "each effect produces visible pixels");
        }
        check(manager.trigger("ink_splatter", 60000) == "invalid_duration", "hard duration limit");
        check(manager.trigger("ink_splatter", -1, std::numeric_limits<double>::quiet_NaN()) == "invalid_intensity", "NaN rejected");
        check(manager.trigger("ink_splatter", -1, 0.5, {{"splatters", 10000}}) == "invalid_parameter", "bounded effect parameters");
        check(manager.trigger("crash", 1000) == "triggered", "valid effect accepted");
        check(manager.trigger("ink_splatter") == "conflict", "exclusive effects isolated");
        manager.clear();
        check(manager.active().empty(), "clear empties active effects");
        { auto frame = fx::sharedFrame(); std::lock_guard<std::mutex> lock(frame->mutex); check(frame->image.isNull(), "clear clears stream frame"); }
        check(manager.trigger("crash") == "cooldown", "cooldown enforced after clear");
        manager.setEnabled(false);
        check(manager.triggerRandom() != "triggered", "global disable blocks random");
        manager.setEnabled(true);
        const QJsonObject request{{"type", "trigger_effect"}, {"event_id", "test-1"}, {"effect", "jumpscare_lite"}, {"duration_ms", 500}};
        check(manager.command(request).value("ok").toBool(), "valid command accepted");
        check(manager.command(request).value("status") == "duplicate_event", "duplicate event rejected");
        QThread::msleep(550); QCoreApplication::processEvents();
        check(manager.active().empty(), "expired effect automatically removed");
        check(manager.command({{"type", "trigger_effect"}, {"event_id", "bad-2"}, {"effect", "fog"}, {"duration_ms", 1.5}}).value("status") == "invalid_duration", "fractional durations rejected");
        manager.shutdown();
        check(manager.trigger("fog") == "disabled", "shutdown blocks activation");
    }
    QFile::remove(fx::settingsPath());
    std::cout << (failed ? "Effects tests failed\n" : "All effects tests passed\n");
    return failed ? 1 : 0;
}
