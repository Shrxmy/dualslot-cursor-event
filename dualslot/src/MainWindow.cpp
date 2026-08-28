#include "MainWindow.h"

#include "DsShell.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

namespace dualslot {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_slots(this), m_emu(m_firmware, this)
{
    setWindowTitle(QStringLiteral("DualSlot DS"));
    resize(700, 980);
    m_shell = new DsShell(this);
    setCentralWidget(m_shell);
    buildMenus();
    statusBar()->showMessage(m_firmware.summary());

    connect(&m_slots, &SlotManager::slotsChanged, m_shell, &DsShell::setSlots);
    connect(&m_slots, &SlotManager::slotsChanged, &m_emu, &EmuThread::requestSlots);
    connect(&m_slots, &SlotManager::slotsChanged, this, [this] { rebuildRecentMenu(); });
    connect(&m_slots, &SlotManager::osdRequested, m_shell, &DsShell::showOsd);
    connect(m_shell, &DsShell::keysChanged, &m_emu, &EmuThread::setKeys);
    connect(m_shell, &DsShell::touchChanged, &m_emu, &EmuThread::setTouch);
    connect(m_shell, &DsShell::romDropped, this, [this](const QString& path) { attachRom(path); });
    connect(&m_emu, &EmuThread::frameReady, m_shell, &DsShell::setFrame, Qt::QueuedConnection);
    connect(&m_emu, &EmuThread::coreChanged, this, [this](ActiveCore core, const QString& title) {
        m_activeCore = core;
        m_shell->setCore(core, title);
        const QString suffix = title.isEmpty() ? QString() : QStringLiteral(" — %1").arg(title);
        setWindowTitle(QStringLiteral("DualSlot DS%1").arg(suffix));
    });
    connect(&m_emu, &EmuThread::osdRequested, m_shell, &DsShell::showOsd);
    connect(&m_emu, &EmuThread::errorRaised, this, [this](const QString& error) { reportAttachError(error); });
    connect(&m_emu, &EmuThread::stateOperationFinished, this, [this](bool ok, const QString& message) {
        m_shell->showOsd(message);
        if (!ok)
            QMessageBox::warning(this, QStringLiteral("Savestate"), message);
    });

