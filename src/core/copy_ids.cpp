#include "copy_ids.h"

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

QString CopyIds::fingerprint(const FundSheet &sheet, const FundRecord &record)
{
    QByteArray data;
    for (int c = 0; c < sheet.columns.size() && c < record.values.size(); ++c) {
        if (sheet.columns.at(c).excelColumn <= 0)
            continue;  // вычисляемые столбцы (цветовая пометка) не в счёт
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
    const auto rows = workbook.readRows(sheetName(), 2, std::max(size.rows, 2), 4);
    entries.reserve(rows.size());
    for (const auto &row : rows) {
        Entry e;
        e.copyId = row.values.value(0).toString();
        e.sheet = row.values.value(1).toString();
        e.row = row.values.value(2).toString().toInt();
        e.fingerprint = row.values.value(3).toString();
        if (!e.copyId.isEmpty())
            entries.push_back(e);
    }
    return entries;
}

bool CopyIds::save(io::XlsxWorkbook &workbook, const FundSheet &sheet, QString *error)
{
    if (!workbook.ensureVeryHiddenSheet(sheetName(), error))
        return false;
    QVector<QStringList> rows;
    rows.reserve(sheet.records.size() + 1);
    rows.push_back({QStringLiteral("copy_id"), QStringLiteral("sheet"), QStringLiteral("row"),
                    QStringLiteral("fingerprint")});
    for (const FundRecord &record : sheet.records) {
        if (record.id.isEmpty() || record.excelRow <= 0)
            continue;
        rows.push_back({record.id, sheet.sheetName, QString::number(record.excelRow), fingerprint(sheet, record)});
    }
    return workbook.replaceSheetText(sheetName(), rows, error);
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
    return created;
}

} // namespace bh::core
