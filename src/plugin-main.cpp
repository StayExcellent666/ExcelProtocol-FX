#include "dock.hpp"
#include "effects.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-hotkey.h>
#include <QApplication>
#include <QMetaObject>
#include <QPointer>
#include <memory>
#include <mutex>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("excelprotocol-fx", "en-US")

#ifndef FX_VERSION
#define FX_VERSION "1.0.0"
#endif

namespace {
constexpr const char *dockId = "excelprotocol-fx-dock";
std::unique_ptr<fx::EffectManager> manager;
QPointer<fx::FxDock> dock;
obs_hotkey_id clearHotkey = OBS_INVALID_HOTKEY_ID;
std::mutex hotkeyMutex;
fx::EffectManager *hotkeyManager = nullptr;
bool dockRegistered = false;

struct SourceData {
    std::shared_ptr<fx::FrameState> frame = fx::sharedFrame();
    gs_texture_t *texture = nullptr;
    quint64 serial = static_cast<quint64>(-1);
    uint32_t width = 0, height = 0;
    bool visible = false;
};

const char *sourceName(void *) { return "ExcelProtocol FX"; }
void *sourceCreate(obs_data_t *, obs_source_t *)
{
    auto *source = new SourceData;
    ++source->frame->sourceCount;
    return source;
}
void sourceDestroy(void *data)
{
    auto *source = static_cast<SourceData *>(data);
    --source->frame->sourceCount;
    obs_enter_graphics();
    if (source->texture) gs_texture_destroy(source->texture);
    obs_leave_graphics();
    delete source;
}
uint32_t sourceWidth(void *data) { return static_cast<SourceData *>(data)->frame->width.load(); }
uint32_t sourceHeight(void *data) { return static_cast<SourceData *>(data)->frame->height.load(); }
void sourceRender(void *data, gs_effect_t *)
{
    auto *source = static_cast<SourceData *>(data);
    QImage image;
    quint64 serial;
    {
        std::lock_guard<std::mutex> lock(source->frame->mutex);
        serial = source->frame->serial;
        if (serial != source->serial) image = source->frame->image;
    }
    if (serial != source->serial) {
        source->visible = !image.isNull();
        if (source->visible) {
            const auto width = static_cast<uint32_t>(image.width()), height = static_cast<uint32_t>(image.height());
            if (!source->texture || source->width != width || source->height != height) {
                if (source->texture) gs_texture_destroy(source->texture);
                source->texture = gs_texture_create(width, height, GS_BGRA, 1, nullptr, GS_DYNAMIC);
                source->width = width; source->height = height;
            }
            if (!source->texture) return;
            gs_texture_set_image(source->texture, image.constBits(), static_cast<uint32_t>(image.bytesPerLine()), false);
        }
        source->serial = serial;
    }
    if (source->visible && source->texture) obs_source_draw(source->texture, 0, 0, source->width, source->height, false);
}

obs_properties_t *sourceProperties(void *)
{
    auto *properties = obs_properties_create();
    obs_properties_add_text(properties, "info", "Configure effects in Docks > ExcelProtocol FX. Place this source above gameplay. The desktop and stream outputs share the same animation frame.", OBS_TEXT_INFO);
    return properties;
}

void clearHotkeyCallback(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed) return;
    std::lock_guard<std::mutex> guard(hotkeyMutex);
    if (hotkeyManager) {
        auto *target = hotkeyManager;
        QMetaObject::invokeMethod(target, [target] { target->clear(); }, Qt::QueuedConnection);
    }
}

void saveHotkey()
{
    if (!manager || clearHotkey == OBS_INVALID_HOTKEY_ID) return;
    auto *bindings = obs_hotkey_save(clearHotkey);
    auto *data = obs_data_create();
    obs_data_set_array(data, "bindings", bindings);
    const char *json = obs_data_get_json(data);
    manager->settings().setValue("safety/clearHotkey", QString::fromUtf8(json ? json : "{}"));
    obs_data_array_release(bindings); obs_data_release(data);
}

void initializeUi()
{
    if (manager || !qApp) return;
    manager = std::make_unique<fx::EffectManager>();
    {
        std::lock_guard<std::mutex> guard(hotkeyMutex);
        hotkeyManager = manager.get();
    }
    clearHotkey = obs_hotkey_register_frontend("ExcelProtocolFX.Clear", "ExcelProtocol FX: Clear all effects", clearHotkeyCallback, nullptr);
    const auto json = manager->settings().value("safety/clearHotkey").toString().toUtf8();
    if (!json.isEmpty()) {
        auto *data = obs_data_create_from_json(json.constData());
        if (data) { auto *bindings = obs_data_get_array(data, "bindings"); obs_hotkey_load(clearHotkey, bindings); obs_data_array_release(bindings); obs_data_release(data); }
    }
    dock = new fx::FxDock(*manager);
    dockRegistered = obs_frontend_add_dock_by_id(dockId, "ExcelProtocol FX", dock);
    if (!dockRegistered) { delete dock; dock = nullptr; blog(LOG_WARNING, "[ExcelProtocol FX] Could not register dock"); }
}

void shutdownUi()
{
    if (!manager) return;
    saveHotkey();
    {
        std::lock_guard<std::mutex> guard(hotkeyMutex);
        hotkeyManager = nullptr;
    }
    if (clearHotkey != OBS_INVALID_HOTKEY_ID) { obs_hotkey_unregister(clearHotkey); clearHotkey = OBS_INVALID_HOTKEY_ID; }
    if (dock) dock->shutdown();
    manager->shutdown();
    if (dockRegistered) { obs_frontend_remove_dock(dockId); dockRegistered = false; }
    if (dock) { delete dock; dock = nullptr; }
    manager.reset();
}

void frontendEvent(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) initializeUi();
    // Shutdown while the frontend/Qt context still exists, before module unload.
    if (event == OBS_FRONTEND_EVENT_EXIT) shutdownUi();
}
}

MODULE_EXPORT const char *obs_module_description(void) { return "ExcelProtocol FX: synchronized desktop and stream effects with GitHub updates"; }
MODULE_EXPORT const char *obs_module_name(void) { return "ExcelProtocol FX"; }
MODULE_EXPORT const char *obs_module_author(void) { return "ExcelProtocol"; }

bool obs_module_load(void)
{
    struct obs_source_info info{};
    info.id = "excelprotocol_fx";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO;
    info.get_name = sourceName; info.create = sourceCreate; info.destroy = sourceDestroy;
    info.get_width = sourceWidth; info.get_height = sourceHeight; info.video_render = sourceRender;
    info.get_properties = sourceProperties; info.icon_type = OBS_ICON_TYPE_CUSTOM;
    obs_register_source(&info);
    obs_frontend_add_event_callback(frontendEvent, nullptr);
    blog(LOG_INFO, "[ExcelProtocol FX] Loaded v%s on OBS %s", FX_VERSION, obs_get_version_string());
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
    shutdownUi();
}
