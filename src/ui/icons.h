#pragma once

#include <QColor>
#include <QIcon>

namespace bh::ui {

// Значки кнопок, нарисованные кодом (QPainter): не нужны файлы картинок
// и модуль SVG, чёткие при любом масштабе Windows, цвет задаётся.
enum class IconKind {
    Open,
    Save,
    SaveAs,
    Add,
    Edit,
    Undo,
    Withdraw,
    Export,
    Search,
    ResetFilters,
    Report,
    Restore,
    Book,
};

QIcon makeIcon(IconKind kind, const QColor &color = QColor(0x2f, 0x3b, 0x4c));
// Картинка для пустого экрана и т. п.
QPixmap makePixmap(IconKind kind, int size, const QColor &color, qreal devicePixelRatio = 1.0);

} // namespace bh::ui
