#pragma once

#include "Types.h"

#include <QObject>
#include <QStringList>

namespace dualslot {

class SlotManager final : public QObject {
    Q_OBJECT
public:
    explicit SlotManager(QObject* parent = nullptr);

    [[nodiscard]] SlotState state() const { return m_state; }
    [[nodiscard]] QStringList recent() const;

    bool insertSlot1(const QString& path, QString& error);
    bool insertSlot2(const QString& path, QString& error);
    bool attachAutomatically(const QString& path, QString& error);
    void ejectSlot1();
    void ejectSlot2();

signals:
    void slotsChanged(const dualslot::SlotState& state);
    void osdRequested(const QString& message);

private:
    void addRecent(const QString& path);

    SlotState m_state;
};

} // namespace dualslot
