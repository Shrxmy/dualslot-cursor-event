#include "SlotManager.h"

#include "RomDetector.h"

#include <QFileInfo>
#include <QSettings>

namespace dualslot {

SlotManager::SlotManager(QObject* parent)
    : QObject(parent)
{
}

QStringList SlotManager::recent() const
{
    return QSettings().value(QStringLiteral("recent/roms")).toStringList();
}

bool SlotManager::insertSlot1(const QString& path, QString& error)
{
    const RomProbe probe = RomDetector::probe(path);
    if (!probe.error.isEmpty()) {
        error = probe.error;
        return false;
    }
    if (probe.type != RomType::Nds && probe.type != RomType::Dsi) {
        error = QStringLiteral("Slot-1 accepts Nintendo DS/DSi cards; %1 is %2.")
                    .arg(QFileInfo(path).fileName(), RomDetector::typeName(probe.type));
        return false;
    }
    m_state.slot1 = QFileInfo(path).absoluteFilePath();
    m_state.slot1Type = probe.type;
    addRecent(m_state.slot1);
    emit slotsChanged(m_state);
    emit osdRequested(QStringLiteral("Slot-1 inserted: %1").arg(probe.title.isEmpty() ? QFileInfo(path).fileName() : probe.title));
    return true;
}

bool SlotManager::insertSlot2(const QString& path, QString& error)
{
    const RomProbe probe = RomDetector::probe(path);
    if (!probe.error.isEmpty()) {
        error = probe.error;
        return false;
    }
    if (probe.type != RomType::Gba && probe.type != RomType::Gb && probe.type != RomType::Gbc) {
        error = QStringLiteral("Slot-2 accepts GBA, GB, and GBC cartridges; %1 is %2.")
                    .arg(QFileInfo(path).fileName(), RomDetector::typeName(probe.type));
        return false;
    }
    m_state.slot2 = QFileInfo(path).absoluteFilePath();
    m_state.slot2Type = probe.type;
    addRecent(m_state.slot2);
    emit slotsChanged(m_state);
    emit osdRequested(QStringLiteral("Slot-2 inserted: %1").arg(probe.title.isEmpty() ? QFileInfo(path).fileName() : probe.title));
    return true;
}

bool SlotManager::attachAutomatically(const QString& path, QString& error)
{
    const RomProbe probe = RomDetector::probe(path);
    if (!probe.error.isEmpty()) {
        error = probe.error;
        return false;
    }
    if (probe.type == RomType::Nds || probe.type == RomType::Dsi)
        return insertSlot1(path, error);
    return insertSlot2(path, error);
}

void SlotManager::ejectSlot1()
{
    if (m_state.slot1.isEmpty())
        return;
    m_state.slot1.clear();
    m_state.slot1Type = RomType::Unknown;
    emit slotsChanged(m_state);
    emit osdRequested(QStringLiteral("Slot-1 ejected"));
}

void SlotManager::ejectSlot2()
{
    if (m_state.slot2.isEmpty())
        return;
    m_state.slot2.clear();
    m_state.slot2Type = RomType::Unknown;
    emit slotsChanged(m_state);
    emit osdRequested(QStringLiteral("Slot-2 ejected"));
}

void SlotManager::addRecent(const QString& path)
{
    QSettings settings;
    QStringList items = settings.value(QStringLiteral("recent/roms")).toStringList();
    items.removeAll(path);
    items.prepend(path);
    while (items.size() > 10)
        items.removeLast();
    settings.setValue(QStringLiteral("recent/roms"), items);
}

} // namespace dualslot
