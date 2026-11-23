#pragma once

#include <QDate>
#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QRgb>
#include <QVariant>
#include <QVector>

namespace bh::io {
class XlsxWorkbook;
using FontColors = QHash<QPair<int, int>, QRgb>;
}

namespace bh::core {

// Что означает столбец фонда. Определяется по заголовку; столбцы без
// заголовка (пометки «*», «сп», «инв» в H/I) считаются пометками.
enum class ColumnRole {
    Number,       // № п/п
    AuthorTitle,  // автор книги и название (не делится, решение этапа 1)
    Inventory,    // инвентарный номер (может быть текстом: «б/н», «33516-1»)
    Quantity,
    Price,        // число или текст «2р20к»
    Year,
    Department,   // отдел / шифр
    Mark,         // пометки наличия
    ColorMark,    // цвет шрифта строки (ветхие / устаревшие), добавляется программой
    Status,       // видимый столбец «Статус» в Excel: «Выбыло 23.11.2026, ветхость»
    WithdrawDate,   // дата выбытия (QDate); хранится в скрытом листе, не в Excel
    WithdrawReason, // причина выбытия
    WithdrawNote,   // примечание к выбытию
    Other,
};

struct FundColumn
{
    int excelColumn = 0;  // номер столбца Excel (с 1)
    QString title;        // заголовок для показа
    ColumnRole role = ColumnRole::Other;
    // false — столбца ещё нет в файле (например, «Статус» до первого выбытия):
    // заголовок запишется при сохранении.
    bool inFile = true;
};

// Одна запись фонда = одна строка Excel = один экземпляр.
struct FundRecord
{
    QString id;           // внутренний ID экземпляра (c-xxxxxxxx), не зависит от строки
    int excelRow = 0;     // 0 — новая запись, ещё не записана в Excel
    QVector<QVariant> values;  // по порядку FundSheet::columns
    QVector<QRgb> colors;      // цвет шрифта ячеек (0 — обычный); пусто, если цветов нет
};

struct FundSheet
{
    QString sheetName;
    int headerRow = 0;
    QVector<FundColumn> columns;
    QVector<FundRecord> records;

    // Индекс столбца с ролью или -1.
    int columnIndex(ColumnRole role) const;

    // Экземпляр выбыл: у него есть дата выбытия.
    bool isWithdrawn(const FundRecord &record) const;
    bool isWithdrawn(int recordIndex) const { return isWithdrawn(records.at(recordIndex)); }
};

// Лист «Архив выбывших» в рабочей копии: туда переносятся строки при очистке.
// При открытии файла этот лист не считается фондом.
QString archiveSheetName();

// Причины выбытия (решение этапа 1); последняя — «другое».
QStringList withdrawalReasons();
// Текст для видимого столбца «Статус»: «Выбыло 23.11.2026, ветхость».
QString withdrawalStatusText(const QDate &date, const QString &reason);

class FundSheetReader
{
public:
    // Читает лист: находит строку заголовков, столбцы и записи книг.
    // Строки итогов и пояснений внизу листа («По документам числится…»,
    // =SUM) записями не считаются.
    static bool read(const io::XlsxWorkbook &workbook, const QString &sheet, FundSheet *out,
                     QString *error = nullptr);

    // Читает все листы и возвращает лист с наибольшим числом записей.
    static bool readBestSheet(const io::XlsxWorkbook &workbook, FundSheet *out,
                              QString *error = nullptr);

    static ColumnRole roleForHeader(const QString &header);

    // Добавляет столбцы выбытия: «Статус» (в Excel, после последнего
    // столбца таблицы, если его ещё нет) и вычисляемые «Дата выбытия»,
    // «Причина выбытия», «Примечание». read() вызывает это сам.
    static void addWithdrawalColumns(FundSheet *sheet);

    // Раскрашивает ячейки по цветам шрифта из Excel и, если цветные строки
    // есть, добавляет столбец «Цветовая пометка»: красный — «Ветхая»,
    // синий — «Устаревшая», другие цвета — «Зелёный шрифт» и т. п.
    // Цвет строки — цвет её ячеек; если цветов несколько — через запятую.
    static void applyFontColors(FundSheet *sheet, const io::FontColors &colors);
};

// Строка для поиска и сравнения: нижний регистр, ё → е, одиночные пробелы.
QString normalizeForSearch(const QString &text);

// Значение для записи из текста, введённого в форме: числа остаются
// числами (как в остальном столбце), «б/н», «2р20к», «63,3 (2)» — текстом.
QVariant valueFromInput(const QString &text, ColumnRole role, const QVariant &previous = {});

// Старая запись цены: «2р20к», «202р», «45к», «44 р. 10 коп.» → рубли.
bool parseRubles(const QString &text, double *rubles);
// Цена в рублях: число, «131,6», «2р20к». false — «б/ц», «нет», пусто.
bool priceInRubles(const QVariant &value, double *rubles);

// Текст ячейки для показа: лишние пробелы убраны, числа без «.0».
QString displayText(const QVariant &value);

} // namespace bh::core
