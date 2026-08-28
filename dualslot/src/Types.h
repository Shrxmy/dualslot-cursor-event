#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <cstdint>

namespace dualslot {

enum class RomType { Unknown, Nds, Dsi, Gba, Gb, Gbc };
enum class ActiveCore { Idle, Mgba, Melon };
enum class ScreenMode { Vertical, Horizontal, TopOnly, BottomOnly, Hybrid };

struct SlotState {
    QString slot1;
    QString slot2;
    RomType slot1Type = RomType::Unknown;
    RomType slot2Type = RomType::Unknown;

    [[nodiscard]] ActiveCore desiredCore() const noexcept
    {
        if (!slot1.isEmpty() && (slot1Type == RomType::Nds || slot1Type == RomType::Dsi))
            return ActiveCore::Melon;
        if (slot1.isEmpty() && !slot2.isEmpty() &&
            (slot2Type == RomType::Gba || slot2Type == RomType::Gb || slot2Type == RomType::Gbc))
            return ActiveCore::Mgba;
        return ActiveCore::Idle;
    }
};

// Button bit positions intentionally match both mGBA and melonDS.
enum Button : std::uint32_t {
    A = 1u << 0,
    B = 1u << 1,
    Select = 1u << 2,
    Start = 1u << 3,
    Right = 1u << 4,
    Left = 1u << 5,
    Up = 1u << 6,
    Down = 1u << 7,
    R = 1u << 8,
    L = 1u << 9,
    X = 1u << 10,
    Y = 1u << 11,
};

struct TouchPoint {
    bool down = false;
    int x = 0;
    int y = 0;
};

struct FramePacket {
    QImage top;
    QImage bottom;
    QByteArray audio; // signed, interleaved 16-bit stereo
    ActiveCore core = ActiveCore::Idle;
    QString detail;
};

} // namespace dualslot

Q_DECLARE_METATYPE(dualslot::SlotState)
Q_DECLARE_METATYPE(dualslot::FramePacket)
Q_DECLARE_METATYPE(dualslot::ActiveCore)
