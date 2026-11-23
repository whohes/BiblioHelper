#include "sheet_xml_rows.h"

#include <QRegularExpression>
#include <QStringList>

#include <pugixml.hpp>

#include <algorithm>
#include <vector>

namespace bh::io::sheetxml {

namespace {

QString columnLetters(const QString &ref)
{
    int i = 0;
    while (i < ref.size() && (ref.at(i).isLetter() || ref.at(i) == QLatin1Char('$')))
        ++i;
    return ref.left(i);
}

// «A12» → 12, «$B$511» → 511.
int rowOf(const QString &ref)
{
    int i = 0;
    while (i < ref.size() && !ref.at(i).isDigit())
        ++i;
    return ref.mid(i).toInt();
}

QString withRow(const QString &ref, int row)
{
    QString letters = columnLetters(ref);
    return letters + QString::number(row);
}

// Ссылка одной ячейки или диапазона без имени листа: «D3», «$A$2:J8245».
QString shiftRange(const QString &range, int afterRow, int count)
{
    const int colon = range.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        const int r = rowOf(range);
        return r > afterRow ? withRow(range, r + count) : range;
    }
    const QString first = range.left(colon);
    const QString last = range.mid(colon + 1);
    const int r1 = rowOf(first);
    const int r2 = rowOf(last);
    if (r1 <= 0 || r2 <= 0)  // «A:A», «3:3» без номеров — не трогаем
        return range;
    if (r1 > afterRow)
        return withRow(first, r1 + count) + QLatin1Char(':') + withRow(last, r2 + count);
    if (r2 >= afterRow)  // диапазон данных: растягивается на новые строки
        return first + QLatin1Char(':') + withRow(last, r2 + count);
    return range;
}

bool isNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.');
}

} // namespace

QString shiftReferences(const QString &text, int afterRow, int count)
{
    // Ячейка или диапазон: [$]A[$]1[:[$]B[$]2]. Строки в кавычках пропускаем.
    static const QRegularExpression re(
        QStringLiteral("\\$?[A-Za-z]{1,3}\\$?[0-9]+(?::\\$?[A-Za-z]{1,3}\\$?[0-9]+)?"));
    QString out;
    out.reserve(text.size() + 8);
    // Разбор по кускам вне кавычек.
    QStringList parts;
    QVector<bool> quoted;
    {
        QString cur;
        bool q = false;
        for (const QChar c : text) {
            if (c == QLatin1Char('"')) {
                if (!q) {
                    parts << cur;
                    quoted << false;
                    cur = QStringLiteral("\"");
                    q = true;
                } else {
                    cur += c;
                    parts << cur;
                    quoted << true;
                    cur.clear();
                    q = false;
                }
                continue;
            }
            cur += c;
        }
        parts << cur;
        quoted << q;
    }

    for (int p = 0; p < parts.size(); ++p) {
        const QString &part = parts.at(p);
        if (quoted.at(p)) {
            out += part;
            continue;
        }
        int last = 0;
        auto it = re.globalMatch(part);
        while (it.hasNext()) {
            const auto m = it.next();
            const int start = m.capturedStart();
            const int end = m.capturedEnd();
            const QChar before = start > 0 ? part.at(start - 1) : QChar();
            const QChar after = end < part.size() ? part.at(end) : QChar();
            // Не ссылка: часть имени (LOG10, Лист1), функция «A1(», ссылка на другой лист «Лист2!A1».
            const bool skip = (start > 0 && (isNameChar(before) || before == QLatin1Char('!')
                                             || before == QLatin1Char('$')))
                || (!after.isNull() && (isNameChar(after) || after == QLatin1Char('(')));
            out += part.mid(last, start - last);
            out += skip ? m.captured() : shiftRange(m.captured(), afterRow, count);
            last = end;
        }
        out += part.mid(last);
    }
    return out;
}

namespace {

void shiftAttribute(pugi::xml_node node, const char *name, int afterRow, int count)
{
    pugi::xml_attribute attr = node.attribute(name);
    if (!attr)
        return;
    const QString value = QString::fromUtf8(attr.value());
    const QString shifted = shiftReferences(value, afterRow, count);
    if (shifted != value)
        attr.set_value(shifted.toUtf8().constData());
}

void shiftFormulasIn(pugi::xml_node row, int afterRow, int count)
{
    for (pugi::xml_node c = row.child("c"); c; c = c.next_sibling("c")) {
        pugi::xml_node f = c.child("f");
        if (!f)
            continue;
        shiftAttribute(f, "ref", afterRow, count);  // общие формулы и формулы массива
        const QString text = QString::fromUtf8(f.text().get());
        if (text.isEmpty())
            continue;
        const QString shifted = shiftReferences(text, afterRow, count);
        if (shifted != text)
            f.text().set(shifted.toUtf8().constData());
    }
}

} // namespace

