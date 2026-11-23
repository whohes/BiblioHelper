#pragma once

#include "fund_sheet.h"

#include <QString>
#include <QVector>

namespace bh::io {
class XlsxWorkbook;
}

namespace bh::core {

// Внутренние ID экземпляров в скрытом листе «_BiblioHelper» рабочей копии.
//
// Инвентарный номер не уникален и бывает пустым, номер строки меняется,
// если файл правили в Excel. Поэтому у каждого экземпляра свой ID, а
// строка находится по номеру и «отпечатку» содержимого.
// Лист veryHidden: в Excel его не видно даже через «Показать».
class CopyIds
{
public:
    static QString sheetName();

    struct Entry
    {
        QString copyId;
        QString sheet;
        int row = 0;
        QString fingerprint;
    };

    static QVector<Entry> load(const io::XlsxWorkbook &workbook);
    static bool save(io::XlsxWorkbook &workbook, const FundSheet &sheet, QString *error = nullptr);

    // Сопоставляет ID записям: по строке с тем же отпечатком, затем по
    // отпечатку (строки сдвинули в Excel), затем по строке (ячейку поправили).
    // Остальным — новые ID. Возвращает число новых.
    static int assign(FundSheet *sheet, const QVector<Entry> &entries);

    static QString fingerprint(const FundSheet &sheet, const FundRecord &record);
    static QString newId();
};

} // namespace bh::core
