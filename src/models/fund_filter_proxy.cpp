#include "fund_filter_proxy.h"

#include "core/fund_sheet.h"
#include "fund_table_model.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QLocale>
#include <QTimer>

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
    refilter();
    emit filtersChanged();
}

void FundFilterProxy::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_searchWords = core::normalizeForSearch(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    refilter();
    emit filtersChanged();
}

void FundFilterProxy::setColumnCondition(int column, const ColumnCondition &condition)
{
    m_columnFilters.remove(column);
    m_conditions.insert(column, condition);
    refilter();
    emit filtersChanged();
}

void FundFilterProxy::setColumnFilter(int column, const QSet<QString> &allowed)
{
    m_conditions.remove(column);
    m_columnFilters.insert(column, allowed);
    refilter();
    emit filtersChanged();
}

void FundFilterProxy::clearColumnFilter(int column)
{
    if (m_columnFilters.remove(column) + m_conditions.remove(column) == 0)
        return;
    refilter();
    emit filtersChanged();
}

void FundFilterProxy::clearAllFilters()
{
    m_columnFilters.clear();
    m_conditions.clear();
    m_searchText.clear();
    m_searchWords.clear();
    refilter();
    emit filtersChanged();
}

bool FundFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (!acceptsRow(sourceRow, -1))
        return false;
    if (!m_group)
        return true;
    const QVector<int> &book = acceptedBookRows(sourceRow);
    if (book.isEmpty())
        return true;
    const QString id = sourceModel()->index(sourceRow, 0).data(FundTableModel::BookIdRole).toString();
    return isExpandedBook(id) || book.first() == sourceRow;
}

void FundFilterProxy::refilter()
{
    m_bookCache.clear();
    if (m_group)
        invalidate();  // первый экземпляр книги мог смениться — место книги в сортировке тоже
    else
        invalidateFilter();
}

void FundFilterProxy::setSourceModel(QAbstractItemModel *model)
{
    if (sourceModel())
        disconnect(sourceModel(), nullptr, this, nullptr);
    // Подключаемся раньше базового класса: кэш книг сбрасывается до того,
    // как прокси перефильтрует изменённые строки.
    if (model) {
        connect(model, &QAbstractItemModel::dataChanged, this, &FundFilterProxy::sourceChanged);
        connect(model, &QAbstractItemModel::rowsInserted, this, &FundFilterProxy::sourceChanged);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &FundFilterProxy::sourceChanged);
        connect(model, &QAbstractItemModel::modelReset, this, &FundFilterProxy::sourceChanged);
        connect(model, &QAbstractItemModel::layoutChanged, this, &FundFilterProxy::sourceChanged);
    }
    QSortFilterProxyModel::setSourceModel(model);
}

void FundFilterProxy::sourceChanged()
{
    m_bookCache.clear();
    if (!m_group || m_refreshPending)
        return;
    // Изменение одного экземпляра может поменять показ всей книги:
    // после всех изменений пересчитываем таблицу один раз.
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        if (m_group)
            refilter();
    });
}

const QVector<int> &FundFilterProxy::acceptedBookRows(int sourceRow) const
{
    static const QVector<int> none;
    const auto *model = qobject_cast<const FundTableModel *>(sourceModel());
    if (!model)
        return none;
    const QString id = model->bookIdOf(sourceRow);
    if (id.isEmpty())
        return none;
    auto it = m_bookCache.find(id);
    if (it == m_bookCache.end()) {
        QVector<int> accepted;
        for (int r : model->bookRows(id)) {
            if (acceptsRow(r, -1))
                accepted.push_back(r);
        }
        it = m_bookCache.insert(id, accepted);
    }
    return it.value().size() > 1 ? it.value() : none;
}

void FundFilterProxy::setGroupBooks(bool group)
{
    if (m_group == group)
        return;
    m_group = group;
    refilter();
}

void FundFilterProxy::setExpandAll(bool expand)
{
    if (m_expandAll == expand)
        return;
    m_expandAll = expand;
    if (!expand)
        m_expanded.clear();
    invalidateFilter();
}

bool FundFilterProxy::isBookRow(const QModelIndex &proxyIndex) const
{
    if (!m_group || !proxyIndex.isValid())
        return false;
    const int source = mapToSource(proxyIndex).row();
    const QVector<int> &book = acceptedBookRows(source);
    return !book.isEmpty() && book.first() == source;
}

bool FundFilterProxy::isBookExpanded(const QModelIndex &proxyIndex) const
{
    if (!isBookRow(proxyIndex))
        return false;
    return isExpandedBook(mapToSource(proxyIndex).data(FundTableModel::BookIdRole).toString());
}

void FundFilterProxy::toggleBook(const QModelIndex &proxyIndex)
{
    if (!isBookRow(proxyIndex))
        return;
    const QString id = mapToSource(proxyIndex).data(FundTableModel::BookIdRole).toString();
    if (isExpandedBook(id)) {
        if (m_expandAll)
            return;  // «Раскрыть все» включено
        m_expanded.remove(id);
    } else {
        m_expanded.insert(id);
    }
    invalidateFilter();
}

void FundFilterProxy::expandBooks(const QVector<int> &sourceRows)
{
    const auto *model = qobject_cast<const FundTableModel *>(sourceModel());
    if (!model)
        return;
    bool changed = false;
    for (int r : sourceRows) {
        const QString id = model->bookIdOf(r);
        if (!id.isEmpty() && !m_expanded.contains(id)) {
            m_expanded.insert(id);
            changed = true;
        }
    }
    if (changed && m_group)
        invalidateFilter();
}

