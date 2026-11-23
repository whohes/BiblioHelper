#include "fund_table_model.h"

#include "core/copy_ids.h"
#include "io/xlsx_workbook.h"

#include <QColor>
#include <QFont>
#include <QLocale>
#include <QRegularExpression>

namespace bh::models {

namespace {

// Старая запись цены: «2р20к», «202р», «45к», «44 р. 10 коп.» → рубли.
bool parseRubles(const QString &text, double *rubles)
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*(?:(\\d+)\\s*р(?:уб)?\\.?)?\\s*(?:(\\d{1,2})\\s*к(?:оп)?\\.?)?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty()))
        return false;
    *rubles = m.captured(1).toDouble() + (m.captured(2).isEmpty() ? 0.0 : m.captured(2).toDouble() / 100.0);
    return true;
}

// Число для сортировки: числа и текст вида «2018» сортируются как числа.
QVariant sortKeyFor(const QVariant &value, const QString &display, core::ColumnRole role)
{
    if (value.type() == QVariant::Double || value.type() == QVariant::LongLong
        || value.type() == QVariant::Int)
        return value.toDouble();
    bool ok = false;
    double d = display.toDouble(&ok);
    if (!ok)
        d = QLocale(QLocale::Russian).toDouble(display, &ok);  // «2,4»
    if (!ok && role == core::ColumnRole::Price)
        ok = parseRubles(display, &d);
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
    setModified(false);
    endResetModel();
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

bool FundTableModel::updateRecord(int row, const QVector<QVariant> &values)
{
    if (row < 0 || row >= m_sheet.records.size())
        return false;
    core::FundRecord &record = m_sheet.records[row];
    QSet<int> changed;
    for (int c = 0; c < m_sheet.columns.size() && c < values.size(); ++c) {
        if (m_sheet.columns.at(c).excelColumn <= 0)
            continue;  // вычисляемый столбец
        if (core::displayText(values.at(c)) != core::displayText(record.values.at(c))
            || values.at(c).type() != record.values.at(c).type())
            changed.insert(c);
    }
    if (changed.isEmpty())
        return false;

    UndoStep step{UndoStep::Edit, row, record.values, m_dirty.value(row),
                  QStringLiteral("Отменить изменение «%1»").arg(m_cells.at(row).value(
                      std::max(0, m_sheet.columnIndex(core::ColumnRole::AuthorTitle))).display.left(40))};
    m_undo.push_back(step);

    for (int c : changed)
        record.values[c] = values.at(c);
    m_dirty[row].unite(changed);
    rebuildRow(row);
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    setModified(true);
    return true;
}

int FundTableModel::addRecord(const QVector<QVariant> &values)
{
    const int row = m_sheet.records.size();
    core::FundRecord record;
    record.id = core::CopyIds::newId();
    record.excelRow = 0;
    record.values.resize(m_sheet.columns.size());
    for (int c = 0; c < m_sheet.columns.size() && c < values.size(); ++c) {
        if (m_sheet.columns.at(c).excelColumn > 0)
            record.values[c] = values.at(c);
    }
    beginInsertRows({}, row, row);
    m_sheet.records.push_back(record);
    m_cells.push_back({});
    rebuildRow(row);
    endInsertRows();
    const int author = m_sheet.columnIndex(core::ColumnRole::AuthorTitle);
    m_undo.push_back({UndoStep::Add, row, {}, {},
                      QStringLiteral("Отменить добавление «%1»")
                          .arg(author >= 0 ? m_cells.at(row).at(author).display.left(40) : QString())});
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
        beginRemoveRows({}, step.row, step.row);
        m_sheet.records.remove(step.row);
        m_cells.remove(step.row);
        m_dirty.remove(step.row);
        endRemoveRows();
    } else {
        m_sheet.records[step.row].values = step.oldValues;
        if (step.oldDirty.isEmpty())
            m_dirty.remove(step.row);
        else
            m_dirty[step.row] = step.oldDirty;
        rebuildRow(step.row);
        emit dataChanged(index(step.row, 0), index(step.row, columnCount() - 1));
    }
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

    // 1. Изменённые ячейки существующих строк.
    for (auto it = m_dirty.cbegin(); it != m_dirty.cend(); ++it) {
        const core::FundRecord &record = m_sheet.records.at(it.key());
        if (record.excelRow <= 0)
            continue;  // новая запись пишется целиком ниже
        for (int c : it.value()) {
            if (!workbook.setCellValue(sheet, record.excelRow, m_sheet.columns.at(c).excelColumn,
                                       record.values.at(c), error))
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
                if (column <= 0 || !record.values.at(c).isValid())
                    continue;
                if (!workbook.setCellValue(sheet, record.excelRow, column, record.values.at(c), error))
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
