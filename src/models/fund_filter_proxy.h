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

    // Какие экземпляры показывать: действующие (вкладка «Книги»),
    // выбывшие (вкладка «Выбывшие») или все.
    enum class StatusFilter { All, Active, Withdrawn };
    void setStatusFilter(StatusFilter filter);
    StatusFilter statusFilter() const { return m_status; }

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

    // Сумма по видимым строкам: цена × количество (пустое количество — 1 шт.).
    struct Totals
    {
        int rows = 0;
        double sum = 0;
        int withoutPrice = 0;  // цена пустая или не число
    };
    Totals totals(int priceColumn, int quantityColumn) const;

    // ---- Книги из нескольких экземпляров (вкладка «Книги») ----
    // Включено: экземпляры одной книги идут подряд, свёрнутая книга показана
    // одной строкой «▸ Название (3 экз.)», раскрытая — всеми экземплярами.
    void setGroupBooks(bool group);
    bool groupBooks() const { return m_group; }
    void setExpandAll(bool expand);
    bool expandAll() const { return m_expandAll; }
    // Строка таблицы — заголовок книги из нескольких показанных экземпляров.
    bool isBookRow(const QModelIndex &proxyIndex) const;
    bool isBookExpanded(const QModelIndex &proxyIndex) const;
    void toggleBook(const QModelIndex &proxyIndex);
    // Раскрыть книги этих экземпляров (строки исходной модели).
    void expandBooks(const QVector<int> &sourceRows);
    // Строка исходной модели — свёрнутая книга, за которой скрыты другие экземпляры.
    bool isCollapsedBook(int sourceRow) const;
    // Показанные экземпляры этой строки: вся свёрнутая книга или сама строка.
    QVector<int> shownCopies(int sourceRow) const;
    // Все экземпляры, прошедшие поиск и фильтры, в порядке на экране
    // (включая скрытые в свёрнутых книгах).
    QVector<int> shownSourceRows() const;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    void setSourceModel(QAbstractItemModel *model) override;

signals:
    void filtersChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool acceptsRow(int sourceRow, int ignoredColumn) const;
    void refilter();
    void sourceChanged();
    // Экземпляры книги строки, прошедшие фильтры (по порядку файла); пусто — строка не в книге.
    const QVector<int> &acceptedBookRows(int sourceRow) const;
    bool isExpandedBook(const QString &bookId) const { return m_expandAll || m_expanded.contains(bookId); }
    bool valueLess(const QModelIndex &left, const QModelIndex &right) const;

    bool m_group = false;
    bool m_expandAll = false;
    bool m_fileOrder = true;
    bool m_refreshPending = false;
    QSet<QString> m_expanded;
    mutable QHash<QString, QVector<int>> m_bookCache;

    QString m_searchText;
    QStringList m_searchWords;
    QHash<int, QSet<QString>> m_columnFilters;
    QHash<int, ColumnCondition> m_conditions;
    QCollator m_collator;
    StatusFilter m_status = StatusFilter::All;
};

} // namespace bh::models
