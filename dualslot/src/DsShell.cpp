#include "DsShell.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QUrl>

namespace dualslot {

DsShell::DsShell(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMinimumSize(420, 560);
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto* timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [this] {
        if (m_osdTimer.isValid() && m_osdTimer.elapsed() < 3500)
            update();
    });
    timer->start();
    updateLayout();
}

void DsShell::setFrame(const FramePacket& frame)
{
    m_frame = frame;
    update();
}

void DsShell::setSlots(const SlotState& slots)
{
    m_slots = slots;
    update();
}

void DsShell::setCore(ActiveCore core, const QString& title)
{
    m_core = core;
    m_title = title;
    update();
}

void DsShell::setLayout(ScreenMode mode)
{
    m_mode = mode;
    updateLayout();
    update();
}

void DsShell::setIntegerScale(bool enabled)
{
    m_integerScale = enabled;
    updateLayout();
    update();
}

void DsShell::showOsd(const QString& message)
{
    m_osd = message;
    m_osdTimer.restart();
    update();
}

void DsShell::resizeEvent(QResizeEvent*)
{
    updateLayout();
}

void DsShell::updateLayout()
{
    const int availableWidth = qMax(1, width() - m_margin * 2);
    const int availableHeight = qMax(1, height() - m_margin * 2);
    ScreenLayoutType layout = screenLayout_Vertical;
    ScreenSizing sizing = screenSizing_Even;
    switch (m_mode) {
    case ScreenMode::Horizontal: layout = screenLayout_Horizontal; break;
    case ScreenMode::Hybrid: layout = screenLayout_Hybrid; sizing = screenSizing_EmphTop; break;
    case ScreenMode::TopOnly: sizing = screenSizing_TopOnly; break;
    case ScreenMode::BottomOnly: sizing = screenSizing_BotOnly; break;
    default: break;
    }
    m_screenLayout.Setup(availableWidth, availableHeight, layout, screenRot_0Deg, sizing,
                         14, m_integerScale, false, 1.0f, 1.0f);
}

QImage DsShell::gbaCanvas() const
{
    QImage canvas(256, 192, QImage::Format_ARGB32);
    canvas.fill(QColor(7, 11, 13));
    if (m_frame.top.isNull())
        return canvas;
    QPainter painter(&canvas);
    const QSize source = m_frame.top.size();
    const int scale = qMax(1, qMin(240 / source.width(), 160 / source.height()));
    const QSize target(source.width() * scale, source.height() * scale);
    const QRect rect(QPoint((256 - target.width()) / 2, (192 - target.height()) / 2), target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(rect, m_frame.top);
    return canvas;
}

void DsShell::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(19, 22, 25));
    painter.setRenderHint(QPainter::Antialiasing);

    // Console body and hinge.
    painter.setPen(QPen(QColor(59, 64, 69), 2));
    painter.setBrush(QColor(35, 39, 43));
    painter.drawRoundedRect(rect().adjusted(7, 7, -7, -7), 20, 20);

    float transforms[kMaxScreenTransforms * 6] {};
    int kinds[kMaxScreenTransforms] {};
    const int count = m_screenLayout.GetScreenTransforms(transforms, kinds);
    const QImage gbaTop = m_core == ActiveCore::Mgba ? gbaCanvas() : QImage();

    for (int i = 0; i < count; ++i) {
        const float* m = transforms + i * 6;
        QTransform transform(m[0], m[1], m[2], m[3], m[4] + m_margin, m[5] + m_margin);
        painter.save();
        painter.setTransform(transform);
        painter.setPen(QPen(QColor(8, 9, 10), 7.0 / qMax(0.1, transform.m11())));
        painter.setBrush(QColor(4, 7, 9));
        painter.drawRect(QRectF(0, 0, 256, 192));
        painter.setPen(Qt::NoPen);

        const QImage& image = kinds[i] == 0
            ? (m_core == ActiveCore::Mgba ? gbaTop : m_frame.top)
            : m_frame.bottom;
        if (!image.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QRectF(0, 0, 256, 192), image);
        } else if (m_core == ActiveCore::Mgba && kinds[i] == 1) {
            painter.fillRect(QRectF(0, 0, 256, 192), QColor(10, 17, 18));
            painter.setPen(QColor(113, 217, 176));
            QFont font = painter.font(); font.setBold(true); font.setPixelSize(12); painter.setFont(font);
            painter.drawText(QRectF(16, 34, 224, 35), Qt::AlignCenter, QStringLiteral("GAME BOY ADVANCE MODE"));
            font.setBold(false); font.setPixelSize(8); painter.setFont(font);
            painter.setPen(QColor(173, 184, 181));
            painter.drawText(QRectF(18, 78, 220, 60), Qt::AlignCenter | Qt::TextWordWrap,
                             QStringLiteral("%1\nSave data is automatic.\nInsert a DS card in Slot-1 to switch consoles.").arg(m_title));
        } else {
            painter.fillRect(QRectF(0, 0, 256, 192), QColor(7, 10, 12));
            painter.setPen(QColor(126, 133, 138));
            painter.drawText(QRectF(15, 0, 226, 192), Qt::AlignCenter | Qt::TextWordWrap,
                             m_core == ActiveCore::Idle ? QStringLiteral("DUALSLOT\n\nInsert a cartridge") : QStringLiteral("Starting console…"));
        }
        painter.restore();
    }

    painter.setPen(QColor(150, 156, 160));
    const QString slot1 = m_slots.slot1.isEmpty() ? QStringLiteral("SLOT-1  EMPTY")
        : QStringLiteral("SLOT-1  %1").arg(QFileInfo(m_slots.slot1).fileName());
    const QString slot2 = m_slots.slot2.isEmpty() ? QStringLiteral("SLOT-2  EMPTY")
        : QStringLiteral("SLOT-2  %1").arg(QFileInfo(m_slots.slot2).fileName());
    painter.drawText(QRect(25, 10, width() - 50, 20), Qt::AlignLeft | Qt::AlignVCenter, slot1);
    painter.drawText(QRect(25, height() - 30, width() - 50, 20), Qt::AlignRight | Qt::AlignVCenter, slot2);

    if (m_osdTimer.isValid() && m_osdTimer.elapsed() < 3500) {
        const qreal opacity = m_osdTimer.elapsed() > 2800 ? (3500 - m_osdTimer.elapsed()) / 700.0 : 1.0;
        painter.setOpacity(opacity);
        QRect box = QRect(0, 0, qMin(width() - 50, 520), 42);
        box.moveCenter(QPoint(width() / 2, height() - 62));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 205));
        painter.drawRoundedRect(box, 9, 9);
        painter.setPen(Qt::white);
        painter.drawText(box.adjusted(12, 0, -12, 0), Qt::AlignCenter, m_osd);
    }
}

