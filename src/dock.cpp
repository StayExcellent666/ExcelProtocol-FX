#include "dock.hpp"
#include "effects.hpp"
#include "update-controller.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace fx {
namespace {
QLabel *note(const QString &value, QWidget *parent = nullptr)
{
    auto *label = new QLabel(value, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QWidget *scrollable(QWidget *widget)
{
    auto *area = new QScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setWidget(widget);
    return area;
}
}

FxDock::FxDock(EffectManager &manager, QWidget *parent) : QWidget(parent), manager_(manager)
{
    setMinimumSize(360, 340);
    setStyleSheet("QGroupBox { border: 1px solid #3c4a55; border-radius: 6px; margin-top: 14px; padding-top: 12px; }"
                  "QGroupBox::title { color: #42dfec; subcontrol-origin: margin; left: 10px; }"
                  "QPushButton#emergencyClear { color: white; background: #863f4d; font-weight: bold; padding: 9px; }"
                  "QLabel#brand { color: #42dfec; font-size: 21px; font-weight: bold; }");
    auto *layout = new QVBoxLayout(this);
    auto *brand = new QLabel("EXCELPROTOCOL FX"); brand->setObjectName("brand"); layout->addWidget(brand);
    auto *tabs = new QTabWidget;
    tabs->addTab(scrollable(dashboard()), "Dashboard");
    tabs->addTab(scrollable(library()), "Effects");
    tabs->addTab(scrollable(display()), "Display");
    tabs->addTab(scrollable(integration()), "Connection");
    updates_ = new UpdateController(this);
    tabs->addTab(scrollable(updates_->createWidget(this)), "Updates");
    layout->addWidget(tabs);
    auto *clearButton = new QPushButton("CLEAR ALL EFFECTS"); clearButton->setObjectName("emergencyClear");
    QObject::connect(clearButton, &QPushButton::clicked, this, [this] { manager_.clear(); });
    layout->addWidget(clearButton);
    layout->addWidget(note(manager_.emergencyHotkeyAvailable()
        ? "Emergency clear: Ctrl + Shift + F12 (works outside OBS)"
        : "Ctrl + Shift + F12 is already in use. Assign ExcelProtocol FX: Clear all effects in OBS Settings > Hotkeys."));
    manager_.onChanged = [this] { refresh(); };
    manager_.onResult = [this](const QString &value) { if (resultLabel_) resultLabel_->setText("Last result: " + value); };
    QObject::connect(qApp, &QGuiApplication::screenAdded, this, [this] { refreshMonitors(); });
    QObject::connect(qApp, &QGuiApplication::screenRemoved, this, [this] { refreshMonitors(); });
    QObject::connect(tabs, &QTabWidget::currentChanged, this, [this] { refresh(); });
    refresh();
    updates_->start();
}

FxDock::~FxDock() { shutdown(); }
void FxDock::shutdown()
{
    if (stopped_) return;
    stopped_ = true;
    manager_.onChanged = {}; manager_.onResult = {};
    if (updates_) updates_->shutdown();
}

QWidget *FxDock::dashboard()
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->addWidget(note("Standalone mode is ready. Effects start only when triggered."));
    auto *enabled = new QCheckBox("Enable all viewer effects"); enabled->setChecked(manager_.enabled());
    QObject::connect(enabled, &QCheckBox::toggled, this, [this](bool value) { manager_.setEnabled(value); }); layout->addWidget(enabled);
    activeLabel_ = note(""); layout->addWidget(activeLabel_);
    resultLabel_ = note("Last result: ready"); layout->addWidget(resultLabel_);
    auto *random = new QPushButton("Test random effect");
    QObject::connect(random, &QPushButton::clicked, this, [this] { manager_.triggerRandom(); }); layout->addWidget(random);
    auto *chaos = new QPushButton("Trigger Chaos Protocol");
    QObject::connect(chaos, &QPushButton::clicked, this, [this] { manager_.triggerChaos(); }); layout->addWidget(chaos);
    auto *form = new QFormLayout;
    auto *intensity = new QDoubleSpinBox; intensity->setRange(0.05, 1); intensity->setSingleStep(0.05); intensity->setValue(manager_.overallIntensity());
    QObject::connect(intensity, &QDoubleSpinBox::valueChanged, this, [this](double value) { manager_.setOverallIntensity(value); });
    form->addRow("Overall intensity", intensity);
    auto *maximum = new QSpinBox; maximum->setRange(1, 4); maximum->setValue(manager_.maxSimultaneous());
    QObject::connect(maximum, &QSpinBox::valueChanged, this, [this](int value) { manager_.setMaxSimultaneous(value); });
    form->addRow("Maximum simultaneous", maximum);
    layout->addLayout(form);
    auto *chaosGroup = new QGroupBox("Chaos Protocol limits"); auto *chaosForm = new QFormLayout(chaosGroup);
    const struct { const char *key; const char *label; int low; int high; int initial; } limits[] = {
        {"minimum", "Minimum effects", 1, 4, 2}, {"maximum", "Maximum effects", 1, 4, 3}, {"durationMs", "Duration (ms)", 1000, 15000, 7000}};
    for (const auto &limit : limits) {
        auto *spin = new QSpinBox; spin->setRange(limit.low, limit.high);
        const QString key = QString("chaos/") + limit.key;
        spin->setValue(manager_.settings().value(key, limit.initial).toInt());
        QObject::connect(spin, &QSpinBox::valueChanged, this, [this, key](int value) { manager_.settings().setValue(key, value); });
        chaosForm->addRow(limit.label, spin);
    }
    auto *combined = new QDoubleSpinBox; combined->setRange(0.1, 3); combined->setSingleStep(0.1);
    combined->setValue(manager_.settings().value("chaos/maxCombinedIntensity", 1.8).toDouble());
    QObject::connect(combined, &QDoubleSpinBox::valueChanged, this, [this](double value) { manager_.settings().setValue("chaos/maxCombinedIntensity", value); });
    chaosForm->addRow("Combined intensity cap", combined);
    chaosForm->addRow(note("Crash and Jumpscare run alone. Chaos respects enabled effects, weights, cooldowns, and the simultaneous limit."));
    layout->addWidget(chaosGroup);
    layout->addWidget(note("Audio is not included in v1.0.0. All effects are visual and silent."));
    layout->addStretch(); return widget;
}

QWidget *FxDock::library()
{
    auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget);
    layout->addWidget(note("Effect intensity controls that effect's size, density, darkness, or motion. It is multiplied by the Overall intensity master control. Random weight is a relative chance used only by Random Effect and Chaos Protocol; a weight of 2 is selected about twice as often as a weight of 1."));
    for (int index = 0; index < manager_.definitions().size(); ++index) {
        const auto &def = manager_.definitions()[index];
        auto *group = new QGroupBox(def.name); auto *form = new QFormLayout(group);
        form->addRow(note(def.description));
        auto *enabled = new QCheckBox("Enabled"); enabled->setChecked(def.enabled);
        QObject::connect(enabled, &QCheckBox::toggled, this, [this, index](bool value) {
            auto &effect = manager_.definitions()[index]; effect.enabled = value;
            if (!value) manager_.cancel(effect.id); manager_.saveSettings();
        });
        form->addRow(enabled);
        auto *duration = new QDoubleSpinBox; duration->setRange(def.minDurationMs / 1000.0, def.maxDurationMs / 1000.0);
        duration->setSuffix(" s"); duration->setSingleStep(0.5); duration->setValue(def.durationMs / 1000.0);
        QObject::connect(duration, &QDoubleSpinBox::valueChanged, this, [this, index](double value) { manager_.definitions()[index].durationMs = static_cast<int>(value * 1000); manager_.saveSettings(); });
        form->addRow("Duration", duration);
        auto *cooldown = new QSpinBox; cooldown->setRange(1, 600); cooldown->setSuffix(" s"); cooldown->setValue(def.cooldownMs / 1000);
        QObject::connect(cooldown, &QSpinBox::valueChanged, this, [this, index](int value) { manager_.definitions()[index].cooldownMs = value * 1000; manager_.saveSettings(); });
        form->addRow("Cooldown", cooldown);
        auto *intensity = new QDoubleSpinBox; intensity->setRange(0.05, 1); intensity->setSingleStep(0.05); intensity->setValue(def.intensity);
        QObject::connect(intensity, &QDoubleSpinBox::valueChanged, this, [this, index](double value) { manager_.definitions()[index].intensity = value; manager_.saveSettings(); });
        form->addRow("Effect intensity", intensity);
        auto *weight = new QDoubleSpinBox; weight->setRange(0, 100); weight->setSingleStep(0.5); weight->setValue(def.weight);
        QObject::connect(weight, &QDoubleSpinBox::valueChanged, this, [this, index](double value) { manager_.definitions()[index].weight = value; manager_.saveSettings(); });
        form->addRow("Random weight (relative)", weight);
        if (!def.exclusive) {
            auto *allow = new QCheckBox("Allowed in Chaos Protocol"); const QString key = "chaos/allowed/" + def.id;
            allow->setChecked(manager_.settings().value(key, true).toBool());
            QObject::connect(allow, &QCheckBox::toggled, this, [this, key](bool value) { manager_.settings().setValue(key, value); }); form->addRow(allow);
        }
        if (def.id == "crash") {
            auto *style = new QComboBox; style->addItems({"Cyan recovery screen", "Error panel", "Terminal recovery"}); style->setCurrentIndex(def.parameters.value("style").toInt());
            QObject::connect(style, &QComboBox::currentIndexChanged, this, [this, index](int value) { manager_.definitions()[index].parameters.insert("style", value); manager_.saveSettings(); }); form->addRow("Style", style);
        }
        if (def.id == "ink_splatter") {
            auto *count = new QSpinBox; count->setRange(1, 12); count->setValue(def.parameters.value("splatters").toInt());
            QObject::connect(count, &QSpinBox::valueChanged, this, [this, index](int value) { manager_.definitions()[index].parameters.insert("splatters", value); manager_.saveSettings(); }); form->addRow("Splatters", count);
            auto *opacity = new QDoubleSpinBox; opacity->setRange(0.1, 1.0); opacity->setSingleStep(0.05); opacity->setDecimals(2);
            opacity->setValue(def.parameters.value("opacity").toDouble(1.0));
            QObject::connect(opacity, &QDoubleSpinBox::valueChanged, this, [this, index](double value) { manager_.definitions()[index].parameters.insert("opacity", value); manager_.saveSettings(); }); form->addRow("Ink opacity", opacity);
            auto *drips = new QCheckBox("Animate drips"); drips->setChecked(def.parameters.value("drips").toBool());
            QObject::connect(drips, &QCheckBox::toggled, this, [this, index](bool value) { manager_.definitions()[index].parameters.insert("drips", value); manager_.saveSettings(); }); form->addRow(drips);
        }
        auto *test = new QPushButton("Test on enabled outputs"); const QString id = def.id;
        QObject::connect(test, &QPushButton::clicked, this, [this, id] { manager_.trigger(id); }); form->addRow(test);
        layout->addWidget(group);
    }
    layout->addStretch(); return widget;
}

