#pragma once

#include "Types.h"

#include "ScreenLayout.h"

#include <QElapsedTimer>
#include <QWidget>

namespace dualslot {

class DsShell final : public QWidget {
    Q_OBJECT
public:
    explicit DsShell(QWidget* parent = nullptr);

    void setFrame(const FramePacket& frame);
    void setSlots(const SlotState& slots);
    void setCore(ActiveCore core, const QString& title);
    void setLayout(ScreenMode mode);
    void setIntegerScale(bool enabled);
    void showOsd(const QString& message);

signals:
    void keysChanged(std::uint32_t pressed);
    void touchChanged(const dualslot::TouchPoint& touch);
    void romDropped(const QString& path);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void updateLayout();
    void updateKey(QKeyEvent* event, bool pressed);
    bool mapTouch(const QPointF& point, TouchPoint& touch, bool clamp);
    QImage gbaCanvas() const;

    FramePacket m_frame;
    SlotState m_slots;
    ActiveCore m_core = ActiveCore::Idle;
    QString m_title;
    QString m_osd;
    QElapsedTimer m_osdTimer;
    ScreenLayout m_screenLayout;
    ScreenMode m_mode = ScreenMode::Vertical;
    bool m_integerScale = true;
    std::uint32_t m_keys = 0;
    int m_margin = 30;
};

} // namespace dualslot
