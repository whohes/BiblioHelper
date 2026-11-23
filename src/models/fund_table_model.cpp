#include "fund_table_model.h"

namespace bh::models {

namespace {

// Число для сортировки: числа и текст вида «2018» сортируются как числа.
QVariant sortKeyFor(const QVariant &value, const QString &display)
{
    if (value.type() == QVariant::Double || value.type() == QVariant::LongLong
        || value.type() == QVariant::Int)
        return value.toDouble();
    bool ok = false;
    const double d = display.toDouble(&ok);
    if (ok)
        return d;
    return core::normalizeForSearch(display);
}

} // namespace

FundTableModel::FundTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void FundTableModel::setSheet(core::FundSheet sheet)
{
    beginResetModel();
    m_sheet = std::move(sheet);
    m_cells.clear();
    m_cells.reserve(m_sheet.records.size());
    for (const core::FundRecord &record : qAsConst(m_sheet.records)) {
        QVector<Cell> row;
        row.reserve(record.values.size());
        for (const QVariant &value : record.values) {
            Cell cell;
            cell.display = core::displayText(value);
            cell.search = core::normalizeForSearch(cell.display);
            cell.sortKey = sortKeyFor(value, cell.display);
            row.push_back(std::move(cell));
        }
        m_cells.push_back(std::move(row));
    }
    endResetModel();
}

int FundTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_cells.size();
}

int FundTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sheet.columns.size();
}

QVariant FundTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_cells.size())
        return {};
    const auto &row = m_cells.at(index.row());
    if (index.column() >= row.size())
        return {};
    const Cell &cell = row.at(index.column());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return cell.display;
    case SortRole:
        return cell.sortKey;
    case SearchRole:
        return cell.search;
    case ExcelRowRole:
        return m_sheet.records.at(index.row()).excelRow;
    case Qt::TextAlignmentRole: {
        const auto role = m_sheet.columns.at(index.column()).role;
        if (role == core::ColumnRole::AuthorTitle)
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        return int(Qt::AlignCenter);
    }
    default:
        return {};
    }
}

QVariant FundTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (section < 0 || section >= m_sheet.columns.size())
            return {};
        if (role == Qt::DisplayRole)
            return m_sheet.columns.at(section).title;
        if (role == Qt::ToolTipRole)
            return QStringLiteral("%1\nЩелчок — сортировка, значок ▼ — фильтр")
                .arg(m_sheet.columns.at(section).title);
        return {};
    }
    if (section < 0 || section >= m_sheet.records.size())
        return {};
    if (role == Qt::DisplayRole)
        return m_sheet.records.at(section).excelRow;
    if (role == Qt::ToolTipRole)
        return QStringLiteral("Строка %1 в Excel").arg(m_sheet.records.at(section).excelRow);
    return {};
}

} // namespace bh::models
