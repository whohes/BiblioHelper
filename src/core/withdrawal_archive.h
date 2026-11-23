#pragma once

#include "fund_sheet.h"

#include <QVector>

namespace bh::io {
class XlsxWorkbook;
}

namespace bh::core {

// Очистка выбывших после отчёта: строки переносятся на лист «Архив выбывших»
// той же рабочей копии (с оформлением и статусом), а с листа фонда
// удаляются. Остальные записи получают новые номера строк, лист ID
// перезаписывается. Изменения — в открытой книге, сохраняет вызывающий код.
class WithdrawalArchive
{
public:
    static QString sheetName();

    // records — индексы записей листа; берутся только выбывшие и уже
    // записанные в Excel. Возвращает число перенесённых или -1 при ошибке.
    static int archive(io::XlsxWorkbook &workbook, FundSheet *sheet, const QVector<int> &records,
                       QString *error = nullptr);
};

} // namespace bh::core
