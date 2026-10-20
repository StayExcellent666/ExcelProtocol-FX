#pragma once

#include <QWidget>

class QLabel;
class QComboBox;

namespace fx {
class EffectManager;
class UpdateController;

class FxDock final : public QWidget {
public:
    explicit FxDock(EffectManager &manager, QWidget *parent = nullptr);
    ~FxDock() override;
    void shutdown();
    void refresh();
private:
    QWidget *dashboard();
    QWidget *library();
    QWidget *advanced();
    QWidget *randomSettings();
    QWidget *display();
    QWidget *integration();
    void refreshMonitors();

    EffectManager &manager_;
    UpdateController *updates_ = nullptr;
    QLabel *activeLabel_ = nullptr;
    QLabel *resultLabel_ = nullptr;
    QLabel *displayLabel_ = nullptr;
    QComboBox *monitors_ = nullptr;
    bool stopped_ = false;
};
} // namespace fx
