#pragma once

#include "fund_sheet.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace bh::io {
class XlsxWorkbook;
}

namespace bh::core {

// Как читать фонд: лист, строка заголовков и назначение столбцов.
// Задаётся в мастере импорта и хранится в рабочей копии на скрытом листе
// «_BiblioHelperImport», чтобы при следующем открытии читать файл так же.
struct ImportLayout
{
    QString sheet;
    int headerRow = 0;
    QHash<int, ColumnRole> roles;  // номер столбца Excel (с 1) → назначение

    bool isValid() const { return !sheet.isEmpty() && headerRow > 0; }

    // Настройки, по которым прочитан лист (столбцы, которые есть в файле).
    static ImportLayout fromSheet(const FundSheet &sheet);

    static QString sheetName();
    // Пустые настройки, если в файле их нет.
    static ImportLayout load(const io::XlsxWorkbook &workbook);
    bool save(io::XlsxWorkbook &workbook, QString *error = nullptr) const;
};

// Назначения, которые можно выбрать для столбца в мастере, по порядку списка.
QVector<ColumnRole> importableRoles();
// Название назначения для мастера: «Инв. №», «Пометка о наличии»…
QString roleTitle(ColumnRole role);
// Короткий английский ключ для хранения в файле и обратно.
QString roleKey(ColumnRole role);
ColumnRole roleFromKey(const QString &key, bool *ok = nullptr);

// Пометка из столбцов проверки наличия («сп», «нет уч.», «*»).
struct MarkCount
{
    QString key;    // для сравнения: «нет уч»
    QString text;   // как чаще всего записана в файле: «нет уч.»
    int count = 0;  // в скольких строках
};

// «Нет уч.» → «нет уч»: нижний регистр, ё → е, без точек и лишних пробелов.
QString normalizeMark(const QString &text);
// Пометки, которые по умолчанию означают «книги нет на месте»: сп, нет уч, не числ.
bool isDefaultWithdrawnMark(const QString &key);
// Пометка, с которой импортируется выбытие («по пометке в исходном файле»).
QString markWithdrawalReason();

// Все пометки в столбцах с назначением «Пометка о наличии», самые частые первыми.
QVector<MarkCount> countMarks(const FundSheet &sheet);
// Записи, у которых есть одна из пометок keys: индекс записи → пометка как в файле.
QHash<int, QString> recordsWithMarks(const FundSheet &sheet, const QSet<QString> &keys);

} // namespace bh::core
