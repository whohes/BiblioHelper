#pragma once

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
    Other,
};

struct FundColumn
{
    int excelColumn = 0;  // номер столбца Excel (с 1)
    QString title;        // заголовок для показа
    ColumnRole role = ColumnRole::Other;
};

// Одна запись фонда = одна строка Excel = один экземпляр.
struct FundRecord
{
    int excelRow = 0;
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
};

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

    // Раскрашивает ячейки по цветам шрифта из Excel и, если цветные строки
    // есть, добавляет столбец «Цветовая пометка»: красный — «Ветхая»,
    // синий — «Устаревшая», другие цвета — «Зелёный шрифт» и т. п.
    // Цвет строки — цвет её ячеек; если цветов несколько — через запятую.
    static void applyFontColors(FundSheet *sheet, const io::FontColors &colors);
};

// Строка для поиска и сравнения: нижний регистр, ё → е, одиночные пробелы.
QString normalizeForSearch(const QString &text);

// Текст ячейки для показа: лишние пробелы убраны, числа без «.0».
QString displayText(const QVariant &value);

} // namespace bh::core
