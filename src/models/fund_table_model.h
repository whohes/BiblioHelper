#pragma once

#include "core/fund_sheet.h"

#include <QAbstractTableModel>

namespace bh::models {

// Таблица экземпляров листа фонда. Только чтение на этом этапе.
// Номер строки Excel показывается в заголовке строки слева.
class FundTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Значение для сортировки: число (double) или нормализованный текст.
    static constexpr int SortRole = Qt::UserRole + 1;
    // Нормализованный текст для поиска и фильтров.
    static constexpr int SearchRole = Qt::UserRole + 2;
    // Номер строки Excel.
    static constexpr int ExcelRowRole = Qt::UserRole + 3;

    explicit FundTableModel(QObject *parent = nullptr);

    void setSheet(core::FundSheet sheet);
    const core::FundSheet &sheet() const { return m_sheet; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Cell
    {
        QString display;
        QString search;
        QVariant sortKey;
    };

    core::FundSheet m_sheet;
    QVector<QVector<Cell>> m_cells;  // заранее посчитанный текст: быстрые поиск и сортировка
};

} // namespace bh::models
