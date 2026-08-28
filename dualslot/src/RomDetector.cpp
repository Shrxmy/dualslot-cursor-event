#include "RomDetector.h"

#include <QFile>
#include <QFileInfo>

#include <array>

namespace dualslot {
namespace {

bool hasPrefix(const QByteArray& data, int offset, std::initializer_list<unsigned char> bytes)
{
    if (offset < 0 || data.size() < offset + static_cast<int>(bytes.size()))
        return false;
    int i = offset;
    for (const auto byte : bytes) {
        if (static_cast<unsigned char>(data.at(i++)) != byte)
            return false;
    }
    return true;
}

QString headerText(const QByteArray& data, int offset, int length)
{
    QByteArray raw = data.mid(offset, length);
    const auto nul = raw.indexOf('\0');
    if (nul >= 0)
        raw.truncate(nul);
    return QString::fromLatin1(raw).trimmed();
}

bool validGba(const QByteArray& data)
{
    if (data.size() < 0xC0 || !hasPrefix(data, 0x04, {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21}) ||
        static_cast<unsigned char>(data.at(0xB2)) != 0x96)
        return false;

    unsigned sum = 0;
    for (int i = 0xA0; i <= 0xBC; ++i)
        sum += static_cast<unsigned char>(data.at(i));
    sum += static_cast<unsigned char>(data.at(0xBD)) + 0x19;
    return (sum & 0xFFu) == 0;
}

bool validGb(const QByteArray& data)
{
    if (data.size() < 0x150 || !hasPrefix(data, 0x104, {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D}))
        return false;
    unsigned checksum = 0;
    for (int i = 0x134; i <= 0x14C; ++i)
        checksum = (checksum - static_cast<unsigned char>(data.at(i)) - 1u) & 0xFFu;
    return checksum == static_cast<unsigned char>(data.at(0x14D));
}

std::uint16_t crc16(const QByteArray& data, int offset, int length)
{
    std::uint16_t crc = 0xFFFF;
    for (int i = 0; i < length; ++i) {
        crc ^= static_cast<unsigned char>(data.at(offset + i));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
    }
    return crc;
}

bool validNds(const QByteArray& data)
{
    if (data.size() < 0x160 || !hasPrefix(data, 0xC0, {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21}))
        return false;
    const auto logoCrc = static_cast<std::uint16_t>(static_cast<unsigned char>(data.at(0x15C)) |
                                                    (static_cast<unsigned char>(data.at(0x15D)) << 8));
    const auto headerCrc = static_cast<std::uint16_t>(static_cast<unsigned char>(data.at(0x15E)) |
                                                      (static_cast<unsigned char>(data.at(0x15F)) << 8));
    return crc16(data, 0xC0, 0x9C) == logoCrc && crc16(data, 0, 0x15E) == headerCrc;
}

} // namespace

RomProbe RomDetector::probe(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {RomType::Unknown, {}, QStringLiteral("Cannot open %1: %2").arg(path, file.errorString())};

    const QByteArray header = file.read(0x200);
    const QString suffix = QFileInfo(path).suffix().toLower();
    RomProbe result;

    if (validNds(header)) {
        const auto unitCode = static_cast<unsigned char>(header.at(0x12));
        result.type = (unitCode == 2 || unitCode == 3 || suffix == QStringLiteral("dsi") || suffix == QStringLiteral("ids"))
            ? RomType::Dsi : RomType::Nds;
        result.title = headerText(header, 0, 12);
    } else if (validGba(header)) {
        result.type = RomType::Gba;
        result.title = headerText(header, 0xA0, 12);
    } else if (validGb(header)) {
        const auto cgb = static_cast<unsigned char>(header.at(0x143));
        result.type = (cgb == 0x80 || cgb == 0xC0 || suffix == QStringLiteral("gbc")) ? RomType::Gbc : RomType::Gb;
        result.title = headerText(header, 0x134, cgb == 0x80 || cgb == 0xC0 ? 15 : 16);
    } else {
        result.error = QStringLiteral("%1 does not contain a valid Nintendo DS, GBA, or Game Boy header.")
                           .arg(QFileInfo(path).fileName());
        return result;
    }

    const bool extensionFits =
        ((result.type == RomType::Nds || result.type == RomType::Dsi) &&
         (suffix == QStringLiteral("nds") || suffix == QStringLiteral("dsi") || suffix == QStringLiteral("ids"))) ||
        (result.type == RomType::Gba && suffix == QStringLiteral("gba")) ||
        ((result.type == RomType::Gb || result.type == RomType::Gbc) &&
         (suffix == QStringLiteral("gb") || suffix == QStringLiteral("gbc")));
    if (!extensionFits)
        result.error = QStringLiteral("The file header is %1 but the .%2 extension does not match.")
                           .arg(typeName(result.type), suffix.isEmpty() ? QStringLiteral("(none)") : suffix);
    return result;
}

QString RomDetector::typeName(RomType type)
{
    switch (type) {
    case RomType::Nds: return QStringLiteral("Nintendo DS");
    case RomType::Dsi: return QStringLiteral("Nintendo DSi");
    case RomType::Gba: return QStringLiteral("Game Boy Advance");
    case RomType::Gb: return QStringLiteral("Game Boy");
    case RomType::Gbc: return QStringLiteral("Game Boy Color");
    default: return QStringLiteral("unknown ROM");
    }
}

} // namespace dualslot
