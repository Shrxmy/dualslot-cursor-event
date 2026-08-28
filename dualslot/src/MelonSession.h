#pragma once

#include "IEmuSession.h"
#include "MelonPlatformBridge.h"

#include <memory>

namespace melonDS { class NDS; }

namespace dualslot {

class FirmwareStore;

class MelonSession final : public IEmuSession, public MelonPlatformBridge {
public:
    explicit MelonSession(const FirmwareStore& firmware);
    ~MelonSession() override;

    bool start(const SlotState& slots, QString& error) override;
    void stop() override;
    FramePacket frame() override;
    void setKeys(std::uint32_t pressed) override;
    void setTouch(const TouchPoint& touch) override;
    bool flushSave(QString& error) override;
    bool saveState(const QString& path, QString& error) override;
    bool loadState(const QString& path, QString& error) override;
    void reset() override;
    [[nodiscard]] ActiveCore core() const noexcept override { return ActiveCore::Melon; }
    [[nodiscard]] QString title() const override { return m_title; }
    [[nodiscard]] bool gbaModeRequested() const noexcept override { return m_gbaModeRequested; }

    void melonStopped(int reason) override;
    void writeNdsSave(const std::uint8_t*, std::uint32_t) override { m_ndsSaveDirty = true; }
    void writeGbaSave(const std::uint8_t*, std::uint32_t) override { m_gbaSaveDirty = true; }
    void writeFirmware(const std::uint8_t*, std::uint32_t) override { m_firmwareDirty = true; }

private:
    bool loadSlot2(const SlotState& slots, QString& error);
    bool writeBytes(const QString& path, const std::uint8_t* data, std::uint32_t length, QString& error);

    const FirmwareStore& m_firmware;
    std::unique_ptr<melonDS::NDS> m_nds;
    SlotState m_slots;
    QString m_title;
    QString m_ndsSavePath;
    QString m_gbaSavePath;
    QString m_firmwareWritePath;
    std::uint32_t m_keys = 0;
    TouchPoint m_touch;
    bool m_usingDsi = false;
    bool m_gbaModeRequested = false;
    bool m_ndsSaveDirty = false;
    bool m_gbaSaveDirty = false;
    bool m_firmwareDirty = false;
};

} // namespace dualslot