void DsShell::updateKey(QKeyEvent* event, bool pressed)
{
    std::uint32_t bit = 0;
    switch (event->key()) {
    case Qt::Key_X: bit = Button::A; break;
    case Qt::Key_Z: bit = Button::B; break;
    case Qt::Key_Backspace: case Qt::Key_Shift: bit = Button::Select; break;
    case Qt::Key_Return: case Qt::Key_Enter: bit = Button::Start; break;
    case Qt::Key_Right: bit = Button::Right; break;
    case Qt::Key_Left: bit = Button::Left; break;
    case Qt::Key_Up: bit = Button::Up; break;
    case Qt::Key_Down: bit = Button::Down; break;
    case Qt::Key_S: bit = Button::R; break;
    case Qt::Key_A: bit = Button::L; break;
    case Qt::Key_Q: bit = Button::X; break;
    case Qt::Key_W: bit = Button::Y; break;
    default: event->ignore(); return;
    }
    if (!event->isAutoRepeat()) {
        if (pressed) m_keys |= bit; else m_keys &= ~bit;
        emit keysChanged(m_keys);
    }
    event->accept();
}

void DsShell::keyPressEvent(QKeyEvent* event) { updateKey(event, true); }
void DsShell::keyReleaseEvent(QKeyEvent* event) { updateKey(event, false); }

bool DsShell::mapTouch(const QPointF& point, TouchPoint& touch, bool clamp)
{
    int x = static_cast<int>(point.x()) - m_margin;
    int y = static_cast<int>(point.y()) - m_margin;
    if (!m_screenLayout.GetTouchCoords(x, y, clamp))
        return false;
    touch = {true, x, y};
    return true;
}

void DsShell::mousePressEvent(QMouseEvent* event)
{
    TouchPoint touch;
    if (event->button() == Qt::LeftButton && mapTouch(event->position(), touch, false)) {
        emit touchChanged(touch);
        event->accept();
    }
}

void DsShell::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        TouchPoint touch;
        if (mapTouch(event->position(), touch, true))
            emit touchChanged(touch);
    }
}

void DsShell::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit touchChanged(TouchPoint{});
}

void DsShell::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile())
        event->acceptProposedAction();
}

void DsShell::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->urls().isEmpty()) {
        emit romDropped(event->mimeData()->urls().first().toLocalFile());
        event->acceptProposedAction();
    }
}

} // namespace dualslot
