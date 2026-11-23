#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace pugi {
class xml_node;
}

namespace bh::io::sheetxml {

// Правка XML листа при добавлении строк в конец данных.
//
// Строки ниже afterRow (итоги, пояснения) сдвигаются вниз на count, а
// ссылки на них — во всех формулах листа, объединённых ячейках,
// автофильтре и условном форматировании. Диапазон, который заканчивается
// ровно на afterRow (=SUM(D3:D8245), автофильтр A2:J8245), растягивается
// на новые строки: он описывает данные, к которым добавились книги.
// Ссылки на другие листы и внутри строк ≤ afterRow не меняются.
void insertRowsAfter(pugi::xml_node worksheet, int afterRow, int count, int templateRow);

// Сдвиг ссылок в тексте формулы или в атрибуте ref/sqref.
QString shiftReferences(const QString &text, int afterRow, int count);

// Заменить все данные служебного листа таблицей строк (текст, inline-строки:
// не трогает общий словарь строк книги). Строка 1 — первая строка таблицы.
void replaceSheetData(pugi::xml_node worksheet, const QVector<QStringList> &rows);

// Расширить автофильтр вправо до столбца lastColumn (новый столбец «Статус»),
// если автофильтр начинается в строке headerRow или следующей. Возвращает true, если изменён.
bool extendAutoFilter(pugi::xml_node worksheet, int headerRow, int lastColumn);

// Оформление нового отчёта для печати и работы: альбомная страница A4
// по ширине листа, закреплённые строки сверху (до headerRow включительно),
// автофильтр на диапазоне filterRef («A4:G120»; пусто — без фильтра).
void setReportLayout(pugi::xml_node worksheet, int headerRow, const QString &filterRef);

// Номер столбца по буквам: «A» → 1, «$AB» → 28.
int columnNumber(const QString &letters);

// Пересчитать формулы при открытии в Excel (итоги после добавления строк).
void requestFullCalcOnLoad(pugi::xml_node workbook);

} // namespace bh::io::sheetxml
