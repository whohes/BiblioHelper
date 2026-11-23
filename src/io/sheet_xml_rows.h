#pragma once

#include <QString>

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

// Пересчитать формулы при открытии в Excel (итоги после добавления строк).
void requestFullCalcOnLoad(pugi::xml_node workbook);

} // namespace bh::io::sheetxml
