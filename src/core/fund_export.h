#pragma once

#include "fund_sheet.h"

#include <QDate>
#include <QString>
#include <QVector>

namespace bh::core {

// Экспорт выбранных записей и столбцов фонда в новый файл.
// XLSX: заголовок, шапка таблицы с автофильтром и закреплением, итог числа
// экземпляров и суммы (если выгружается цена), печать по ширине листа.
// CSV: UTF-8 с BOM и разделителем «;» — так его без вопросов открывает Excel.
struct FundExport
{
    enum class Format { Xlsx, Csv };

    QString title;          // «Книжный фонд» / «Выбывшие экземпляры»
    QString fundName;       // имя исходного файла для подзаголовка
    QDate createdOn;
    QVector<int> columns;   // индексы столбцов листа, по порядку
    QVector<int> records;   // индексы записей, по порядку строк

    bool write(const QString &path, Format format, const FundSheet &sheet, QString *error = nullptr) const;

    // Столбцы для выгрузки по умолчанию: всё, что видно в таблице.
    // withdrawn — выгружаются выбывшие: добавляются дата, причина и примечание.
    static QVector<int> defaultColumns(const FundSheet &sheet, bool withdrawn);

private:
    bool writeXlsx(const QString &path, const FundSheet &sheet, QString *error) const;
    bool writeCsv(const QString &path, const FundSheet &sheet, QString *error) const;
};

} // namespace bh::core
