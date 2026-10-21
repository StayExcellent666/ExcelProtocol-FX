#include "effects.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
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
        // Older INI files can deserialize JSON defaults as strings. The
        // effect catalog must still publish protocol-safe primitive types.
        QSettings legacy(fx::settingsPath(), QSettings::IniFormat);
        legacy.setValue("library/ink_splatter/splatters", "12");
        legacy.setValue("library/ink_splatter/drips", "true");
        legacy.setValue("library/ink_splatter/opacity", "1");
        legacy.setValue("library/ink_splatter/size", "1");
        legacy.sync();
    }
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
            int maximumAlpha = 0;
            for (int y = 0; y < first.height(); ++y)
                for (int x = 0; x < first.width(); ++x) {
                    maximumAlpha = std::max(maximumAlpha, qAlpha(first.pixel(x, y)));
                    if (qAlpha(first.pixel(x, y))) visible = true;
                }
            check(visible, "each effect produces visible pixels");
            if (def.id != "fog") check(maximumAlpha == 255, "non-fog effects contain fully opaque pixels");
        }
        check(manager.trigger("ink_splatter", 60000) == "invalid_duration", "hard duration limit");
        check(manager.trigger("ink_splatter", -1, std::numeric_limits<double>::quiet_NaN()) == "invalid_intensity", "NaN rejected");
        check(manager.trigger("ink_splatter", -1, 0.5, {{"splatters", 10000}}) == "invalid_parameter", "bounded effect parameters");
        check(manager.trigger("ink_splatter", -1, 0.5, {{"opacity", 0.0}}) == "invalid_parameter", "bounded ink opacity");
        check(manager.trigger("ink_splatter", -1, 0.5, {{"size", 2.0}}) == "invalid_parameter", "bounded ink size");
        const QVector<fx::ActiveEffect> opaqueInk{{"ink_splatter", 42901, 0, 8000, 1.0,
                                                  {{"splatters", 5}, {"drips", true}, {"opacity", 1.0}, {"size", 1.0}}}};
        const auto inkFrame = fx::renderEffects(QSize(640, 360), opaqueInk, 1000, 0.25);
        int maximumAlpha = 0;
        for (int y = 0; y < inkFrame.height(); ++y)
            for (int x = 0; x < inkFrame.width(); ++x)
                maximumAlpha = std::max(maximumAlpha, qAlpha(inkFrame.pixel(x, y)));
        check(maximumAlpha == 255, "full ink opacity is opaque independently of intensity");
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
        const auto catalog = manager.command({{"type", "list_effects"}}).value("effects").toArray();
        check(catalog.size() == manager.definitions().size(), "effect catalog reports every effect");
        check(catalog.at(0).toObject().contains("default_duration_ms"), "effect catalog reports default duration");
        const auto inkMetadata = catalog.at(1).toObject().value("parameters").toObject();
        check(inkMetadata.value("splatters").toObject().value("max").toInt() == 12, "effect catalog reports parameter limits");
        check(inkMetadata.value("drips").toObject().value("type") == "boolean", "effect catalog reports parameter types");
        check(inkMetadata.value("splatters").toObject().value("default").isDouble(), "integer default is numeric");
        check(inkMetadata.value("drips").toObject().value("default").isBool(), "drips default is boolean");
        check(inkMetadata.value("opacity").toObject().value("default").toDouble() == 1.0, "full opacity default is valid");
        check(inkMetadata.value("size").toObject().value("control") == "range", "ink size is exposed as a slider");
        const auto raidStart = manager.command({{"type", "raid_start"}, {"event_id", "raid-1"}, {"viewers", 12}});
        check(raidStart.value("status") == "raid_started", "remote raid starts");
        check(manager.command({{"type", "raid_start"}, {"event_id", "raid-2"}, {"viewers", 0}}).value("status") == "invalid_raid_viewers", "raid viewer bounds enforced");
        const auto raidChat = manager.command({{"type", "raid_chat"}, {"event_id", "raid-3"}, {"username", "Coder"}, {"role", "vip"}, {"message", "hello"}});
        check(raidChat.value("ok").toBool(), "remote raid chat accepted");
        check(manager.command({{"type", "get_raid_status"}}).value("raid").toObject().value("interaction_count").toInt() == 1, "raid status reports interaction");
        const auto raidEnd = manager.command({{"type", "raid_end"}, {"event_id", "raid-4"}, {"victory", false}});
        check(raidEnd.value("status") == "raid_victory", "engaged raid cannot be forced to lose");
        manager.stopRaid();
        manager.shutdown();
        check(manager.trigger("fog") == "disabled", "shutdown blocks activation");
    }
    QFile::remove(fx::settingsPath());
    std::cout << (failed ? "Effects tests failed\n" : "All effects tests passed\n");
    return failed ? 1 : 0;
}
