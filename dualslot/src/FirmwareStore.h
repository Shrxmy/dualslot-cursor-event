#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace dualslot {

class FirmwareStore final {
public:
    FirmwareStore();

    void discover();
    [[nodiscard]] QString path(const QString& key) const;
    [[nodiscard]] bool hasDsFirmware() const;
    [[nodiscard]] bool hasDsiFirmware() const;
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QStringList roots() const { return m_roots; }

private:
    QStringList m_roots;
    QHash<QString, QString> m_paths;
};

} // namespace dualslot
