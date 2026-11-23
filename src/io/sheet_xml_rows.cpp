#include "sheet_xml_rows.h"

#include <QRegularExpression>
#include <QStringList>

#include <functional>

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

namespace {

// Заменяет каждую ссылку на ячейку или диапазон (вне кавычек, не на другой
// лист и не часть имени) результатом mapRange.
QString rewriteReferences(const QString &text, const std::function<QString(const QString &)> &mapRange)
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
            out += skip ? m.captured() : mapRange(m.captured());
            last = end;
        }
        out += part.mid(last);
    }
    return out;
}

} // namespace

QString shiftReferences(const QString &text, int afterRow, int count)
{
    return rewriteReferences(text, [=](const QString &range) { return shiftRange(range, afterRow, count); });
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

namespace {

// Номера строк после удаления: строка r становится r − (удалённых выше).
class RowMap
{
public:
    explicit RowMap(QVector<int> deleted)
        : m_deleted(std::move(deleted))
    {
        std::sort(m_deleted.begin(), m_deleted.end());
        m_deleted.erase(std::unique(m_deleted.begin(), m_deleted.end()), m_deleted.end());
    }

    bool isDeleted(int r) const { return std::binary_search(m_deleted.begin(), m_deleted.end(), r); }
    int map(int r) const
    {
        return r - int(std::lower_bound(m_deleted.begin(), m_deleted.end(), r) - m_deleted.begin());
    }
    // «D3», «A2:J8245» → новые номера. hit — ссылка на удалённую ячейку
    // или диапазон только из удалённых строк.
    QString mapRange(const QString &range, bool *hit) const
    {
        const int colon = range.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            const int r = rowOf(range);
            if (r <= 0)
                return range;
            if (isDeleted(r)) {
                *hit = true;
                return range;
            }
            return withRow(range, map(r));
        }
        const QString first = range.left(colon);
        const QString last = range.mid(colon + 1);
        int r1 = rowOf(first);
        int r2 = rowOf(last);
        if (r1 <= 0 || r2 <= 0)
            return range;
        while (r1 <= r2 && isDeleted(r1))
            ++r1;
        while (r2 >= r1 && isDeleted(r2))
            --r2;
        if (r1 > r2) {
            *hit = true;
            return range;
        }
        return withRow(first, map(r1)) + QLatin1Char(':') + withRow(last, map(r2));
    }
    QString mapText(const QString &text, bool *hit) const
    {
        return rewriteReferences(text, [&](const QString &range) { return mapRange(range, hit); });
    }

private:
    QVector<int> m_deleted;
};

// Формулу — в её последнее значение. Строковый результат (t="str") без
// формулы Excel не принимает, поэтому он становится встроенной строкой.
void replaceFormulaWithValue(pugi::xml_node c)
{
    pugi::xml_node f = c.child("f");
    if (!f)
        return;
    c.remove_child(f);
    pugi::xml_attribute t = c.attribute("t");
    if (t && std::string(t.value()) == "str") {
        const std::string text = c.child("v").text().get();
        c.remove_child("v");
        t.set_value("inlineStr");
        c.append_child("is").append_child("t").text().set(text.c_str());
    }
}

void mapAttribute(pugi::xml_node node, const char *name, const RowMap &map, bool *hit)
{
    pugi::xml_attribute attr = node.attribute(name);
    if (!attr)
        return;
    const QString value = QString::fromUtf8(attr.value());
    const QString mapped = map.mapText(value, hit);
    if (mapped != value)
        attr.set_value(mapped.toUtf8().constData());
}

// Для sqref из нескольких диапазонов через пробел: убрать те, что целиком в удалённых строках.
void mapSqref(pugi::xml_node node, const char *name, const RowMap &map)
{
    pugi::xml_attribute attr = node.attribute(name);
    if (!attr)
        return;
    QStringList kept;
    for (const QString &range : QString::fromUtf8(attr.value()).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool hit = false;
        const QString mapped = map.mapRange(range, &hit);
        if (!hit)
            kept << mapped;
    }
    if (kept.isEmpty())
        node.parent().remove_child(node);
    else
        attr.set_value(kept.join(QLatin1Char(' ')).toUtf8().constData());
}

} // namespace

