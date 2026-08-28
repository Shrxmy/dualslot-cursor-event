#include "FirmwareStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>

#include <utility>

namespace dualslot {
namespace {

const QHash<QString, QStringList>& expectedNames()
{
    static const QHash<QString, QStringList> names {
        {QStringLiteral("bios7"), {QStringLiteral("bios7.bin"), QStringLiteral("biosnds7.bin")}},
        {QStringLiteral("bios9"), {QStringLiteral("bios9.bin"), QStringLiteral("biosnds9.bin")}},
        {QStringLiteral("firmware"), {QStringLiteral("firmware.bin"), QStringLiteral("dsfirmware.bin")}},
        {QStringLiteral("bios7i"), {QStringLiteral("bios7i.bin"), QStringLiteral("biosdsi7.bin")}},
        {QStringLiteral("bios9i"), {QStringLiteral("bios9i.bin"), QStringLiteral("biosdsi9.bin")}},
        {QStringLiteral("dsiFirmware"), {QStringLiteral("firmwarei.bin"), QStringLiteral("dsifirmware.bin"), QStringLiteral("dsi_firmware.bin")}},
        {QStringLiteral("nand"), {QStringLiteral("nand.bin"), QStringLiteral("dsinand.bin"), QStringLiteral("dsi_nand.bin")}},
        {QStringLiteral("gbaBios"), {QStringLiteral("gba_bios.bin"), QStringLiteral("biosgba.bin")}},
        {QStringLiteral("gbBios"), {QStringLiteral("dmg_boot.bin"), QStringLiteral("gb_bios.bin")}},
        {QStringLiteral("gbcBios"), {QStringLiteral("cgb_boot.bin"), QStringLiteral("gbc_bios.bin")}},
    };
    return names;
}

} // namespace

FirmwareStore::FirmwareStore()
{
    const QDir exeDir(QCoreApplication::applicationDirPath());
    m_roots << exeDir.filePath(QStringLiteral("firmware"));
    m_roots << QDir::current().filePath(QStringLiteral("firmware"));
    m_roots << QDir::current().absoluteFilePath(QStringLiteral("../firmware"));

    QSettings settings;
    m_roots.append(settings.value(QStringLiteral("firmware/searchPaths")).toStringList());
    m_roots.removeDuplicates();
    discover();
}

void FirmwareStore::discover()
{
    m_paths.clear();
    QHash<QString, QString> files;
    for (const QString& root : std::as_const(m_roots)) {
        if (!QFileInfo::exists(root))
            continue;
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString candidate = it.next();
            const QString name = QFileInfo(candidate).fileName().toLower();
            if (!files.contains(name))
                files.insert(name, QFileInfo(candidate).absoluteFilePath());
        }
    }

    for (auto it = expectedNames().constBegin(); it != expectedNames().constEnd(); ++it) {
        for (const QString& name : it.value()) {
            if (files.contains(name)) {
                m_paths.insert(it.key(), files.value(name));
                break;
            }
        }
    }
}

QString FirmwareStore::path(const QString& key) const
{
    return m_paths.value(key);
}

bool FirmwareStore::hasDsFirmware() const
{
    return m_paths.contains(QStringLiteral("bios7")) && m_paths.contains(QStringLiteral("bios9")) &&
           m_paths.contains(QStringLiteral("firmware"));
}

bool FirmwareStore::hasDsiFirmware() const
{
    return hasDsFirmware() && m_paths.contains(QStringLiteral("bios7i")) &&
           m_paths.contains(QStringLiteral("bios9i")) && m_paths.contains(QStringLiteral("dsiFirmware")) &&
           m_paths.contains(QStringLiteral("nand"));
}

QString FirmwareStore::summary() const
{
    if (hasDsiFirmware())
        return QStringLiteral("DS and DSi firmware discovered");
    if (hasDsFirmware())
        return QStringLiteral("DS firmware discovered; DSi titles will use DS mode");
    return QStringLiteral("Using melonDS FreeBIOS/direct boot; add legal dumps to firmware/");
}

} // namespace dualslot
