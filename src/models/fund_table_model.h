#pragma once

#include "core/fund_sheet.h"

#include <QAbstractTableModel>
#include <QDate>
#include <QHash>
#include <QSet>

namespace bh::io {
class XlsxWorkbook;
}

namespace bh::models {

// Таблица экземпляров листа фонда: правка, добавление, выбытие, отмена.
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
    // Внутренний ID экземпляра.
    static constexpr int IdRole = Qt::UserRole + 4;
    // Экземпляр выбыл (bool).
    static constexpr int WithdrawnRole = Qt::UserRole + 5;
    // ID книги, если экземпляр объединён с другими (иначе пусто).
    static constexpr int BookIdRole = Qt::UserRole + 6;

    explicit FundTableModel(QObject *parent = nullptr);

    void setSheet(core::FundSheet sheet);
    const core::FundSheet &sheet() const { return m_sheet; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // ---- Изменения (до «Сохранить» живут только в памяти) ----
    int rowForId(const QString &id) const;
    // Заменить значения записи (по порядку столбцов). Возвращает false, если ничего не изменилось.
    bool updateRecord(int row, const QVector<QVariant> &values);
    // Новая запись в конце списка; возвращает её строку в модели.
    int addRecord(const QVector<QVariant> &values);

    // Выбытие экземпляров (строки модели): дата, причина, примечание.
    // Записывает и видимый «Статус». Одно действие для отмены.
    int withdraw(const QVector<int> &rows, const QDate &date, const QString &reason, const QString &note);
    // Вернуть выбывшие экземпляры в фонд.
    int restore(const QVector<int> &rows);
    bool isWithdrawn(int row) const { return m_sheet.isWithdrawn(row); }

    // ---- Книги из нескольких экземпляров ----
    QString bookIdOf(int row) const;
    // Экземпляры книги (строки модели по порядку); пусто, если такой книги нет.
    QVector<int> bookRows(const QString &bookId) const;
    // Строка и все экземпляры её книги.
    QVector<int> withBookmates(const QVector<int> &rows) const;
    // Объединить каждую группу строк в одну книгу (одно действие для отмены).
    // Возвращает число получившихся книг.
    int mergeBooks(const QVector<QVector<int>> &groups);
    // Отделить экземпляры от их книг: каждый становится отдельной книгой.
    // Возвращает число отделённых экземпляров.
    int splitBooks(const QVector<int> &rows);

    bool canUndo() const { return !m_undo.isEmpty(); }
    QString undoText() const;
    void undo();

    bool isModified() const { return m_modified; }
    int newRecordCount() const;

    // Записать изменения в открытую рабочую копию (ещё без сохранения файла):
    // изменённые ячейки, новые строки после последней строки данных, лист ID.
    bool writeChanges(io::XlsxWorkbook &workbook, QString *error);
    // После успешного сохранения файла: изменений больше нет.
    void markSaved();

signals:
    void modifiedChanged(bool modified);
    // Изменился список отмены (текст кнопки «Отменить»).
    void undoChanged();

private:
    struct Cell
    {
        QString display;
        QString search;
        QVariant sortKey;
    };

    struct RowState
    {
        int row = 0;
        QVector<QVariant> oldValues;
        QSet<int> oldDirty;
        QString oldBookId;
    };

    struct UndoStep
    {
        enum Kind { Edit, Add, Books } kind;
        QVector<RowState> rows;  // Edit: прежнее состояние строк; Add: одна строка
        QString text;
    };

    Cell makeCell(int column, const QVariant &value) const;
    // Записать значения в строки одним шагом отмены; columns — какие столбцы меняются.
    int applyToRows(const QVector<int> &rows, const QHash<int, QVariant> &values, const QString &undoText);
    QString titleOf(int row) const;
    void pushUndo(UndoStep step);
    void rebuildRow(int row);
    void rebuildBooks();
    // Записать ID книг строкам одним шагом отмены.
    int setBookIds(const QHash<int, QString> &bookIds, const QString &undoText);
    void setModified(bool modified);

    core::FundSheet m_sheet;
    QVector<QVector<Cell>> m_cells;
    QHash<int, QSet<int>> m_dirty;  // строка модели → изменённые столбцы
    QHash<QString, QVector<int>> m_books;  // ID книги → строки модели
    QVector<UndoStep> m_undo;
    bool m_modified = false;  // заранее посчитанный текст: быстрые поиск и сортировка
};

} // namespace bh::models