void insertRowsAfter(pugi::xml_node worksheet, int afterRow, int count, int templateRow)
{
    if (count <= 0)
        return;
    pugi::xml_node sheetData = worksheet.child("sheetData");
    if (!sheetData)
        sheetData = worksheet.append_child("sheetData");

    // 1. Формулы во всех строках, номера строк и ячеек ниже afterRow.
    pugi::xml_node templateNode;
    pugi::xml_node insertAfter;  // последняя строка с номером ≤ afterRow
    for (pugi::xml_node row = sheetData.child("row"); row; row = row.next_sibling("row")) {
        const int r = row.attribute("r").as_int();
        if (r == templateRow)
            templateNode = row;
        shiftFormulasIn(row, afterRow, count);
        if (r <= afterRow) {
            insertAfter = row;
            continue;
        }
        row.attribute("r").set_value(r + count);
        for (pugi::xml_node c = row.child("c"); c; c = c.next_sibling("c")) {
            pugi::xml_attribute ref = c.attribute("r");
            if (ref) {
                const QString value = QString::fromUtf8(ref.value());
                ref.set_value(withRow(value, rowOf(value) + count).toUtf8().constData());
            }
        }
    }

    // 2. Диапазоны листа.
    shiftAttribute(worksheet.child("dimension"), "ref", afterRow, count);
    shiftAttribute(worksheet.child("autoFilter"), "ref", afterRow, count);
    for (pugi::xml_node m = worksheet.child("mergeCells").child("mergeCell"); m; m = m.next_sibling("mergeCell"))
        shiftAttribute(m, "ref", afterRow, count);
    for (pugi::xml_node cf = worksheet.child("conditionalFormatting"); cf;
         cf = cf.next_sibling("conditionalFormatting")) {
        shiftAttribute(cf, "sqref", afterRow, count);
        for (pugi::xml_node rule = cf.child("cfRule"); rule; rule = rule.next_sibling("cfRule")) {
            for (pugi::xml_node f = rule.child("formula"); f; f = f.next_sibling("formula")) {
                const QString text = QString::fromUtf8(f.text().get());
                f.text().set(shiftReferences(text, afterRow, count).toUtf8().constData());
            }
        }
    }
    for (pugi::xml_node dv = worksheet.child("dataValidations").child("dataValidation"); dv;
         dv = dv.next_sibling("dataValidation"))
        shiftAttribute(dv, "sqref", afterRow, count);
    for (pugi::xml_node h = worksheet.child("hyperlinks").child("hyperlink"); h; h = h.next_sibling("hyperlink"))
        shiftAttribute(h, "ref", afterRow, count);

    // 3. Новые пустые строки с оформлением строки-образца (высота, стиль ячеек).
    pugi::xml_node previous = insertAfter;
    for (int i = 1; i <= count; ++i) {
        const int r = afterRow + i;
        pugi::xml_node row = previous ? sheetData.insert_child_after("row", previous)
                                      : sheetData.prepend_child("row");
        if (templateNode) {
            for (pugi::xml_attribute a = templateNode.first_attribute(); a; a = a.next_attribute()) {
                if (std::string(a.name()) != "r")
                    row.append_attribute(a.name()).set_value(a.value());
            }
            for (pugi::xml_node tc = templateNode.child("c"); tc; tc = tc.next_sibling("c")) {
                pugi::xml_node c = row.append_child("c");
                const QString col = columnLetters(QString::fromUtf8(tc.attribute("r").value()));
                c.append_attribute("r").set_value((col + QString::number(r)).toUtf8().constData());
                if (pugi::xml_attribute s = tc.attribute("s"))
                    c.append_attribute("s").set_value(s.value());
            }
        }
        // Атрибут r — первым, как пишет Excel.
        if (pugi::xml_attribute first = row.first_attribute())
            row.insert_attribute_before("r", first).set_value(r);
        else
            row.append_attribute("r").set_value(r);
        previous = row;
    }
}

QString columnName(int column)
{
    QString letters;
    while (column > 0) {
        const int rem = (column - 1) % 26;
        letters.prepend(QChar('A' + rem));
        column = (column - 1) / 26;
    }
    return letters;
}

void replaceSheetData(pugi::xml_node worksheet, const QVector<QStringList> &rows)
{
    pugi::xml_node sheetData = worksheet.child("sheetData");
    if (sheetData)
        worksheet.remove_child(sheetData);
    // sheetData идёт после sheetFormatPr/cols (или sheetViews) по схеме.
    pugi::xml_node after = worksheet.child("cols");
    if (!after)
        after = worksheet.child("sheetFormatPr");
    if (!after)
        after = worksheet.child("sheetViews");
    if (!after)
        after = worksheet.child("dimension");
    sheetData = after ? worksheet.insert_child_after("sheetData", after) : worksheet.prepend_child("sheetData");

    int maxColumns = 0;
    for (int r = 0; r < rows.size(); ++r) {
        pugi::xml_node row = sheetData.append_child("row");
        row.append_attribute("r").set_value(r + 1);
        const QStringList &values = rows.at(r);
        maxColumns = std::max(maxColumns, int(values.size()));
        for (int c = 0; c < values.size(); ++c) {
            if (values.at(c).isEmpty())
                continue;
            pugi::xml_node cell = row.append_child("c");
            cell.append_attribute("r").set_value((columnName(c + 1) + QString::number(r + 1)).toUtf8().constData());
            cell.append_attribute("t").set_value("inlineStr");
            cell.append_child("is").append_child("t").text().set(values.at(c).toUtf8().constData());
        }
    }
    pugi::xml_node dimension = worksheet.child("dimension");
    if (dimension && !rows.isEmpty() && maxColumns > 0) {
        const QString ref = QStringLiteral("A1:%1%2").arg(columnName(maxColumns)).arg(rows.size());
        dimension.attribute("ref").set_value(ref.toUtf8().constData());
    }
}

void requestFullCalcOnLoad(pugi::xml_node workbook)
{
    pugi::xml_node calcPr = workbook.child("calcPr");
    if (!calcPr) {
        // calcPr идёт после sheets/definedNames; append безопасен, если дальше ничего нет.
        pugi::xml_node after = workbook.child("definedNames") ? workbook.child("definedNames")
                                                              : workbook.child("sheets");
        calcPr = after ? workbook.insert_child_after("calcPr", after) : workbook.append_child("calcPr");
    }
    pugi::xml_attribute attr = calcPr.attribute("fullCalcOnLoad");
    if (!attr)
        attr = calcPr.append_attribute("fullCalcOnLoad");
    attr.set_value("1");
}

} // namespace bh::io::sheetxml
