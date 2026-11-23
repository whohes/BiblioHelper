#include "fund_filter_proxy.h"

#include "core/fund_sheet.h"
#include "fund_table_model.h"

#include <QLocale>

#include <algorithm>

namespace bh::models {

FundFilterProxy::FundFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_collator(QLocale(QLocale::Russian, QLocale::Russia))
{
    // «33516-1» после «33516», «б/н 2» перед «б/н 10».
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setSortRole(FundTableModel::SortRole);
}

QString FundFilterProxy::emptyValueLabel()
{
    return QStringLiteral("(пусто)");
}

void FundFilterProxy::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_searchWords = core::normalizeForSearch(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    invalidateFilter();
    emit filtersChanged();
}

void FundFilterProxy::setColumnFilter(int column, const QSet<QString> &allowed)
{
    m_columnFilters.insert(column, allowed);
    invalidateFilter();
    emit filtersChanged();
}

void FundFilterProxy::clearColumnFilter(int column)
{
    if (m_columnFilters.remove(column) == 0)
        return;
    invalidateFilter();
    emit filtersChanged();
}

void FundFilterProxy::clearAllFilters()
{
    m_columnFilters.clear();
    m_searchText.clear();
    m_searchWords.clear();
    invalidateFilter();
    emit filtersChanged();
}

bool FundFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    return acceptsRow(sourceRow, -1);
}

bool FundFilterProxy::acceptsRow(int sourceRow, int ignoredColumn) const
{
    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return true;

    for (auto it = m_columnFilters.cbegin(); it != m_columnFilters.cend(); ++it) {
        if (it.key() == ignoredColumn)
            continue;
        QString value = model->index(sourceRow, it.key()).data(Qt::DisplayRole).toString();
        if (value.isEmpty())
            value = emptyValueLabel();
        if (!it.value().contains(value))
            return false;
    }

    if (m_searchWords.isEmpty())
        return true;
    QString rowText;
    const int columns = model->columnCount();
    for (int c = 0; c < columns; ++c) {
        rowText += model->index(sourceRow, c).data(FundTableModel::SearchRole).toString();
        rowText += QLatin1Char('\n');
    }
    for (const QString &word : m_searchWords) {
        if (!rowText.contains(word))
            return false;
    }
    return true;
}

QVector<QPair<QString, int>> FundFilterProxy::columnValues(int column) const
{
    QVector<QPair<QString, int>> result;
    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return result;

    QHash<QString, int> counts;
    QHash<QString, QVariant> sortKeys;
    for (int r = 0; r < model->rowCount(); ++r) {
        if (!acceptsRow(r, column))
            continue;
        const QModelIndex index = model->index(r, column);
        QString value = index.data(Qt::DisplayRole).toString();
        if (value.isEmpty())
            value = emptyValueLabel();
        if (!counts.contains(value))
            sortKeys.insert(value, index.data(FundTableModel::SortRole));
        ++counts[value];
    }
    // Значения, которые уже отмечены в фильтре этого столбца, остаются в списке.
    for (const QString &value : m_columnFilters.value(column)) {
        if (!counts.contains(value)) {
            counts.insert(value, 0);
            sortKeys.insert(value, value);
        }
    }

    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        result.push_back({it.key(), it.value()});
    const QString empty = emptyValueLabel();
    std::sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
        if (a.first == empty || b.first == empty)
            return b.first == empty && a.first != empty;  // «(пусто)» в конце
        const QVariant ka = sortKeys.value(a.first);
        const QVariant kb = sortKeys.value(b.first);
        const bool na = ka.type() == QVariant::Double;
        const bool nb = kb.type() == QVariant::Double;
        if (na && nb)
            return ka.toDouble() < kb.toDouble();
        if (na != nb)
            return na;  // числа перед текстом
        return m_collator.compare(a.first, b.first) < 0;
    });
    return result;
}

bool FundFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QVariant l = left.data(FundTableModel::SortRole);
    const QVariant r = right.data(FundTableModel::SortRole);
    const bool ln = l.type() == QVariant::Double;
    const bool rn = r.type() == QVariant::Double;
    const QString ls = ln ? QString() : l.toString();
    const QString rs = rn ? QString() : r.toString();
    // Пустые ячейки всегда в конце при сортировке по возрастанию.
    if (!ln && ls.isEmpty())
        return false;
    if (!rn && rs.isEmpty())
        return true;
    if (ln && rn)
        return l.toDouble() < r.toDouble();
    if (ln != rn)
        return ln;
    return m_collator.compare(ls, rs) < 0;
}

} // namespace bh::models