void deleteRows(pugi::xml_node worksheet, const QVector<int> &rows)
{
    if (rows.isEmpty())
        return;
    const RowMap map(rows);
    pugi::xml_node sheetData = worksheet.child("sheetData");

    std::vector<pugi::xml_node> removed;
    for (pugi::xml_node row = sheetData.child("row"); row; row = row.next_sibling("row")) {
        const int r = row.attribute("r").as_int();
        if (map.isDeleted(r)) {
            removed.push_back(row);
            continue;
        }
        for (pugi::xml_node c = row.child("c"); c; c = c.next_sibling("c")) {
            if (pugi::xml_node f = c.child("f")) {
                bool hit = false;
                pugi::xml_attribute fref = f.attribute("ref");
                const QString text = QString::fromUtf8(f.text().get());
                const QString mapped = text.isEmpty() ? text : map.mapText(text, &hit);
                if (hit) {
                    // Формула ссылалась на удалённую строку: оставляем её значение,
                    // иначе Excel покажет #ССЫЛКА!.
                    replaceFormulaWithValue(c);
                } else {
                    if (mapped != text)
                        f.text().set(mapped.toUtf8().constData());
                    if (fref) {
                        bool refHit = false;
                        const QString value = QString::fromUtf8(fref.value());
                        const QString m = map.mapText(value, &refHit);
                        if (!refHit && m != value)
                            fref.set_value(m.toUtf8().constData());
                    }
                }
            }
            if (pugi::xml_attribute ref = c.attribute("r")) {
                const QString value = QString::fromUtf8(ref.value());
                const int newRow = map.map(r);
                if (newRow != r)
                    ref.set_value(withRow(value, newRow).toUtf8().constData());
            }
        }
        const int newRow = map.map(r);
        if (newRow != r)
            row.attribute("r").set_value(newRow);
    }
    for (pugi::xml_node row : removed)
        sheetData.remove_child(row);

    bool ignored = false;
    mapAttribute(worksheet.child("dimension"), "ref", map, &ignored);
    if (pugi::xml_node filter = worksheet.child("autoFilter")) {
        bool hit = false;
        mapAttribute(filter, "ref", map, &hit);
    }
    if (pugi::xml_node merges = worksheet.child("mergeCells")) {
        std::vector<pugi::xml_node> drop;
        for (pugi::xml_node m = merges.child("mergeCell"); m; m = m.next_sibling("mergeCell")) {
            bool hit = false;
            mapAttribute(m, "ref", map, &hit);
            if (hit)
                drop.push_back(m);
        }
        for (pugi::xml_node m : drop)
            merges.remove_child(m);
        int count = 0;
        for (pugi::xml_node m = merges.child("mergeCell"); m; m = m.next_sibling("mergeCell"))
            ++count;
        if (count == 0)
            worksheet.remove_child(merges);
        else if (pugi::xml_attribute a = merges.attribute("count"))
            a.set_value(count);
    }
    std::vector<pugi::xml_node> cfs;
    for (pugi::xml_node cf = worksheet.child("conditionalFormatting"); cf; cf = cf.next_sibling("conditionalFormatting"))
        cfs.push_back(cf);
    for (pugi::xml_node cf : cfs) {
        for (pugi::xml_node rule = cf.child("cfRule"); rule; rule = rule.next_sibling("cfRule")) {
            for (pugi::xml_node f = rule.child("formula"); f; f = f.next_sibling("formula")) {
                bool hit = false;
                const QString text = QString::fromUtf8(f.text().get());
                f.text().set(map.mapText(text, &hit).toUtf8().constData());
            }
        }
        mapSqref(cf, "sqref", map);
    }
    if (pugi::xml_node dvs = worksheet.child("dataValidations")) {
        std::vector<pugi::xml_node> list;
        for (pugi::xml_node dv = dvs.child("dataValidation"); dv; dv = dv.next_sibling("dataValidation"))
            list.push_back(dv);
        for (pugi::xml_node dv : list)
            mapSqref(dv, "sqref", map);
    }
    if (pugi::xml_node links = worksheet.child("hyperlinks")) {
        std::vector<pugi::xml_node> list;
        for (pugi::xml_node h = links.child("hyperlink"); h; h = h.next_sibling("hyperlink"))
            list.push_back(h);
        for (pugi::xml_node h : list)
            mapSqref(h, "ref", map);
    }
}

