#pragma once

#include "Types.h"

#include <QString>

namespace dualslot {

struct RomProbe {
    RomType type = RomType::Unknown;
    QString title;
    QString error;
};

class RomDetector final {
public:
    static RomProbe probe(const QString& path);
    static QString typeName(RomType type);
};

} // namespace dualslot