QWidget *FxDock::display()
{
    auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget);
    auto *desktop = new QCheckBox("Show on my monitor"); desktop->setChecked(manager_.desktopEnabled());
    QObject::connect(desktop, &QCheckBox::toggled, this, [this](bool value) { manager_.setDesktopEnabled(value); }); layout->addWidget(desktop);
    auto *stream = new QCheckBox("Show on stream"); stream->setChecked(manager_.streamEnabled());
    QObject::connect(stream, &QCheckBox::toggled, this, [this](bool value) { manager_.setStreamEnabled(value); }); layout->addWidget(stream);
    layout->addWidget(note("Target gaming monitor")); monitors_ = new QComboBox; layout->addWidget(monitors_); refreshMonitors();
    QObject::connect(monitors_, &QComboBox::currentIndexChanged, this, [this](int) { manager_.setMonitorName(monitors_->currentData().toString()); });
    displayLabel_ = note(manager_.displayDescription()); layout->addWidget(displayLabel_);
    auto *addSource = new QPushButton("Add FX source to current scene");
    QObject::connect(addSource, &QPushButton::clicked, this, [this] {
        obs_source_t *sceneSource = obs_frontend_get_current_scene();
        obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
        if (!scene) { if (sceneSource) obs_source_release(sceneSource); return; }
        obs_source_t *source = obs_get_source_by_name("ExcelProtocol FX");
        if (source && QString::fromUtf8(obs_source_get_id(source)) != "excelprotocol_fx") { obs_source_release(source); source = nullptr; }
        if (!source) source = obs_source_create("excelprotocol_fx", "ExcelProtocol FX", nullptr, nullptr);
        if (source) {
            struct Find { obs_source_t *source; bool found; } find{source, false};
            obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *context) {
                auto *state = static_cast<Find *>(context);
                if (obs_sceneitem_get_source(item) == state->source) state->found = true;
                return !state->found;
            }, &find);
            if (!find.found) {
                obs_sceneitem_t *item = obs_scene_add(scene, source);
                if (item) {
                    obs_video_info video{};
                    if (obs_get_video_info(&video)) {
                        vec2 bounds{static_cast<float>(video.base_width), static_cast<float>(video.base_height)};
                        obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER); obs_sceneitem_set_bounds(item, &bounds);
                    }
                    obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
                }
            }
            obs_source_release(source);
        }
        obs_source_release(sceneSource); refresh();
    });
    layout->addWidget(addSource);
    layout->addWidget(note("Keep the ExcelProtocol FX source above gameplay in every scene that needs effects. Game Capture alone does not capture this desktop overlay. Display Capture may already include it; avoid showing the effect twice."));
    layout->addWidget(note("Windows desktop composition supports windowed and borderless games. Exclusive fullscreen and protected surfaces are not guaranteed. Frames are shared between desktop and OBS, with up to one render frame of presentation delay."));
    layout->addStretch(); return widget;
}

