#pragma once

#include "fund_sheet.h"

#include <QString>
#include <QVector>

namespace bh::core {

// Отчёт по выбывшим экземплярам — новый XLSX-файл (QXlsx).
// Каждый экземпляр — отдельная строка: №, автор и название, инв. №, цена,
// дата и причина выбытия, примечание; внизу итог числа и суммы.
struct WithdrawalReport
{
    QString fundName;      // название фонда (имя исходного файла) для шапки
    QDate createdOn;       // дата отчёта
    QVector<int> records;  // индексы записей листа, в порядке строк отчёта

    // Сохраняет файл атомарно (QSaveFile). Записи без выбытия пропускаются.
    bool write(const QString &path, const FundSheet &sheet, QString *error = nullptr) const;

    static QStringList columnTitles();
};

} // namespace bh::core
