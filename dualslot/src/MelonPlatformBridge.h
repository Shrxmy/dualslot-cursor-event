#pragma once

#include <cstdint>
#include <QString>

namespace dualslot {

struct MelonPlatformBridge {
    virtual ~MelonPlatformBridge() = default;
    virtual void melonStopped(int reason) = 0;
    virtual void writeNdsSave(const std::uint8_t* data, std::uint32_t length) = 0;
    virtual void writeGbaSave(const std::uint8_t* data, std::uint32_t length) = 0;
    virtual void writeFirmware(const std::uint8_t* data, std::uint32_t length) = 0;
};

} // namespace dualslot
