#include "fund_filter_proxy.h"

#include "core/fund_sheet.h"
#include "fund_table_model.h"

#include <QLocale>

#include <algorithm>

namespace bh::models {

bool ColumnCondition::isNumeric(Op op)
{
    return op == Op::Greater || op == Op::GreaterEqual || op == Op::Less || op == Op::LessEqual
        || op == Op::Between;
}

bool ColumnCondition::needsValue(Op op)
{
    return op != Op::Empty && op != Op::NotEmpty;
}

QString ColumnCondition::label(Op op)
{
    switch (op) {
    case Op::Contains: return QStringLiteral("содержит");
    case Op::NotContains: return QStringLiteral("не содержит");
    case Op::StartsWith: return QStringLiteral("начинается с");
    case Op::Equals: return QStringLiteral("равно");
    case Op::Greater: return QStringLiteral("больше");
    case Op::GreaterEqual: return QStringLiteral("больше или равно");
    case Op::Less: return QStringLiteral("меньше");
    case Op::LessEqual: return QStringLiteral("меньше или равно");
    case Op::Between: return QStringLiteral("между");
    case Op::Empty: return QStringLiteral("пусто");
    case Op::NotEmpty: return QStringLiteral("не пусто");
    }
    return {};
}

QString ColumnCondition::describe() const
{
    QLocale ru(QLocale::Russian);
    ru.setNumberOptions(QLocale::OmitGroupSeparator);  // «2020», а не «2 020»
    if (op == Op::Between)
        return QStringLiteral("между %1 и %2").arg(ru.toString(a), ru.toString(b));
    if (isNumeric(op))
        return QStringLiteral("%1 %2").arg(label(op), ru.toString(a));
    if (needsValue(op))
        return QStringLiteral("%1 «%2»").arg(label(op), text);
    return label(op);
}

bool ColumnCondition::matches(const QString &normalizedText, const QVariant &sortKey) const
{
    const QString needle = core::normalizeForSearch(text);
    switch (op) {
    case Op::Contains: return normalizedText.contains(needle);
    case Op::NotContains: return !normalizedText.contains(needle);
    case Op::StartsWith: return normalizedText.startsWith(needle);
    case Op::Equals: return normalizedText == needle;
    case Op::Empty: return normalizedText.isEmpty();
    case Op::NotEmpty: return !normalizedText.isEmpty();
    default: break;
    }
    // Числовые условия: текст, который не число («б/н»), не подходит.
    if (sortKey.type() != QVariant::Double)
        return false;
    const double v = sortKey.toDouble();
    switch (op) {
    case Op::Greater: return v > a;
    case Op::GreaterEqual: return v >= a;
    case Op::Less: return v < a;
    case Op::LessEqual: return v <= a;
    case Op::Between: return v >= std::min(a, b) && v <= std::max(a, b);
    default: return false;
    }
}

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

void FundFilterProxy::setStatusFilter(StatusFilter filter)
{
    if (filter == m_status)
        return;
    m_status = filter;
    invalidateFilter();
    emit filtersChanged();
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

void FundFilterProxy::setColumnCondition(int column, const ColumnCondition &condition)
{
    m_columnFilters.remove(column);
    m_conditions.insert(column, condition);
    invalidateFilter();
    emit filtersChanged();
}

void FundFilterProxy::setColumnFilter(int column, const QSet<QString> &allowed)
{
    m_conditions.remove(column);
    m_columnFilters.insert(column, allowed);
    invalidateFilter();
    emit filtersChanged();
}

void FundFilterProxy::clearColumnFilter(int column)
{
    if (m_columnFilters.remove(column) + m_conditions.remove(column) == 0)
        return;
    invalidateFilter();
    emit filtersChanged();
}

void FundFilterProxy::clearAllFilters()
{
    m_columnFilters.clear();
    m_conditions.clear();
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

    if (m_status != StatusFilter::All) {
        const bool withdrawn = model->index(sourceRow, 0).data(FundTableModel::WithdrawnRole).toBool();
        if (withdrawn != (m_status == StatusFilter::Withdrawn))
            return false;
    }

    for (auto it = m_columnFilters.cbegin(); it != m_columnFilters.cend(); ++it) {
        if (it.key() == ignoredColumn)
            continue;
        QString value = model->index(sourceRow, it.key()).data(Qt::DisplayRole).toString();
        if (value.isEmpty())
            value = emptyValueLabel();
        if (!it.value().contains(value))
            return false;
    }

    for (auto it = m_conditions.cbegin(); it != m_conditions.cend(); ++it) {
        if (it.key() == ignoredColumn)
            continue;
        const QModelIndex index = model->index(sourceRow, it.key());
        if (!it.value().matches(index.data(FundTableModel::SearchRole).toString(),
                                index.data(FundTableModel::SortRole)))
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

FundFilterProxy::Totals FundFilterProxy::totals(int priceColumn, int quantityColumn) const
{
    Totals result;
    result.rows = rowCount();
    if (priceColumn < 0)
        return result;
    for (int r = 0; r < result.rows; ++r) {
        // SortRole — число для числовых ячеек и цен вида «44р10к», иначе текст.
        const QVariant price = index(r, priceColumn).data(FundTableModel::SortRole);
        if (price.type() != QVariant::Double) {
            ++result.withoutPrice;
            continue;
        }
        double quantity = 1;
        if (quantityColumn >= 0) {
            const QVariant q = index(r, quantityColumn).data(FundTableModel::SortRole);
            if (q.type() == QVariant::Double && q.toDouble() > 0)
                quantity = q.toDouble();
        }
        result.sum += price.toDouble() * quantity;
    }
    return result;
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
