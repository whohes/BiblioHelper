#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace bh::ui {

namespace {

// Все значки — в сетке 24×24, линии толщиной 2 со скруглёнными концами.
void draw(QPainter &p, IconKind kind, const QColor &color)
{
    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    switch (kind) {
    case IconKind::Open:  // папка
        path.moveTo(3, 7);
        path.lineTo(3, 18.5);
        path.quadTo(3, 20, 4.5, 20);
        path.lineTo(19.5, 20);
        path.quadTo(21, 20, 21, 18.5);
        path.lineTo(21, 9.5);
        path.quadTo(21, 8, 19.5, 8);
        path.lineTo(12, 8);
        path.lineTo(10, 5);
        path.lineTo(4.5, 5);
        path.quadTo(3, 5, 3, 6.5);
        p.drawPath(path);
        p.drawLine(QPointF(3, 11), QPointF(21, 11));
        break;
    case IconKind::Save:
    case IconKind::SaveAs:  // дискета
        path.moveTo(5, 4);
        path.lineTo(16, 4);
        path.lineTo(20, 8);
        path.lineTo(20, 18.5);
        path.quadTo(20, 20, 18.5, 20);
        path.lineTo(5.5, 20);
        path.quadTo(4, 20, 4, 18.5);
        path.lineTo(4, 5.5);
        path.quadTo(4, 4, 5.5, 4);
        p.drawPath(path);
        p.drawRect(QRectF(8, 4, 7, 4.5));
        p.drawRect(QRectF(7.5, 13, 9, 7));
        if (kind == IconKind::SaveAs) {
            p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(15, 23), QPointF(23, 15));
        }
        break;
    case IconKind::Add:  // плюс в круге
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawLine(QPointF(12, 7.5), QPointF(12, 16.5));
        p.drawLine(QPointF(7.5, 12), QPointF(16.5, 12));
        break;
    case IconKind::Edit:  // карандаш
        path.moveTo(15, 5);
        path.lineTo(19, 9);
        path.lineTo(9, 19);
        path.lineTo(4.5, 19.5);
        path.lineTo(5, 15);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(13, 7), QPointF(17, 11));
        break;
    case IconKind::Undo:  // стрелка назад
        path.moveTo(9, 6);
        path.lineTo(4.5, 10.5);
        path.lineTo(9, 15);
        p.drawPath(path);
        path = QPainterPath();
        path.moveTo(4.5, 10.5);
        path.lineTo(14, 10.5);
        path.cubicTo(18, 10.5, 20, 13, 20, 15.5);
        path.cubicTo(20, 18, 18, 20, 14, 20);
        path.lineTo(11, 20);
        p.drawPath(path);
        break;
    case IconKind::Withdraw:  // книга со знаком «минус»
        path.moveTo(4, 5.5);
        path.quadTo(4, 4, 5.5, 4);
        path.lineTo(17, 4);
        path.lineTo(17, 13);
        path.moveTo(4, 5.5);
        path.lineTo(4, 18.5);
        path.quadTo(4, 20, 5.5, 20);
        path.lineTo(12, 20);
        path.moveTo(4, 17);
        path.lineTo(12, 17);
        p.drawPath(path);
        p.drawLine(QPointF(15, 18.5), QPointF(22, 18.5));
        break;
    case IconKind::Export:  // таблица и стрелка наружу
        p.drawRoundedRect(QRectF(3, 4, 13, 16), 1.5, 1.5);
        p.drawLine(QPointF(3, 9.5), QPointF(16, 9.5));
        p.drawLine(QPointF(3, 15), QPointF(16, 15));
        p.drawLine(QPointF(8.5, 9.5), QPointF(8.5, 20));
        p.drawLine(QPointF(13, 12), QPointF(22, 12));
        path.moveTo(18.5, 8.5);
        path.lineTo(22, 12);
        path.lineTo(18.5, 15.5);
        p.drawPath(path);
        break;
    case IconKind::Search:  // лупа
        p.drawEllipse(QPointF(10.5, 10.5), 6, 6);
        p.drawLine(QPointF(15, 15), QPointF(20, 20));
        break;
    case IconKind::ResetFilters:  // воронка и крестик
        path.moveTo(3, 5);
        path.lineTo(17, 5);
        path.lineTo(11.5, 11.5);
        path.lineTo(11.5, 18);
        path.lineTo(8.5, 20);
        path.lineTo(8.5, 11.5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(16, 14), QPointF(21, 19));
        p.drawLine(QPointF(21, 14), QPointF(16, 19));
        break;
    case IconKind::Report:  // лист с таблицей
        path.moveTo(6, 3);
        path.lineTo(14, 3);
        path.lineTo(19, 8);
        path.lineTo(19, 19.5);
        path.quadTo(19, 21, 17.5, 21);
        path.lineTo(6.5, 21);
        path.quadTo(5, 21, 5, 19.5);
        path.lineTo(5, 4.5);
        path.quadTo(5, 3, 6.5, 3);
        p.drawPath(path);
        p.drawLine(QPointF(8.5, 12), QPointF(15.5, 12));
        p.drawLine(QPointF(8.5, 15.5), QPointF(15.5, 15.5));
        p.drawLine(QPointF(12, 12), QPointF(12, 18.5));
        p.drawLine(QPointF(8.5, 18.5), QPointF(15.5, 18.5));
        break;
    case IconKind::Restore:  // стрелка возврата в книгу
        path.moveTo(4, 5.5);
        path.quadTo(4, 4, 5.5, 4);
        path.lineTo(17, 4);
        path.lineTo(17, 11);
        path.moveTo(4, 5.5);
        path.lineTo(4, 18.5);
        path.quadTo(4, 20, 5.5, 20);
        path.lineTo(11, 20);
        path.moveTo(4, 17);
        path.lineTo(10, 17);
        p.drawPath(path);
        p.drawLine(QPointF(14, 17), QPointF(21, 17));
        path = QPainterPath();
        path.moveTo(17, 13.5);
        path.lineTo(13.5, 17);
        path.lineTo(17, 20.5);
        p.drawPath(path);
        break;
    case IconKind::Book:  // раскрытая книга
        path.moveTo(12, 6.5);
        path.cubicTo(9.5, 4.8, 6, 4.5, 3, 5);
        path.lineTo(3, 18);
        path.cubicTo(6, 17.5, 9.5, 17.8, 12, 19.5);
        path.cubicTo(14.5, 17.8, 18, 17.5, 21, 18);
        path.lineTo(21, 5);
        path.cubicTo(18, 4.5, 14.5, 4.8, 12, 6.5);
        path.lineTo(12, 19.5);
        p.drawPath(path);
        break;
    }
}

} // namespace

QPixmap makePixmap(IconKind kind, int size, const QColor &color, qreal devicePixelRatio)
{
    const int px = qRound(size * devicePixelRatio);
    QPixmap pixmap(px, px);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 24.0, px / 24.0);
    draw(p, kind, color);
    p.end();
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

QIcon makeIcon(IconKind kind, const QColor &color)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48})
        icon.addPixmap(makePixmap(kind, size, color), QIcon::Normal);
    // Неактивная кнопка — серый значок.
    const QColor disabled(0xa8, 0xb0, 0xbb);
    for (int size : {16, 20, 24, 32, 40, 48})
        icon.addPixmap(makePixmap(kind, size, disabled), QIcon::Disabled);
    return icon;
}

} // namespace bh::ui
