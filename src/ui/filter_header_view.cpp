#include "filter_header_view.h"

#include <QMouseEvent>
#include <QPainter>

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
    painter->save();
    if (filtered)
        painter->fillRect(rect, QColor(0xfd, 0xf0, 0xc8));
    painter->restore();

    // Текст заголовка без области кнопки.
    painter->save();
    QHeaderView::paintSection(painter, rect.adjusted(0, 0, -kButtonWidth, 0), logicalIndex);
    painter->restore();

    const QRect button = buttonRect(rect);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QColor(0x9a, 0x9a, 0x9a));
    painter->setBrush(filtered ? QColor(0xf2, 0xc9, 0x4c) : QColor(0xff, 0xff, 0xff));
    painter->drawRoundedRect(button.adjusted(0, button.height() / 2 - 12, 0, -(button.height() / 2 - 12)), 4, 4);
    painter->setPen(QColor(0x1f, 0x1f, 0x1f));
    QFont f = painter->font();
    f.setPointSizeF(f.pointSizeF() * 0.8);
    painter->setFont(f);
    // ▼ — фильтр, «Ф» на подсвеченном — фильтр включён.
    painter->drawText(button, Qt::AlignCenter, filtered ? QStringLiteral("▼✓") : QStringLiteral("▼"));
    painter->restore();

    // Разделитель между столбцами.
    painter->save();
    painter->setPen(QColor(0xc8, 0xc8, 0xc8));
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
    QHeaderView::mouseMoveEvent(event);
}

} // namespace bh::ui
