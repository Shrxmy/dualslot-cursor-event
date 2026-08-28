#pragma once

#include "IEmuSession.h"

#include <QVector>

struct mCore;

namespace dualslot {

class FirmwareStore;

class MgbaSession final : public IEmuSession {
public:
    explicit MgbaSession(const FirmwareStore& firmware);
    ~MgbaSession() override;

    bool start(const SlotState& state, QString& error) override;
    void stop() override;
    FramePacket frame() override;
    void setKeys(std::uint32_t pressed) override;
    void setTouch(const TouchPoint&) override {}
    bool flushSave(QString& error) override;
    bool saveState(const QString& path, QString& error) override;
    bool loadState(const QString& path, QString& error) override;
    void reset() override;
    [[nodiscard]] ActiveCore core() const noexcept override { return ActiveCore::Mgba; }
    [[nodiscard]] QString title() const override { return m_title; }

private:
    const FirmwareStore& m_firmware;
    mCore* m_core = nullptr;
    QVector<std::uint32_t> m_video;
    unsigned m_width = 0;
    unsigned m_height = 0;
    QString m_romPath;
    QString m_savePath;
    QString m_title;
    std::uint32_t m_keys = 0;
    bool m_configInitialized = false;
};

} // namespace dualslot
