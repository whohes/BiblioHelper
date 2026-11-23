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

// Удалить строки листа (номера Excel). Строки ниже поднимаются, ссылки в
// формулах, диапазоны автофильтра, объединений, условного форматирования
// сжимаются (=SUM(D3:D8245) → D3:D8243). Формула, которая ссылалась на
// удалённую ячейку, заменяется своим значением.
void deleteRows(pugi::xml_node worksheet, const QVector<int> &rows);

// То же для имён книги, которые ссылаются на лист sheet («_FilterDatabase»).
void deleteRowsInDefinedNames(pugi::xml_node workbook, const QString &sheet, const QVector<int> &rows);

// Последний номер строки в sheetData (0 — лист пустой).
int lastRowNumber(pugi::xml_node worksheet);

// Скопировать строки (с оформлением, высотой, значениями; формулы → значения)
// в другой лист той же книги, начиная со строки firstTargetRow.
void copyRows(pugi::xml_node source, pugi::xml_node target, const QVector<int> &rows, int firstTargetRow);

// Ширины столбцов — как у листа-источника (если у target их ещё нет).
void copyColumnWidths(pugi::xml_node source, pugi::xml_node target);

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