bool FundFilterProxy::isCollapsedBook(int sourceRow) const
{
    if (!m_group)
        return false;
    const QVector<int> &book = acceptedBookRows(sourceRow);
    if (book.isEmpty() || book.first() != sourceRow)
        return false;
    return !isExpandedBook(sourceModel()->index(sourceRow, 0).data(FundTableModel::BookIdRole).toString());
}

QVector<int> FundFilterProxy::shownCopies(int sourceRow) const
{
    if (isCollapsedBook(sourceRow))
        return acceptedBookRows(sourceRow);
    return {sourceRow};
}

QVector<int> FundFilterProxy::shownSourceRows() const
{
    QVector<int> rows;
    rows.reserve(rowCount());
    for (int r = 0; r < rowCount(); ++r)
        rows += shownCopies(mapToSource(index(r, 0)).row());
    return rows;
}

void FundFilterProxy::sort(int column, Qt::SortOrder order)
{
    m_bookCache.clear();
    m_fileOrder = column < 0;
    // Без сортировки экземпляры книги разбросаны по файлу: в режиме книг
    // «порядок как в файле» — тоже сортировка, по первому экземпляру книги.
    if (m_group && column < 0)
        QSortFilterProxyModel::sort(0, Qt::AscendingOrder);
    else
        QSortFilterProxyModel::sort(column, order);
}

QIcon FundFilterProxy::bookArrowIcon(bool expanded)
{
    // Крупная стрелка на голубой плашке: её легко заметить и попасть по ней мышью.
    static QIcon icons[2];
    QIcon &icon = icons[expanded ? 1 : 0];
    if (!icon.isNull())
        return icon;
    for (int scale : {1, 2}) {
        const int side = kArrowIconSize * scale;
        QPixmap pixmap(side, side);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xdc, 0xe8, 0xf7));
        p.drawRoundedRect(QRectF(0, 0, side, side), 4 * scale, 4 * scale);
        p.setBrush(QColor(0x2f, 0x6d, 0xb5));
        const double m = side * 0.28;
        QPolygonF arrow;
        if (expanded)
            arrow << QPointF(m, side * 0.36) << QPointF(side - m, side * 0.36) << QPointF(side / 2.0, side * 0.70);
        else
            arrow << QPointF(side * 0.38, m) << QPointF(side * 0.72, side / 2.0) << QPointF(side * 0.38, side - m);
        p.drawPolygon(arrow);
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QVariant FundFilterProxy::data(const QModelIndex &index, int role) const
{
    if (!m_group || !index.isValid()
        || (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::BackgroundRole
            && role != Qt::DecorationRole))
        return QSortFilterProxyModel::data(index, role);
    const auto *model = qobject_cast<const FundTableModel *>(sourceModel());
    if (!model)
        return QSortFilterProxyModel::data(index, role);
    const int source = mapToSource(index).row();
    const QVector<int> &book = acceptedBookRows(source);
    if (book.isEmpty())
        return QSortFilterProxyModel::data(index, role);
    const bool head = book.first() == source;
    const bool expanded = isExpandedBook(model->bookIdOf(source));
    if (role == Qt::BackgroundRole)
        return head ? QVariant() : QVariant(QColor(0xf3, 0xf6, 0xfa));
    const int author = model->sheet().columnIndex(core::ColumnRole::AuthorTitle);
    if (index.column() != author)
        return QSortFilterProxyModel::data(index, role);
    if (role == Qt::DecorationRole)
        return head ? bookArrowIcon(expanded) : QVariant();
    const QString text = QSortFilterProxyModel::data(index, Qt::DisplayRole).toString();
    if (role == Qt::ToolTipRole) {
        return head ? QStringLiteral("%1\nКнига из %2 экз. Щёлкните по стрелке слева, чтобы %3.")
                          .arg(text)
                          .arg(book.size())
                          .arg(expanded ? QStringLiteral("свернуть") : QStringLiteral("показать все экземпляры"))
                    : text;
    }
    if (!head)
        return QStringLiteral("            %1").arg(text);
    if (expanded)
        return text;
    return QStringLiteral("%1   (%2 экз.)").arg(text).arg(book.size());
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
    const QVector<int> rows = shownSourceRows();
    result.rows = rows.size();
    if (priceColumn < 0 || !sourceModel())
        return result;
    const QAbstractItemModel *model = sourceModel();
    for (int r : rows) {
        // SortRole — число для числовых ячеек и цен вида «44р10к», иначе текст.
        const QVariant price = model->index(r, priceColumn).data(FundTableModel::SortRole);
        if (price.type() != QVariant::Double) {
            ++result.withoutPrice;
            continue;
        }
        double quantity = 1;
        if (quantityColumn >= 0) {
            const QVariant q = model->index(r, quantityColumn).data(FundTableModel::SortRole);
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
    if (m_group) {
        const QVector<int> &lb = acceptedBookRows(left.row());
        const QVector<int> &rb = acceptedBookRows(right.row());
        const int lrep = lb.isEmpty() ? left.row() : lb.first();
        const int rrep = rb.isEmpty() ? right.row() : rb.first();
        if (lrep != rrep) {
            // Книга стоит на месте своего первого экземпляра.
            if (m_fileOrder)
                return lrep < rrep;
            const QModelIndex l = left.sibling(lrep, left.column());
            const QModelIndex r = right.sibling(rrep, right.column());
            if (valueLess(l, r))
                return true;
            if (valueLess(r, l))
                return false;
            return lrep < rrep;
        }
        // Экземпляры одной книги — всегда по порядку файла, первый сверху.
        const bool before = left.row() < right.row();
        return sortOrder() == Qt::DescendingOrder ? !before : before;
    }
    return valueLess(left, right);
}

bool FundFilterProxy::valueLess(const QModelIndex &left, const QModelIndex &right) const
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
