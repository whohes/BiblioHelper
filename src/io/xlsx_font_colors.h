#pragma once

#include <QHash>
#include <QPair>
#include <QRgb>
#include <QString>

namespace bh::io {

// Явные цвета шрифта непустых ячеек листа: ключ — (строка, столбец) Excel.
// Чёрный, серый и цвета темы не возвращаются.
//
// Читается через QXlsx и только для чтения: у OpenXLSX чтение цвета шрифта
// дописывает атрибут в styles.xml, а рабочую копию менять нельзя.
using FontColors = QHash<QPair<int, int>, QRgb>;

FontColors readFontColors(const QString &path, const QString &sheet, QString *error = nullptr);

// «Красный», «Синий», «Зелёный»… по оттенку; пусто для чёрного/серого.
QString colorName(QRgb color);

} // namespace bh::io