QWidget *FxDock::integration()
{
    auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget);
    layout->addWidget(note("Local command tester\nPaste an ExcelProtocol command to validate it and trigger the same effect engine used by the dock. No network listener is opened."));
    layout->addWidget(note("Remote TLS WebSocket transport and OBS WebSocket vendor registration are not included in v1.0.0. The command API is ready for a future authenticated transport; your existing bot is unchanged."));
    auto *editor = new QPlainTextEdit;
    editor->setPlainText("{\n  \"type\": \"trigger_effect\",\n  \"event_id\": \"test-001\",\n  \"effect\": \"ink_splatter\",\n  \"duration_ms\": 8000,\n  \"intensity\": 0.7,\n  \"parameters\": {\"splatters\": 5}\n}");
    editor->setMaximumBlockCount(200); layout->addWidget(editor);
    auto *send = new QPushButton("Run local command"); layout->addWidget(send);
    auto *response = new QPlainTextEdit; response->setReadOnly(true); response->setMaximumBlockCount(1000); layout->addWidget(response);
    QObject::connect(send, &QPushButton::clicked, this, [this, editor, response] {
        const auto bytes = editor->toPlainText().toUtf8();
        if (bytes.size() > 16384) { response->setPlainText("Command exceeds 16 KiB."); return; }
        QJsonParseError error{}; const auto json = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()) { response->setPlainText("Invalid JSON object."); return; }
        response->setPlainText(QString::fromUtf8(QJsonDocument(manager_.command(json.object())).toJson(QJsonDocument::Indented)));
    });
    layout->addWidget(note("Mutation commands require a unique event_id (up to 128 characters). Duration, intensity, enabled states, cooldowns, duplicate IDs and a 16-entry queue are enforced."));
    return widget;
}

void FxDock::refreshMonitors()
{
    if (!monitors_) return;
    const QSignalBlocker blocker(monitors_);
    monitors_->clear();
    monitors_->addItem("Primary display", "");
    for (auto *screen : QGuiApplication::screens()) {
        const QSize size = screen->geometry().size() * screen->devicePixelRatio();
        monitors_->addItem(QString("%1 - %2 x %3 (%4% scaling)").arg(screen->name()).arg(size.width()).arg(size.height()).arg(qRound(screen->devicePixelRatio() * 100)), screen->name());
    }
    const int index = monitors_->findData(manager_.monitorName()); monitors_->setCurrentIndex(index < 0 ? 0 : index);
}

void FxDock::refresh()
{
    if (activeLabel_) {
        QStringList names;
        for (const auto &effect : manager_.active()) for (const auto &def : manager_.definitions()) if (effect.id == def.id) names << def.name;
        activeLabel_->setText(names.isEmpty() ? "Active effects: none" : "Active effects: " + names.join(", "));
    }
    if (displayLabel_) displayLabel_->setText(manager_.displayDescription());
}
} // namespace fx