void deleteRowsInDefinedNames(pugi::xml_node workbook, const QString &sheet, const QVector<int> &rows)
{
    const RowMap map(rows);
    // «Лист1!$A$5:$J$3143» или «'Мой лист'!A1».
    const QString plain = QRegularExpression::escape(sheet);
    const QRegularExpression re(QStringLiteral("(?:'%1'|%1)!(\\$?[A-Za-z]{1,3}\\$?[0-9]+(?::\\$?[A-Za-z]{1,3}\\$?[0-9]+)?)")
                                    .arg(plain));
    for (pugi::xml_node name = workbook.child("definedNames").child("definedName"); name;
         name = name.next_sibling("definedName")) {
        const QString text = QString::fromUtf8(name.text().get());
        QString out;
        int last = 0;
        auto it = re.globalMatch(text);
        bool changed = false;
        while (it.hasNext()) {
            const auto m = it.next();
            bool hit = false;
            const QString mapped = map.mapRange(m.captured(1), &hit);
            out += text.mid(last, m.capturedStart(1) - last) + (hit ? m.captured(1) : mapped);
            last = m.capturedEnd(1);
            changed = changed || (!hit && mapped != m.captured(1));
        }
        if (changed) {
            out += text.mid(last);
            name.text().set(out.toUtf8().constData());
        }
    }
}

int lastRowNumber(pugi::xml_node worksheet)
{
    int last = 0;
    for (pugi::xml_node row = worksheet.child("sheetData").child("row"); row; row = row.next_sibling("row"))
        last = std::max(last, row.attribute("r").as_int());
    return last;
}

void copyRows(pugi::xml_node source, pugi::xml_node target, const QVector<int> &rows, int firstTargetRow)
{
    pugi::xml_node targetData = target.child("sheetData");
    if (!targetData)
        targetData = target.append_child("sheetData");
    QHash<int, pugi::xml_node> byNumber;
    for (pugi::xml_node row = source.child("sheetData").child("row"); row; row = row.next_sibling("row"))
        byNumber.insert(row.attribute("r").as_int(), row);

    int r = firstTargetRow;
    for (int sourceRow : rows) {
        const pugi::xml_node from = byNumber.value(sourceRow);
        pugi::xml_node row = from ? targetData.append_copy(from) : targetData.append_child("row");
        if (pugi::xml_attribute a = row.attribute("r"))
            a.set_value(r);
        else
            row.prepend_attribute("r").set_value(r);
        row.remove_attribute("spans");
        for (pugi::xml_node c = row.child("c"); c; c = c.next_sibling("c")) {
            // В архиве — значения: формулы ссылались бы на строки другого листа.
            replaceFormulaWithValue(c);
            if (pugi::xml_attribute ref = c.attribute("r")) {
                const QString value = QString::fromUtf8(ref.value());
                ref.set_value(withRow(value, r).toUtf8().constData());
            }
        }
        ++r;
    }
    // Диапазон листа Excel пересчитает сам.
    if (pugi::xml_node dimension = target.child("dimension"))
        target.remove_child(dimension);
}

