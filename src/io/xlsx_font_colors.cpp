#include "xlsx_font_colors.h"

#include "xlsxcell.h"
#include "xlsxcellrange.h"
#include "xlsxdocument.h"
#include "xlsxformat.h"
#include "xlsxworksheet.h"

#include <QColor>

namespace bh::io {

FontColors readFontColors(const QString &path, const QString &sheet, QString *error)
{
    FontColors result;
    QXlsx::Document document(path);
    if (!document.isLoadPackage()) {
        if (error)
            *error = QStringLiteral("Не удалось прочитать оформление файла");
        return result;
    }
    auto *worksheet = dynamic_cast<QXlsx::Worksheet *>(document.sheet(sheet));
    if (!worksheet)
        return result;

    const QXlsx::CellRange range = worksheet->dimension();
    for (int r = range.firstRow(); r <= range.lastRow(); ++r) {
        for (int c = range.firstColumn(); c <= range.lastColumn(); ++c) {
            const std::shared_ptr<QXlsx::Cell> cell = worksheet->cellAt(r, c);
            if (!cell || cell->value().toString().trimmed().isEmpty())
                continue;
            const QColor color = cell->format().fontColor();
            if (color.isValid() && !colorName(color.rgb()).isEmpty())
                result.insert({r, c}, color.rgb());
        }
    }
    return result;
}

QString colorName(QRgb rgb)
{
    const QColor color = QColor::fromRgb(rgb).toHsv();
    // Чёрный, серый и почти бесцветные — не пометка.
    if (color.saturation() < 80 || color.value() < 60)
        return {};
    const int hue = color.hue();
    if (hue < 20 || hue >= 330)
        return QStringLiteral("Красный");
    if (hue < 45)
        return QStringLiteral("Оранжевый");
    if (hue < 70)
        return QStringLiteral("Жёлтый");
    if (hue < 170)
        return QStringLiteral("Зелёный");
    if (hue < 260)
        return QStringLiteral("Синий");
    return QStringLiteral("Фиолетовый");
}

} // namespace bh::io
