#pragma once

#include "EmuThread.h"
#include "FirmwareStore.h"
#include "SlotManager.h"

#include <QMainWindow>

class QMenu;

namespace dualslot {

class DsShell;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool attachRom(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void rebuildRecentMenu();
    void chooseSlot1();
    void chooseSlot2();
    void reportAttachError(const QString& error);
    QString statePath(int slot) const;
    void saveState(int slot);
    void loadState(int slot);

    FirmwareStore m_firmware;
    SlotManager m_slots;
    EmuThread m_emu;
    DsShell* m_shell = nullptr;
    QMenu* m_recentMenu = nullptr;
    ActiveCore m_activeCore = ActiveCore::Idle;
};

} // namespace dualslot
