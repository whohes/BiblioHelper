#include "withdrawal_archive.h"

#include "copy_ids.h"
#include "io/xlsx_workbook.h"

#include <QSet>

#include <algorithm>

namespace bh::core {

QString WithdrawalArchive::sheetName()
{
    return archiveSheetName();
}

int WithdrawalArchive::archive(io::XlsxWorkbook &workbook, FundSheet *sheet, const QVector<int> &records,
                               QString *error)
{
    QSet<int> chosen;
    for (int index : records) {
        if (index >= 0 && index < sheet->records.size() && sheet->isWithdrawn(index)
            && sheet->records.at(index).excelRow > 0)
            chosen.insert(index);
    }
    if (chosen.isEmpty())
        return 0;

    // Порядок в архиве — как в листе фонда.
    QVector<int> rows;
    for (int index : qAsConst(chosen))
        rows.push_back(sheet->records.at(index).excelRow);
    std::sort(rows.begin(), rows.end());
    if (!workbook.archiveRows(sheet->sheetName, rows, sheet->headerRow, sheetName(), error))
        return -1;

    // Записи: перенесённые убрать, остальным — новые номера строк.
    QVector<FundRecord> kept;
    kept.reserve(sheet->records.size() - chosen.size());
    for (int i = 0; i < sheet->records.size(); ++i) {
        if (chosen.contains(i))
            continue;
        FundRecord record = sheet->records.at(i);
        if (record.excelRow > 0)
            record.excelRow -= int(std::lower_bound(rows.begin(), rows.end(), record.excelRow) - rows.begin());
        kept.push_back(std::move(record));
    }
    sheet->records = std::move(kept);
    if (!CopyIds::save(workbook, *sheet, error))
        return -1;
    return chosen.size();
}

} // namespace bh::core
