#pragma once

#include "Types.h"

#include <QThread>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace dualslot {

class FirmwareStore;
class IEmuSession;

class EmuThread final : public QThread {
    Q_OBJECT
public:
    explicit EmuThread(const FirmwareStore& firmware, QObject* parent = nullptr);
    ~EmuThread() override;

    void requestSlots(const SlotState& state);
    void setKeys(std::uint32_t pressed) { m_keys.store(pressed, std::memory_order_relaxed); }
    void setTouch(const TouchPoint& touch);
    void setPaused(bool paused) { m_paused.store(paused); }
    void setFastForward(bool enabled) { m_fastForward.store(enabled); }
    void requestReset();
    void requestSaveState(const QString& path);
    void requestLoadState(const QString& path);
    void shutdown();

signals:
    void frameReady(const dualslot::FramePacket& frame);
    void coreChanged(dualslot::ActiveCore core, const QString& title);
    void osdRequested(const QString& message);
    void errorRaised(const QString& message);
    void stateOperationFinished(bool success, const QString& message);

protected:
    void run() override;

private:
    enum class CommandType { Reset, SaveState, LoadState };
    struct Command { CommandType type; QString path; };

    void switchSession(const SlotState& slots);
    void processCommand();

    const FirmwareStore& m_firmware;
    std::unique_ptr<IEmuSession> m_session;
    SlotState m_activeSlots;
    std::mutex m_mutex;
    std::optional<SlotState> m_pendingSlots;
    std::optional<Command> m_command;
    TouchPoint m_touch;
    std::atomic<std::uint32_t> m_keys {0};
    std::atomic_bool m_paused {false};
    std::atomic_bool m_fastForward {false};
    std::atomic_bool m_stopping {false};
};

} // namespace dualslot