void copyColumnWidths(pugi::xml_node source, pugi::xml_node target)
{
    pugi::xml_node cols = source.child("cols");
    if (!cols || target.child("cols"))
        return;
    pugi::xml_node anchor = target.child("sheetFormatPr");
    if (!anchor)
        anchor = target.child("sheetViews");
    if (!anchor)
        anchor = target.child("dimension");
    if (anchor)
        target.insert_copy_after(cols, anchor);
    else
        target.insert_copy_before(cols, target.child("sheetData"));
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

int columnNumber(const QString &letters)
{
    int n = 0;
    for (const QChar c : letters) {
        if (c == QLatin1Char('$'))
            continue;
        const QChar u = c.toUpper();
        if (u < QLatin1Char('A') || u > QLatin1Char('Z'))
            break;
        n = n * 26 + (u.unicode() - 'A' + 1);
    }
    return n;
}

namespace {

// Вставить элемент на своё место: после последнего из тех, что идут раньше
// по схеме листа (порядок элементов в XLSX важен для Excel).
pugi::xml_node childInOrder(pugi::xml_node worksheet, const char *name, std::initializer_list<const char *> before)
{
    pugi::xml_node node = worksheet.child(name);
    if (node)
        return node;
    pugi::xml_node anchor;
    for (const char *b : before) {
        if (pugi::xml_node n = worksheet.child(b))
            anchor = n;
    }
    return anchor ? worksheet.insert_child_after(name, anchor) : worksheet.prepend_child(name);
}

void setAttr(pugi::xml_node node, const char *name, const char *value)
{
    pugi::xml_attribute a = node.attribute(name);
    if (!a)
        a = node.append_attribute(name);
    a.set_value(value);
}

} // namespace

void setReportLayout(pugi::xml_node worksheet, int headerRow, const QString &filterRef)
{
    // Уместить по ширине страницы.
    pugi::xml_node sheetPr = worksheet.child("sheetPr");
    if (!sheetPr)
        sheetPr = worksheet.prepend_child("sheetPr");
    pugi::xml_node fit = sheetPr.child("pageSetUpPr");
    if (!fit)
        fit = sheetPr.append_child("pageSetUpPr");
    setAttr(fit, "fitToPage", "1");

    // Закрепить шапку.
    pugi::xml_node views = childInOrder(worksheet, "sheetViews", {"sheetPr", "dimension"});
    pugi::xml_node view = views.child("sheetView");
    if (!view) {
        view = views.append_child("sheetView");
        setAttr(view, "workbookViewId", "0");
    }
    pugi::xml_node pane = view.child("pane");
    if (!pane)
        pane = view.prepend_child("pane");
    const QByteArray split = QByteArray::number(headerRow);
    const QByteArray topLeft = "A" + QByteArray::number(headerRow + 1);
    setAttr(pane, "ySplit", split.constData());
    setAttr(pane, "topLeftCell", topLeft.constData());
    setAttr(pane, "activePane", "bottomLeft");
    setAttr(pane, "state", "frozen");
    for (pugi::xml_node sel = view.child("selection"); sel; sel = view.child("selection"))
        view.remove_child(sel);
    pugi::xml_node selection = view.append_child("selection");
    setAttr(selection, "pane", "bottomLeft");

    if (!filterRef.isEmpty()) {
        pugi::xml_node filter = childInOrder(worksheet, "autoFilter",
                                             {"sheetData", "sheetCalcPr", "sheetProtection", "protectedRanges",
                                              "scenarios"});
        setAttr(filter, "ref", filterRef.toUtf8().constData());
    }

    // Excel ждёт описания столбцов по порядку, а QXlsx пишет их вразнобой.
    if (pugi::xml_node cols = worksheet.child("cols")) {
        std::vector<pugi::xml_node> list;
        for (pugi::xml_node col = cols.child("col"); col; col = col.next_sibling("col"))
            list.push_back(col);
        std::stable_sort(list.begin(), list.end(), [](pugi::xml_node a, pugi::xml_node b) {
            return a.attribute("min").as_int() < b.attribute("min").as_int();
        });
        for (pugi::xml_node col : list)
            cols.append_move(col);
    }

    pugi::xml_node setup = childInOrder(worksheet, "pageSetup",
                                        {"sheetData", "autoFilter", "mergeCells", "conditionalFormatting",
                                         "dataValidations", "hyperlinks", "printOptions", "pageMargins"});
    setAttr(setup, "paperSize", "9");  // A4
    setAttr(setup, "orientation", "landscape");
    setAttr(setup, "fitToWidth", "1");
    setAttr(setup, "fitToHeight", "0");
}

bool extendAutoFilter(pugi::xml_node worksheet, int headerRow, int lastColumn)
{
    pugi::xml_attribute ref = worksheet.child("autoFilter").attribute("ref");
    if (!ref)
        return false;
    const QString range = QString::fromUtf8(ref.value());
    const int colon = range.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return false;
    const QString first = range.left(colon);
    const QString last = range.mid(colon + 1);
    // Автофильтр бывает и на строку ниже заголовков (Богдановская: заголовки
    // в 4-й строке, автофильтр с 5-й).
    const int top = rowOf(first);
    if ((top != headerRow && top != headerRow + 1) || columnNumber(columnLetters(last)) >= lastColumn)
        return false;
    const QString extended = first + QLatin1Char(':') + columnName(lastColumn) + QString::number(rowOf(last));
    ref.set_value(extended.toUtf8().constData());
    return true;
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
