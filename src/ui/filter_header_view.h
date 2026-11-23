#pragma once

#include <QHeaderView>

namespace bh::ui {

// Заголовок таблицы как в Excel: справа у каждого столбца значок ▼,
// по нему открывается фильтр. Щелчок по остальной части — сортировка.
// Столбец с действующим фильтром выделен цветом и значком.
class FilterHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    explicit FilterHeaderView(QWidget *parent = nullptr);

    // Какие столбцы сейчас отфильтрованы (для подсветки).
    void setFilteredColumns(const QList<int> &columns);

signals:
    void filterRequested(int logicalIndex, const QPoint &globalPos);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    QSize sectionSizeFromContents(int logicalIndex) const override;

private:
    QRect buttonRect(const QRect &sectionRect) const;

    QList<int> m_filtered;
};

} // namespace bh::ui
