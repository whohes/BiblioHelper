#pragma once

#include <QCollator>
#include <QHash>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>

namespace bh::models {

// Условие для столбца, как «Текстовые/Числовые фильтры» в Excel.
struct ColumnCondition
{
    enum class Op {
        Contains,
        NotContains,
        StartsWith,
        Equals,
        Greater,       // >  число
        GreaterEqual,  // >= число
        Less,          // <  число
        LessEqual,     // <= число
        Between,       // от a до b включительно
        Empty,
        NotEmpty,
    };
    Op op = Op::Contains;
    QString text;      // для текстовых условий
    double a = 0;      // для числовых
    double b = 0;

    static bool isNumeric(Op op);
    static bool needsValue(Op op);
    static QString label(Op op);
    QString describe() const;  // «больше 2000», «содержит "пушкин"»
    bool matches(const QString &normalizedText, const QVariant &sortKey) const;
};

// Поиск по всем столбцам и фильтры по значениям столбцов, как в Excel.
// Работает поверх FundTableModel (роли SortRole и SearchRole).
class FundFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit FundFilterProxy(QObject *parent = nullptr);

    // Текст для показа пустых ячеек в списке значений фильтра.
    static QString emptyValueLabel();

    // Поиск: все слова запроса должны встретиться в строке (в любом столбце).
    void setSearchText(const QString &text);
    QString searchText() const { return m_searchText; }

    // Показывать только строки, где значение столбца (как на экране) входит
    // в allowed. Пустое значение — emptyValueLabel().
    void setColumnFilter(int column, const QSet<QString> &allowed);
    void clearColumnFilter(int column);
    bool hasColumnFilter(int column) const { return m_columnFilters.contains(column); }
    QSet<QString> columnFilter(int column) const { return m_columnFilters.value(column); }

    // Условие вместо списка значений (одно на столбец; заменяет список).
    void setColumnCondition(int column, const ColumnCondition &condition);
    bool hasColumnCondition(int column) const { return m_conditions.contains(column); }
    ColumnCondition columnCondition(int column) const { return m_conditions.value(column); }
    bool isColumnFiltered(int column) const
    {
        return m_columnFilters.contains(column) || m_conditions.contains(column);
    }

    void clearAllFilters();
    bool hasAnyFilter() const
    {
        return !m_columnFilters.isEmpty() || !m_conditions.isEmpty() || !m_searchWords.isEmpty();
    }

    // Значения столбца среди строк, прошедших остальные фильтры (как в Excel),
    // с количеством строк для каждого.
    QVector<QPair<QString, int>> columnValues(int column) const;

signals:
    void filtersChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool acceptsRow(int sourceRow, int ignoredColumn) const;

    QString m_searchText;
    QStringList m_searchWords;
    QHash<int, QSet<QString>> m_columnFilters;
    QHash<int, ColumnCondition> m_conditions;
    QCollator m_collator;
};

} // namespace bh::models
