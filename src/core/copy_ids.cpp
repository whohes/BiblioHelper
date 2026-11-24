#include "copy_ids.h"

#include "import_layout.h"
#include "io/xlsx_workbook.h"

#include <QCryptographicHash>
#include <QHash>
#include <QUuid>

namespace bh::core {

QString CopyIds::sheetName()
{
    return QStringLiteral("_BiblioHelper");
}

QString CopyIds::newId()
{
    return QStringLiteral("c-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
}

QString CopyIds::newBookId()
{
    return QStringLiteral("b-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
}

QString CopyIds::fingerprint(const FundSheet &sheet, const FundRecord &record)
{
    QByteArray data;
    for (int c = 0; c < sheet.columns.size() && c < record.values.size(); ++c) {
        if (sheet.columns.at(c).excelColumn <= 0 || sheet.columns.at(c).role == ColumnRole::Status)
            continue;  // вычисляемые столбцы и «Статус» (его пишет программа) не в счёт
        data += normalizeForSearch(displayText(record.values.at(c))).toUtf8();
        data += '\x1f';
    }
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(12));
}

QVector<CopyIds::Entry> CopyIds::load(const io::XlsxWorkbook &workbook)
{
    QVector<Entry> entries;
    if (!workbook.sheetNames().contains(sheetName()))
        return entries;
    const io::SheetSize size = workbook.sheetSize(sheetName());
    const auto rows = workbook.readRows(sheetName(), 2, std::max(size.rows, 2), 8);
    entries.reserve(rows.size());
    for (const auto &row : rows) {
        Entry e;
        e.copyId = row.values.value(0).toString();
        e.sheet = row.values.value(1).toString();
        e.row = row.values.value(2).toString().toInt();
        e.fingerprint = row.values.value(3).toString();
        e.withdrawnOn = row.values.value(4).toString();
        e.reason = row.values.value(5).toString();
        e.note = row.values.value(6).toString();
        e.bookId = row.values.value(7).toString();
        if (!e.copyId.isEmpty())
            entries.push_back(e);
    }
    return entries;
}

bool CopyIds::save(io::XlsxWorkbook &workbook, const FundSheet &sheet, QString *error)
{
    // Экземпляры других листов-фондов этого файла остаются как были.
    const QVector<Entry> others = [&] {
        QVector<Entry> kept;
        for (const Entry &e : load(workbook)) {
            if (!e.sheet.isEmpty() && e.sheet != sheet.sheetName && workbook.sheetNames().contains(e.sheet))
                kept.push_back(e);
        }
        return kept;
    }();
    if (!workbook.ensureVeryHiddenSheet(sheetName(), error))
        return false;
    QVector<QStringList> rows;
    rows.reserve(sheet.records.size() + 1);
    rows.push_back({QStringLiteral("copy_id"), QStringLiteral("sheet"), QStringLiteral("row"),
                    QStringLiteral("fingerprint"), QStringLiteral("withdrawn_on"), QStringLiteral("reason"),
                    QStringLiteral("note"), QStringLiteral("book_id")});
    const int dateCol = sheet.columnIndex(ColumnRole::WithdrawDate);
    const int reasonCol = sheet.columnIndex(ColumnRole::WithdrawReason);
    const int noteCol = sheet.columnIndex(ColumnRole::WithdrawNote);
    auto text = [](const FundRecord &record, int column) {
        return column >= 0 && column < record.values.size() ? displayText(record.values.at(column)) : QString();
    };
    for (const FundRecord &record : sheet.records) {
        if (record.id.isEmpty() || record.excelRow <= 0)
            continue;
        QStringList row{record.id, sheet.sheetName, QString::number(record.excelRow), fingerprint(sheet, record)};
        if (sheet.isWithdrawn(record)) {
            row << record.values.at(dateCol).toDate().toString(Qt::ISODate) << text(record, reasonCol)
                << text(record, noteCol);
        }
        if (!record.bookId.isEmpty()) {
            while (row.size() < 7)
                row << QString();
            row << record.bookId;
        }
        rows.push_back(row);
    }
    for (const Entry &e : others) {
        QStringList row{e.copyId, e.sheet, QString::number(e.row), e.fingerprint, e.withdrawnOn, e.reason, e.note};
        if (!e.bookId.isEmpty())
            row << e.bookId;
        rows.push_back(row);
    }
    if (!workbook.replaceSheetText(sheetName(), rows, error))
        return false;
    // Вместе с ID — настройки чтения листа, чтобы при следующем открытии
    // не спрашивать мастер импорта снова.
    return ImportLayout::fromSheet(sheet).save(workbook, error);
}

int CopyIds::assign(FundSheet *sheet, const QVector<Entry> &entries)
{
    QHash<int, int> byRow;                 // строка → индекс записи ID
    QMultiHash<QString, int> byPrint;      // отпечаток → индексы записей ID
    for (int i = 0; i < entries.size(); ++i) {
        if (!entries.at(i).sheet.isEmpty() && entries.at(i).sheet != sheet->sheetName)
            continue;
        byRow.insert(entries.at(i).row, i);
        byPrint.insert(entries.at(i).fingerprint, i);
    }
    QVector<bool> used(entries.size(), false);
    QVector<QString> prints(sheet->records.size());
    for (int r = 0; r < sheet->records.size(); ++r) {
        FundRecord &record = sheet->records[r];
        record.id.clear();
        prints[r] = fingerprint(*sheet, record);
    }

    // 1. Та же строка, то же содержимое.
    for (int r = 0; r < sheet->records.size(); ++r) {
        FundRecord &record = sheet->records[r];
        const auto it = byRow.constFind(record.excelRow);
        if (it != byRow.constEnd() && !used[*it] && entries.at(*it).fingerprint == prints[r]) {
            record.id = entries.at(*it).copyId;
            used[*it] = true;
        }
    }
    // 2. То же содержимое на другой строке (строки вставили/удалили в Excel).
    for (int r = 0; r < sheet->records.size(); ++r) {
        FundRecord &record = sheet->records[r];
        if (!record.id.isEmpty())
            continue;
        for (auto it = byPrint.find(prints[r]); it != byPrint.end() && it.key() == prints[r]; ++it) {
            if (!used[*it]) {
                record.id = entries.at(*it).copyId;
                used[*it] = true;
                break;
            }
        }
    }
    // 3. Та же строка, содержимое поправили в Excel.
    for (int r = 0; r < sheet->records.size(); ++r) {
        FundRecord &record = sheet->records[r];
        if (!record.id.isEmpty())
            continue;
        const auto it = byRow.constFind(record.excelRow);
        if (it != byRow.constEnd() && !used[*it]) {
            record.id = entries.at(*it).copyId;
            used[*it] = true;
        }
    }
    // 4. Остальным — новые ID.
    int created = 0;
    for (FundRecord &record : sheet->records) {
        if (record.id.isEmpty()) {
            record.id = newId();
            ++created;
        }
    }

    // Сведения о выбытии и книга — по найденному ID.
    const int dateCol = sheet->columnIndex(ColumnRole::WithdrawDate);
    const int reasonCol = sheet->columnIndex(ColumnRole::WithdrawReason);
    const int noteCol = sheet->columnIndex(ColumnRole::WithdrawNote);
    QHash<QString, int> byId;
    for (int i = 0; i < entries.size(); ++i) {
        if (used[i])
            byId.insert(entries.at(i).copyId, i);
    }
    QHash<QString, int> bookSizes;
    for (FundRecord &record : sheet->records) {
        record.bookId.clear();
        const auto it = byId.constFind(record.id);
        if (it == byId.constEnd())
            continue;
        const Entry &e = entries.at(*it);
        record.bookId = e.bookId;
        if (!record.bookId.isEmpty())
            ++bookSizes[record.bookId];
        const QDate date = QDate::fromString(e.withdrawnOn, Qt::ISODate);
        if (dateCol < 0 || !date.isValid())
            continue;
        record.values[dateCol] = date;
        if (reasonCol >= 0)
            record.values[reasonCol] = e.reason.isEmpty() ? QVariant() : QVariant(e.reason);
        if (noteCol >= 0)
            record.values[noteCol] = e.note.isEmpty() ? QVariant() : QVariant(e.note);
    }
    // Книга, от которой остался один экземпляр (остальные удалили в Excel), — уже не группа.
    for (FundRecord &record : sheet->records) {
        if (!record.bookId.isEmpty() && bookSizes.value(record.bookId) < 2)
            record.bookId.clear();
    }
    return created;
}

} // namespace bh::core
