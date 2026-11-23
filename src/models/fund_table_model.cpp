#include "fund_table_model.h"

#include "core/copy_ids.h"
#include "io/xlsx_workbook.h"

#include <QColor>
#include <QSet>

#include <algorithm>
#include <QFont>
#include <QLocale>
#include <QRegularExpression>

namespace bh::models {

namespace {

// Число для сортировки: числа и текст вида «2018» сортируются как числа.
QVariant sortKeyFor(const QVariant &value, const QString &display, core::ColumnRole role)
{
    if (value.type() == QVariant::Double || value.type() == QVariant::LongLong
        || value.type() == QVariant::Int)
        return value.toDouble();
    if (value.type() == QVariant::Date)
        return double(value.toDate().toJulianDay());
    bool ok = false;
    double d = display.toDouble(&ok);
    if (!ok)
        d = QLocale(QLocale::Russian).toDouble(display, &ok);  // «2,4»
    if (!ok && role == core::ColumnRole::Price)
        ok = core::parseRubles(display, &d);
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
    for (int r = 0; r < m_sheet.records.size(); ++r) {
        m_cells.push_back({});
        rebuildRow(r);
    }
    m_dirty.clear();
    m_undo.clear();
    rebuildBooks();
    setModified(false);
    endResetModel();
    emit undoChanged();
}

FundTableModel::Cell FundTableModel::makeCell(int column, const QVariant &value) const
{
    Cell cell;
    cell.display = core::displayText(value);
    cell.search = core::normalizeForSearch(cell.display);
    cell.sortKey = sortKeyFor(value, cell.display, m_sheet.columns.at(column).role);
    return cell;
}

void FundTableModel::rebuildRow(int row)
{
    const core::FundRecord &record = m_sheet.records.at(row);
    QVector<Cell> cells;
    cells.reserve(record.values.size());
    for (int c = 0; c < record.values.size(); ++c)
        cells.push_back(makeCell(c, record.values.at(c)));
    m_cells[row] = std::move(cells);
}

void FundTableModel::rebuildBooks()
{
    m_books.clear();
    for (int r = 0; r < m_sheet.records.size(); ++r) {
        const QString &book = m_sheet.records.at(r).bookId;
        if (!book.isEmpty())
            m_books[book].push_back(r);
    }
}

QString FundTableModel::bookIdOf(int row) const
{
    return row >= 0 && row < m_sheet.records.size() ? m_sheet.records.at(row).bookId : QString();
}

QVector<int> FundTableModel::bookRows(const QString &bookId) const
{
    return bookId.isEmpty() ? QVector<int>() : m_books.value(bookId);
}

QVector<int> FundTableModel::withBookmates(const QVector<int> &rows) const
{
    QVector<int> result;
    QSet<int> seen;
    for (int row : rows) {
        const QVector<int> mates = bookRows(bookIdOf(row));
        for (int r : mates.isEmpty() ? QVector<int>{row} : mates) {
            if (!seen.contains(r)) {
                seen.insert(r);
                result.push_back(r);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

int FundTableModel::setBookIds(const QHash<int, QString> &bookIds, const QString &undoText)
{
    UndoStep step{UndoStep::Books, {}, undoText};
    for (auto it = bookIds.cbegin(); it != bookIds.cend(); ++it) {
        const int row = it.key();
        if (row < 0 || row >= m_sheet.records.size() || m_sheet.records.at(row).bookId == it.value())
            continue;
        step.rows.push_back({row, {}, m_dirty.value(row), m_sheet.records.at(row).bookId});
        m_sheet.records[row].bookId = it.value();
        m_dirty[row];  // книга хранится в скрытом листе: записать при сохранении
    }
    if (step.rows.isEmpty())
        return 0;
    rebuildBooks();
    for (const RowState &state : qAsConst(step.rows))
        emit dataChanged(index(state.row, 0), index(state.row, columnCount() - 1));
    const int count = step.rows.size();
    pushUndo(std::move(step));
    setModified(true);
    return count;
}

int FundTableModel::mergeBooks(const QVector<QVector<int>> &groups)
{
    QHash<int, QString> ids;
    // Книга, у которой после объединения не останется экземпляров, исчезает сама;
    // книга, от которой останется один экземпляр, — тоже (он становится отдельной книгой).
    QHash<QString, int> remaining;
    for (auto it = m_books.cbegin(); it != m_books.cend(); ++it)
        remaining.insert(it.key(), it.value().size());
    int books = 0;
    for (const QVector<int> &group : groups) {
        QVector<int> rows;
        for (int r : group) {
            if (r >= 0 && r < m_sheet.records.size() && !rows.contains(r))
                rows.push_back(r);
        }
        if (rows.size() < 2)
            continue;
        // Если часть экземпляров уже в книге — к ней и присоединяем.
        QString target;
        for (int r : qAsConst(rows)) {
            if (!m_sheet.records.at(r).bookId.isEmpty()) {
                target = m_sheet.records.at(r).bookId;
                break;
            }
        }
        if (target.isEmpty())
            target = core::CopyIds::newBookId();
        for (int r : qAsConst(rows)) {
            const QString &old = m_sheet.records.at(r).bookId;
            if (old == target)
                continue;
            if (!old.isEmpty())
                --remaining[old];
            ids.insert(r, target);
        }
        ++books;
    }
    for (auto it = remaining.cbegin(); it != remaining.cend(); ++it) {
        if (it.value() == 1) {
            for (int r : m_books.value(it.key())) {
                if (!ids.contains(r))
                    ids.insert(r, QString());
            }
        }
    }
    const QString text = books == 1 ? QStringLiteral("Отменить объединение в книгу")
                                    : QStringLiteral("Отменить объединение %1 книг").arg(books);
    return setBookIds(ids, text) > 0 ? books : 0;
}

int FundTableModel::splitBooks(const QVector<int> &rows)
{
    QHash<int, QString> ids;
    QHash<QString, int> remaining;
    for (int r : rows) {
        const QString book = bookIdOf(r);
        if (book.isEmpty() || ids.contains(r))
            continue;
        ids.insert(r, QString());
        if (!remaining.contains(book))
            remaining.insert(book, m_books.value(book).size());
        --remaining[book];
    }
    const int split = ids.size();
    // Последний оставшийся экземпляр книги — тоже отдельная книга.
    for (auto it = remaining.cbegin(); it != remaining.cend(); ++it) {
        if (it.value() == 1) {
            for (int r : m_books.value(it.key()))
                ids.insert(r, QString());
        }
    }
    const QString text = split == 1 ? QStringLiteral("Отменить разделение «%1»").arg(titleOf(ids.cbegin().key()))
                                    : QStringLiteral("Отменить разделение %1 экз.").arg(split);
    return split > 0 && setBookIds(ids, text) > 0 ? split : 0;
}

void FundTableModel::setModified(bool modified)
{
    if (m_modified == modified)
        return;
    m_modified = modified;
    emit modifiedChanged(modified);
}

int FundTableModel::rowForId(const QString &id) const
{
    for (int r = 0; r < m_sheet.records.size(); ++r) {
        if (m_sheet.records.at(r).id == id)
            return r;
    }
    return -1;
}

QString FundTableModel::titleOf(int row) const
{
    const int author = m_sheet.columnIndex(core::ColumnRole::AuthorTitle);
    return author >= 0 ? m_cells.at(row).at(author).display.left(40) : QString();
}

void FundTableModel::pushUndo(UndoStep step)
{
    m_undo.push_back(std::move(step));
    emit undoChanged();
}

bool FundTableModel::updateRecord(int row, const QVector<QVariant> &values)
{
    if (row < 0 || row >= m_sheet.records.size())
        return false;
    core::FundRecord &record = m_sheet.records[row];
    QSet<int> changed;
    for (int c = 0; c < m_sheet.columns.size() && c < values.size(); ++c) {
        const auto &column = m_sheet.columns.at(c);
        if (column.excelColumn <= 0 || column.role == core::ColumnRole::Status)
            continue;  // вычисляемый столбец; «Статус» меняет только выбытие
        if (core::displayText(values.at(c)) != core::displayText(record.values.at(c))
            || values.at(c).type() != record.values.at(c).type())
            changed.insert(c);
    }
    if (changed.isEmpty())
        return false;

    pushUndo({UndoStep::Edit, {{row, record.values, m_dirty.value(row)}},
              QStringLiteral("Отменить изменение «%1»").arg(titleOf(row))});

    for (int c : changed)
        record.values[c] = values.at(c);
    m_dirty[row].unite(changed);
    rebuildRow(row);
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    setModified(true);
    return true;
}

int FundTableModel::applyToRows(const QVector<int> &rows, const QHash<int, QVariant> &values,
                                const QString &undoText)
{
    UndoStep step{UndoStep::Edit, {}, undoText};
    for (int row : rows) {
        if (row < 0 || row >= m_sheet.records.size())
            continue;
        core::FundRecord &record = m_sheet.records[row];
        step.rows.push_back({row, record.values, m_dirty.value(row)});
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            record.values[it.key()] = it.value();
            if (m_sheet.columns.at(it.key()).excelColumn > 0)
                m_dirty[row].insert(it.key());
        }
        // Выбытие хранится в скрытом листе: запись нужно сохранить, даже
        // если в видимых ячейках ничего не поменялось.
        m_dirty[row];
        rebuildRow(row);
        emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    }
    if (step.rows.isEmpty())
        return 0;
    const int count = step.rows.size();
    pushUndo(std::move(step));
    setModified(true);
    return count;
}

int FundTableModel::withdraw(const QVector<int> &rows, const QDate &date, const QString &reason,
                             const QString &note)
{
    const int dateCol = m_sheet.columnIndex(core::ColumnRole::WithdrawDate);
    if (dateCol < 0 || !date.isValid())
        return 0;
    QVector<int> active;
    for (int row : rows) {
        if (row >= 0 && row < m_sheet.records.size() && !m_sheet.isWithdrawn(row))
            active.push_back(row);
    }
    if (active.isEmpty())
        return 0;
    QHash<int, QVariant> values;
    values.insert(dateCol, date);
    auto set = [&](core::ColumnRole role, const QVariant &value) {
        const int c = m_sheet.columnIndex(role);
        if (c >= 0)
            values.insert(c, value);
    };
    const QString cleanReason = reason.trimmed();
    const QString cleanNote = note.trimmed();
    set(core::ColumnRole::WithdrawReason, cleanReason.isEmpty() ? QVariant() : QVariant(cleanReason));
    set(core::ColumnRole::WithdrawNote, cleanNote.isEmpty() ? QVariant() : QVariant(cleanNote));
    set(core::ColumnRole::Status, core::withdrawalStatusText(date, cleanReason));
    const QString text = active.size() == 1
        ? QStringLiteral("Отменить выбытие «%1»").arg(titleOf(active.first()))
        : QStringLiteral("Отменить выбытие %1 экз.").arg(active.size());
    return applyToRows(active, values, text);
}

int FundTableModel::restore(const QVector<int> &rows)
{
    QVector<int> withdrawn;
    for (int row : rows) {
        if (row >= 0 && row < m_sheet.records.size() && m_sheet.isWithdrawn(row))
            withdrawn.push_back(row);
    }
    if (withdrawn.isEmpty())
        return 0;
    QHash<int, QVariant> values;
    for (auto role : {core::ColumnRole::WithdrawDate, core::ColumnRole::WithdrawReason,
                      core::ColumnRole::WithdrawNote, core::ColumnRole::Status}) {
        const int c = m_sheet.columnIndex(role);
        if (c >= 0)
            values.insert(c, QVariant());
    }
    const QString text = withdrawn.size() == 1
        ? QStringLiteral("Отменить возврат в фонд «%1»").arg(titleOf(withdrawn.first()))
        : QStringLiteral("Отменить возврат в фонд %1 экз.").arg(withdrawn.size());
    return applyToRows(withdrawn, values, text);
}

int FundTableModel::addRecord(const QVector<QVariant> &values)
{
    const int row = m_sheet.records.size();
    core::FundRecord record;
    record.id = core::CopyIds::newId();
    record.excelRow = 0;
    record.values.resize(m_sheet.columns.size());
    for (int c = 0; c < m_sheet.columns.size() && c < values.size(); ++c) {
        if (m_sheet.columns.at(c).excelColumn > 0 && m_sheet.columns.at(c).role != core::ColumnRole::Status)
            record.values[c] = values.at(c);
    }
    beginInsertRows({}, row, row);
    m_sheet.records.push_back(record);
    m_cells.push_back({});
    rebuildRow(row);
    endInsertRows();
    pushUndo({UndoStep::Add, {{row, {}, {}}}, QStringLiteral("Отменить добавление «%1»").arg(titleOf(row))});
    setModified(true);
    return row;
}

QString FundTableModel::undoText() const
{
    return m_undo.isEmpty() ? QString() : m_undo.last().text;
}

void FundTableModel::undo()
{
    if (m_undo.isEmpty())
        return;
    const UndoStep step = m_undo.takeLast();
    if (step.kind == UndoStep::Add) {
        const int row = step.rows.first().row;
        beginRemoveRows({}, row, row);
        m_sheet.records.remove(row);
        m_cells.remove(row);
        m_dirty.remove(row);
        endRemoveRows();
        rebuildBooks();
    } else if (step.kind == UndoStep::Books) {
        for (const RowState &state : step.rows) {
            m_sheet.records[state.row].bookId = state.oldBookId;
            if (state.oldDirty.isEmpty())
                m_dirty.remove(state.row);
            else
                m_dirty[state.row] = state.oldDirty;
        }
        rebuildBooks();
        for (const RowState &state : step.rows)
            emit dataChanged(index(state.row, 0), index(state.row, columnCount() - 1));
    } else {
        for (const RowState &state : step.rows) {
            m_sheet.records[state.row].values = state.oldValues;
            if (state.oldDirty.isEmpty())
                m_dirty.remove(state.row);
            else
                m_dirty[state.row] = state.oldDirty;
            rebuildRow(state.row);
            emit dataChanged(index(state.row, 0), index(state.row, columnCount() - 1));
        }
    }
    emit undoChanged();
    setModified(!m_dirty.isEmpty() || newRecordCount() > 0);
}

int FundTableModel::newRecordCount() const
{
    int n = 0;
    for (const auto &record : m_sheet.records)
        n += record.excelRow <= 0 ? 1 : 0;
    return n;
}

bool FundTableModel::writeChanges(io::XlsxWorkbook &workbook, QString *error)
{
    const QString &sheet = m_sheet.sheetName;

    // 0. Столбец «Статус» появляется в Excel при первом выбытии.
    // Оформление берём у последнего подписанного столбца таблицы («Отдел»).
    const int statusCol = m_sheet.columnIndex(core::ColumnRole::Status);
    bool newStatusColumn = false;
    int styleSource = 0;
    if (statusCol >= 0 && !m_sheet.columns.at(statusCol).inFile) {
        bool anyStatus = false;
        for (const auto &record : qAsConst(m_sheet.records))
            anyStatus = anyStatus || !core::displayText(record.values.at(statusCol)).isEmpty();
        if (anyStatus) {
            for (const core::FundColumn &column : qAsConst(m_sheet.columns)) {
                if (column.excelColumn > 0 && column.inFile && column.role != core::ColumnRole::Mark)
                    styleSource = std::max(styleSource, column.excelColumn);
            }
            core::FundColumn &column = m_sheet.columns[statusCol];
            const int excelCol = column.excelColumn;
            if (styleSource > 0 && !workbook.copyCellStyle(sheet, m_sheet.headerRow, styleSource, excelCol, error))
                return false;
            if (!workbook.setCellValue(sheet, m_sheet.headerRow, excelCol, column.title, error)
                || !workbook.setColumnWidth(sheet, excelCol, 28, error)
                || !workbook.extendAutoFilter(sheet, m_sheet.headerRow, excelCol, error))
                return false;
            column.inFile = true;
            newStatusColumn = true;
        }
    }
    // Ячейка нового столбца получает оформление столбца-образца.
    auto writeCell = [&](int excelRow, int c, const QVariant &value) {
        const int excelCol = m_sheet.columns.at(c).excelColumn;
        if (newStatusColumn && c == statusCol && styleSource > 0
            && !workbook.copyCellStyle(sheet, excelRow, styleSource, excelCol, error))
            return false;
        return workbook.setCellValue(sheet, excelRow, excelCol, value, error);
    };

    // 1. Изменённые ячейки существующих строк.
    for (auto it = m_dirty.cbegin(); it != m_dirty.cend(); ++it) {
        const core::FundRecord &record = m_sheet.records.at(it.key());
        if (record.excelRow <= 0)
            continue;  // новая запись пишется целиком ниже
        for (int c : it.value()) {
            if (m_sheet.columns.at(c).excelColumn <= 0 || !m_sheet.columns.at(c).inFile)
                continue;
            if (!writeCell(record.excelRow, c, record.values.at(c)))
                return false;
        }
    }

    // 2. Новые записи — строками после последней строки данных.
    QVector<int> fresh;
    int lastRow = 0;
    for (int r = 0; r < m_sheet.records.size(); ++r) {
        if (m_sheet.records.at(r).excelRow <= 0)
            fresh.push_back(r);
        else
            lastRow = std::max(lastRow, m_sheet.records.at(r).excelRow);
    }
    if (!fresh.isEmpty()) {
        const int afterRow = lastRow > 0 ? lastRow : m_sheet.headerRow;
        if (!workbook.appendRowsAfter(sheet, afterRow, fresh.size(), afterRow, error))
            return false;
        for (int i = 0; i < fresh.size(); ++i) {
            core::FundRecord &record = m_sheet.records[fresh.at(i)];
            record.excelRow = afterRow + 1 + i;
            for (int c = 0; c < m_sheet.columns.size(); ++c) {
                const int column = m_sheet.columns.at(c).excelColumn;
                if (column <= 0 || !m_sheet.columns.at(c).inFile || !record.values.at(c).isValid())
                    continue;
                if (!writeCell(record.excelRow, c, record.values.at(c)))
                    return false;
            }
        }
    }

    // 3. Лист ID.
    return core::CopyIds::save(workbook, m_sheet, error);
}

void FundTableModel::markSaved()
{
    m_dirty.clear();
    m_undo.clear();  // отмена — только до сохранения
    emit undoChanged();
    if (!m_sheet.records.isEmpty())
        emit headerDataChanged(Qt::Vertical, 0, m_sheet.records.size() - 1);
    setModified(false);
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
    case IdRole:
        return m_sheet.records.at(index.row()).id;
    case WithdrawnRole:
        return m_sheet.isWithdrawn(index.row());
    case BookIdRole:
        return m_sheet.records.at(index.row()).bookId;
    case Qt::FontRole:
        if (m_dirty.value(index.row()).contains(index.column()) || m_sheet.records.at(index.row()).excelRow <= 0) {
            QFont font;
            font.setBold(true);  // изменено, но ещё не сохранено
            return font;
        }
        return {};
    case Qt::ForegroundRole: {
        // Цвет шрифта как в Excel: ветхие и устаревшие книги видны сразу.
        const auto &colors = m_sheet.records.at(index.row()).colors;
        if (index.column() < colors.size() && colors.at(index.column()) != 0)
            return QColor::fromRgb(colors.at(index.column()));
        return {};
    }
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
    const int excelRow = m_sheet.records.at(section).excelRow;
    if (role == Qt::DisplayRole)
        return excelRow > 0 ? QVariant(excelRow) : QVariant(QStringLiteral("нов."));
    if (role == Qt::ToolTipRole)
        return excelRow > 0 ? QStringLiteral("Строка %1 в Excel").arg(excelRow)
                            : QStringLiteral("Новая книга: попадёт в Excel при сохранении");
    return {};
}

} // namespace bh::models
