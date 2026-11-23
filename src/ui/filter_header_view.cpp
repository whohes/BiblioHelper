#include "filter_header_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>

namespace bh::ui {

namespace {
constexpr int kButtonWidth = 26;
}

FilterHeaderView::FilterHeaderView(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setSectionsClickable(true);
    setSortIndicatorShown(true);
    setHighlightSections(false);
    setMouseTracking(true);
    setDefaultAlignment(Qt::AlignCenter);
}

void FilterHeaderView::setFilteredColumns(const QList<int> &columns)
{
    m_filtered = columns;
    viewport()->update();
}

QRect FilterHeaderView::buttonRect(const QRect &sectionRect) const
{
    return QRect(sectionRect.right() - kButtonWidth + 1, sectionRect.top() + 2, kButtonWidth - 3,
                 sectionRect.height() - 4);
}

QSize FilterHeaderView::sectionSizeFromContents(int logicalIndex) const
{
    QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
    size.rwidth() += kButtonWidth;
    size.setHeight(std::max(size.height(), fontMetrics().height() * 2 + 12));
    return size;
}

void FilterHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    const bool filtered = m_filtered.contains(logicalIndex);
    // Фон всей секции, включая место под кнопку фильтра.
    painter->fillRect(rect, filtered ? QColor(0xfd, 0xf0, 0xc8) : QColor(0xee, 0xf2, 0xf7));

    // Текст заголовка без области кнопки.
    painter->save();
    QHeaderView::paintSection(painter, rect.adjusted(0, 0, -kButtonWidth, 0), logicalIndex);
    painter->restore();

    // Кнопка фильтра: серая «галочка вниз», при включённом фильтре — синяя плашка с воронкой.
    const QRect button = buttonRect(rect);
    const int side = std::min(button.width(), 22);
    const QRectF box(button.center().x() - side / 2.0 + 0.5, button.center().y() - side / 2.0 + 0.5, side, side);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (filtered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x2f, 0x6d, 0xb5));
        painter->drawRoundedRect(box, 5, 5);
        const QPointF c = box.center();
        const qreal w = side * 0.32;
        QPolygonF funnel;
        funnel << QPointF(c.x() - w, c.y() - w * 0.8) << QPointF(c.x() + w, c.y() - w * 0.8)
               << QPointF(c.x() + w * 0.2, c.y() + w * 0.1) << QPointF(c.x() + w * 0.2, c.y() + w * 0.9)
               << QPointF(c.x() - w * 0.2, c.y() + w * 0.6) << QPointF(c.x() - w * 0.2, c.y() + w * 0.1);
        painter->setBrush(Qt::white);
        painter->drawPolygon(funnel);
    } else {
        if (m_hoverSection == logicalIndex) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0xdc, 0xe4, 0xee));
            painter->drawRoundedRect(box, 5, 5);
        }
        QPen pen(QColor(0x6b, 0x77, 0x86), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QPointF c = box.center();
        const qreal w = side * 0.2;
        painter->drawPolyline(QPolygonF() << QPointF(c.x() - w, c.y() - w * 0.5) << QPointF(c.x(), c.y() + w * 0.5)
                                          << QPointF(c.x() + w, c.y() - w * 0.5));
    }
    painter->restore();

    // Разделитель между столбцами.
    painter->save();
    painter->setPen(QColor(0xd5, 0xdc, 0xe5));
    painter->drawLine(rect.topRight(), rect.bottomRight());
    painter->restore();
}

void FilterHeaderView::mousePressEvent(QMouseEvent *event)
{
    const int logical = logicalIndexAt(event->pos());
    if (logical >= 0 && event->button() == Qt::LeftButton) {
        const int x = sectionViewportPosition(logical);
        const QRect section(x, 0, sectionSize(logical), height());
        if (buttonRect(section).contains(event->pos())) {
            emit filterRequested(logical, mapToGlobal(QPoint(section.left(), section.bottom())));
            return;
        }
    }
    QHeaderView::mousePressEvent(event);
}

void FilterHeaderView::mouseMoveEvent(QMouseEvent *event)
{
    const int logical = logicalIndexAt(event->pos());
    bool overButton = false;
    if (logical >= 0) {
        const QRect section(sectionViewportPosition(logical), 0, sectionSize(logical), height());
        overButton = buttonRect(section).contains(event->pos());
    }
    setCursor(overButton ? Qt::PointingHandCursor : Qt::ArrowCursor);
    const int hover = overButton ? logical : -1;
    if (hover != m_hoverSection) {
        m_hoverSection = hover;
        viewport()->update();
    }
    QHeaderView::mouseMoveEvent(event);
}

void FilterHeaderView::leaveEvent(QEvent *event)
{
    if (m_hoverSection >= 0) {
        m_hoverSection = -1;
        viewport()->update();
    }
    QHeaderView::leaveEvent(event);
}

} // namespace bh::ui
