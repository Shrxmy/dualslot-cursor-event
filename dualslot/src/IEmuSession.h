#pragma once

#include "Types.h"

#include <QString>

namespace dualslot {

class IEmuSession {
public:
    virtual ~IEmuSession() = default;

    virtual bool start(const SlotState& slots, QString& error) = 0;
    virtual void stop() = 0;
    virtual FramePacket frame() = 0;
    virtual void setKeys(std::uint32_t pressed) = 0;
    virtual void setTouch(const TouchPoint& touch) = 0;
    virtual bool flushSave(QString& error) = 0;
    virtual bool saveState(const QString& path, QString& error) = 0;
    virtual bool loadState(const QString& path, QString& error) = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual ActiveCore core() const noexcept = 0;
    [[nodiscard]] virtual QString title() const = 0;
    [[nodiscard]] virtual bool gbaModeRequested() const noexcept { return false; }
};

} // namespace dualslot