    m_emu.start();
    m_emu.requestSlots(m_slots.state());
}

MainWindow::~MainWindow()
{
    m_emu.shutdown();
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Cartridge"));
    QAction* slot1 = file->addAction(QStringLiteral("Insert &Slot-1 DS card…"), this, &MainWindow::chooseSlot1);
    slot1->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    QAction* slot2 = file->addAction(QStringLiteral("Insert Slot-&2 cartridge…"), this, &MainWindow::chooseSlot2);
    slot2->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    file->addAction(QStringLiteral("Eject Slot-1"), &m_slots, &SlotManager::ejectSlot1);
    file->addAction(QStringLiteral("Eject Slot-2"), &m_slots, &SlotManager::ejectSlot2);
    m_recentMenu = file->addMenu(QStringLiteral("Recent cartridges"));
    rebuildRecentMenu();
    file->addSeparator();
    QAction* exitAction = file->addAction(QStringLiteral("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);

    QMenu* emulation = menuBar()->addMenu(QStringLiteral("&Emulation"));
    QAction* pause = emulation->addAction(QStringLiteral("&Pause"));
    pause->setCheckable(true);
    pause->setShortcut(Qt::Key_Space);
    connect(pause, &QAction::toggled, this, [this](bool checked) {
        m_emu.setPaused(checked);
        m_shell->showOsd(checked ? QStringLiteral("Paused") : QStringLiteral("Resumed"));
    });
    QAction* reset = emulation->addAction(QStringLiteral("&Reset"), &m_emu, &EmuThread::requestReset);
    reset->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    QAction* fast = emulation->addAction(QStringLiteral("Fast forward"));
    fast->setCheckable(true);
    fast->setShortcut(Qt::Key_Tab);
    connect(fast, &QAction::toggled, &m_emu, &EmuThread::setFastForward);

    QMenu* saveMenu = emulation->addMenu(QStringLiteral("Save state"));
    QMenu* loadMenu = emulation->addMenu(QStringLiteral("Load state"));
    for (int i = 0; i < 10; ++i) {
        QAction* save = saveMenu->addAction(QStringLiteral("Slot %1").arg(i));
        QAction* load = loadMenu->addAction(QStringLiteral("Slot %1").arg(i));
        if (i == 0) {
            save->setShortcut(QKeySequence(QStringLiteral("F5")));
            load->setShortcut(QKeySequence(QStringLiteral("F8")));
        }
        connect(save, &QAction::triggered, this, [this, i] { saveState(i); });
        connect(load, &QAction::triggered, this, [this, i] { loadState(i); });
    }

    QMenu* view = menuBar()->addMenu(QStringLiteral("&View"));
    auto* layouts = new QActionGroup(this);
    layouts->setExclusive(true);
    const QList<QPair<QString, ScreenMode>> layoutItems {
        {QStringLiteral("Vertical"), ScreenMode::Vertical},
        {QStringLiteral("Horizontal"), ScreenMode::Horizontal},
        {QStringLiteral("Hybrid"), ScreenMode::Hybrid},
        {QStringLiteral("Top screen only"), ScreenMode::TopOnly},
        {QStringLiteral("Bottom screen only"), ScreenMode::BottomOnly},
    };
    for (const auto& [name, mode] : layoutItems) {
        QAction* action = view->addAction(name);
        action->setCheckable(true);
        action->setChecked(mode == ScreenMode::Vertical);
        layouts->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            m_shell->setLayout(mode);
            QSettings().setValue(QStringLiteral("video/layout"), static_cast<int>(mode));
        });
    }
    QAction* integerScale = view->addAction(QStringLiteral("Integer scale"));
    integerScale->setCheckable(true);
    integerScale->setChecked(true);
    connect(integerScale, &QAction::toggled, m_shell, &DsShell::setIntegerScale);
    view->addSeparator();
    QAction* fullscreen = view->addAction(QStringLiteral("Fullscreen"));
    fullscreen->setCheckable(true);
    fullscreen->setShortcut(Qt::Key_F11);
    connect(fullscreen, &QAction::toggled, this, [this](bool enabled) {
        if (enabled) showFullScreen(); else showNormal();
    });

    QMenu* help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Controls"), this, [this] {
        QMessageBox::information(this, QStringLiteral("Controls"),
            QStringLiteral("D-pad: Arrow keys\nA/B: X/Z\nX/Y: Q/W\nL/R: A/S\nStart: Enter\nSelect: Backspace or Shift\nTouch: mouse on lower screen\nPause: Space · Fast forward: Tab · Fullscreen: F11"));
    });
    help->addAction(QStringLiteral("About DualSlot"), this, [this] {
        QMessageBox::about(this, QStringLiteral("About DualSlot"),
            QStringLiteral("DualSlot %1\nA GPL-3 device shell using unmodified melonDS and mGBA libraries.\n\nNo Nintendo firmware or game data is distributed with this application.").arg(QStringLiteral(DUALSLOT_VERSION)));
    });
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList recents = m_slots.recent();
    if (recents.isEmpty()) {
        QAction* empty = m_recentMenu->addAction(QStringLiteral("(none)"));
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : recents) {
        QAction* action = m_recentMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { attachRom(path); });
    }
}

void MainWindow::chooseSlot1()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Insert Slot-1 card"), {},
        QStringLiteral("Nintendo DS ROMs (*.nds *.dsi *.ids);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_slots.insertSlot1(path, error)) reportAttachError(error);
}

void MainWindow::chooseSlot2()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Insert Slot-2 cartridge"), {},
        QStringLiteral("Game Boy cartridges (*.gba *.gb *.gbc);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (!m_slots.insertSlot2(path, error)) reportAttachError(error);
}

bool MainWindow::attachRom(const QString& path)
{
    QString error;
    if (!m_slots.attachAutomatically(path, error)) {
        reportAttachError(error);
        return false;
    }
    return true;
}

void MainWindow::reportAttachError(const QString& error)
{
    if (!error.isEmpty()) {
        m_shell->showOsd(error);
        QMessageBox::critical(this, QStringLiteral("Cartridge error"), error);
    }
}

QString MainWindow::statePath(int slot) const
{
    const SlotState state = m_slots.state();
    const QString rom = m_activeCore == ActiveCore::Melon ? state.slot1 : state.slot2;
    if (rom.isEmpty()) return {};
    const QString root = QSettings().value(QStringLiteral("saves/path"), QDir::current().filePath(QStringLiteral("saves"))).toString();
    const QString coreName = m_activeCore == ActiveCore::Melon ? QStringLiteral("melonds") : QStringLiteral("mgba");
    return QDir(root).filePath(QStringLiteral("%1/%2-slot%3.dst").arg(QFileInfo(rom).completeBaseName(), coreName).arg(slot));
}

void MainWindow::saveState(int slot)
{
    const QString path = statePath(slot);
    if (path.isEmpty()) { m_shell->showOsd(QStringLiteral("No game is running")); return; }
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_shell->grab().save(path + QStringLiteral(".png"), "PNG");
    m_emu.requestSaveState(path);
}

void MainWindow::loadState(int slot)
{
    const QString path = statePath(slot);
    if (path.isEmpty()) { m_shell->showOsd(QStringLiteral("No game is running")); return; }
    m_emu.requestLoadState(path);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_emu.shutdown();
    event->accept();
}

} // namespace dualslot
