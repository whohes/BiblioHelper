#include "xls_import.h"

#include <QFile>

#include "xlsxdocument.h"

#include <xls.h>

#include <algorithm>
#include <cmath>

namespace bh::core {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QVariant numberValue(double d)
{
    if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15)
        return qlonglong(d);
    return d;
}

QVariant cellValue(const xls::xlsCell *cell)
{
    if (!cell)
        return {};
    switch (cell->id) {
    case XLS_RECORD_BLANK:
    case XLS_RECORD_MULBLANK:
    case XLS_RECORD_BOOLERR:
        return {};
    case XLS_RECORD_NUMBER:
    case XLS_RECORD_RK:
    case XLS_RECORD_MULRK:
        return numberValue(cell->d);
    case XLS_RECORD_FORMULA:
    case XLS_RECORD_FORMULA_ALT:
        // l == 0 — число в d; иначе результат-строка в str («bool», «error» — служебные).
        if (cell->l == 0)
            return numberValue(cell->d);
        if (cell->str && qstrcmp(cell->str, "bool") != 0 && qstrcmp(cell->str, "error") != 0)
            return QString::fromUtf8(cell->str);
        return {};
    default:
        if (cell->str) {
            const QString text = QString::fromUtf8(cell->str);
            if (!text.trimmed().isEmpty())
                return text;
        }
        return {};
    }
}

} // namespace

bool XlsImport::read(const QString &path, QVector<Sheet> *sheets, QString *error)
{
    // Читаем из памяти: libxls открывает файл узкой строкой пути,
    // а путь может быть на русском.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Не удалось открыть файл: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray data = file.readAll();
    xls::xls_error_t code = xls::LIBXLS_OK;
    xls::xlsWorkBook *wb = xls::xls_open_buffer(reinterpret_cast<const unsigned char *>(data.constData()),
                                                size_t(data.size()), "UTF-8", &code);
    if (!wb) {
        setError(error, QStringLiteral("Файл не похож на книгу Excel 97–2003 (%1)")
                            .arg(QString::fromUtf8(xls::xls_getError(code))));
        return false;
    }

    QVector<Sheet> result;
    for (xls::DWORD i = 0; i < wb->sheets.count; ++i) {
        if (wb->sheets.sheet[i].type != 0)
            continue;  // диаграммы и макросы
        xls::xlsWorkSheet *ws = xls::xls_getWorkSheet(wb, int(i));
        if (!ws)
            continue;
        if (xls::xls_parseWorkSheet(ws) != xls::LIBXLS_OK) {
            xls::xls_close_WS(ws);
            continue;
        }
        Sheet sheet;
        sheet.name = QString::fromUtf8(wb->sheets.sheet[i].name);
        if (sheet.name.isEmpty())
            sheet.name = QStringLiteral("Лист%1").arg(i + 1);
        const int lastRow = ws->rows.lastrow;
        const int lastCol = ws->rows.lastcol;
        sheet.rows.resize(lastRow + 1);
        for (int r = 0; r <= lastRow; ++r) {
            QVector<QVariant> &row = sheet.rows[r];
            for (int c = 0; c <= lastCol; ++c) {
                const QVariant value = cellValue(xls::xls_cell(ws, xls::WORD(r), xls::WORD(c)));
                if (!value.isValid())
                    continue;
                if (row.size() <= c)
                    row.resize(c + 1);
                row[c] = value;
            }
        }
        // Пустые строки в конце листа не нужны.
        while (!sheet.rows.isEmpty() && sheet.rows.last().isEmpty())
            sheet.rows.removeLast();
        result.push_back(std::move(sheet));
        xls::xls_close_WS(ws);
    }
    xls::xls_close_WB(wb);

    if (result.isEmpty()) {
        setError(error, QStringLiteral("В файле нет листов с данными"));
        return false;
    }
    *sheets = std::move(result);
    return true;
}

bool XlsImport::writeXlsx(const QVector<Sheet> &sheets, const QString &path, QString *error)
{
    QXlsx::Document doc;
    for (const Sheet &sheet : sheets) {
        // Имя листа в XLSX — до 31 символа, без []:*?/\.
        QString name = sheet.name;
        for (const QChar ch : {QLatin1Char('['), QLatin1Char(']'), QLatin1Char(':'), QLatin1Char('*'),
                               QLatin1Char('?'), QLatin1Char('/'), QLatin1Char('\\')})
            name.replace(ch, QLatin1Char(' '));
        name = name.left(31).trimmed();
        if (name.isEmpty() || doc.sheetNames().contains(name))
            name = QStringLiteral("Лист%1").arg(doc.sheetNames().size() + 1);
        doc.addSheet(name);
        doc.selectSheet(name);
        int width = 0;
        for (int r = 0; r < sheet.rows.size(); ++r) {
            const auto &row = sheet.rows.at(r);
            width = std::max(width, int(row.size()));
            for (int c = 0; c < row.size(); ++c) {
                if (row.at(c).isValid())
                    doc.write(r + 1, c + 1, row.at(c));
            }
        }
        for (int c = 1; c <= width; ++c)
            doc.setColumnWidth(c, c == 2 ? 50 : 14);
    }
    if (!sheets.isEmpty())
        doc.selectSheet(doc.sheetNames().first());
    if (!doc.saveAs(path)) {
        setError(error, QStringLiteral("Не удалось записать рабочую копию: %1").arg(path));
        return false;
    }
    return true;
}

} // namespace bh::core
