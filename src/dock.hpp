#pragma once

#include <QWidget>

class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace fx {
class EffectManager;
class RemoteClient;
class UpdateController;

class FxDock final : public QWidget {
public:
    explicit FxDock(EffectManager &manager, QWidget *parent = nullptr);
    ~FxDock() override;
    void shutdown();
    void refresh();
private:
    QWidget *dashboard();
    QWidget *raid();
    QWidget *library();
    QWidget *advanced();
    QWidget *randomSettings();
    QWidget *display();
    QWidget *integration();
    void refreshMonitors();
    void refreshConnection();

    EffectManager &manager_;
    RemoteClient *remote_ = nullptr;
    UpdateController *updates_ = nullptr;
    QLabel *activeLabel_ = nullptr;
    QLabel *resultLabel_ = nullptr;
    QLabel *raidLabel_ = nullptr;
    QLabel *displayLabel_ = nullptr;
    QComboBox *monitors_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QLabel *deviceLabel_ = nullptr;
    QLineEdit *pairingCode_ = nullptr;
    QLineEdit *deviceName_ = nullptr;
    QPushButton *pairButton_ = nullptr;
    QPushButton *reconnectButton_ = nullptr;
    QPushButton *forgetButton_ = nullptr;
    QPushButton *testRaidButton_ = nullptr;
    QPushButton *stopRaidButton_ = nullptr;
    bool stopped_ = false;
};
} // namespace fx
