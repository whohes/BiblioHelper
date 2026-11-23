#include "csv_import.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextCodec>
#include <QVariant>

#include "xlsxdocument.h"
#include "xlsxformat.h"

#include <algorithm>

namespace bh::core {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

CsvImport::Encoding CsvImport::detectEncoding(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return Encoding::Utf8;
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    QTextCodec::ConverterState state;
    utf8->toUnicode(data.constData(), data.size(), &state);
    return state.invalidChars == 0 ? Encoding::Utf8 : Encoding::Windows1251;
}

QChar CsvImport::detectDelimiter(const QString &text)
{
    const QVector<QChar> candidates{QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char(',')};
    QChar best = QLatin1Char(';');
    int bestScore = -1;
    for (const QChar delimiter : candidates) {
        const QVector<QStringList> rows = parse(text.left(64 * 1024), delimiter);
        // Строк с одинаковым (самым частым) числом столбцов больше 1 — тем лучше.
        QHash<int, int> widths;
        for (const QStringList &row : rows) {
            if (row.size() > 1)
                ++widths[row.size()];
        }
        int score = 0;
        for (auto it = widths.constBegin(); it != widths.constEnd(); ++it)
            score = std::max(score, it.value());
        if (score > bestScore) {
            bestScore = score;
            best = delimiter;
        }
    }
    return best;
}

QVector<QStringList> CsvImport::parse(const QString &text, QChar delimiter)
{
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool fieldStarted = false;
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
            continue;
        }
        if (ch == QLatin1Char('"') && !fieldStarted) {
            quoted = true;
            fieldStarted = true;
        } else if (ch == delimiter) {
            row << field;
            field.clear();
            fieldStarted = false;
        } else if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            if (ch == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            row << field;
            rows.push_back(row);
            row.clear();
            field.clear();
            fieldStarted = false;
        } else {
            field += ch;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows.push_back(row);
    }
    return rows;
}

bool CsvImport::read(const QString &path, Table *table, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Не удалось открыть файл: %1").arg(file.errorString()));
        return false;
    }
    QByteArray data = file.readAll();
    Table result;
    result.encoding = detectEncoding(data);
    QString text;
    if (result.encoding == Encoding::Utf8) {
        if (data.startsWith("\xEF\xBB\xBF"))
            data.remove(0, 3);
        text = QString::fromUtf8(data);
    } else {
        text = QTextCodec::codecForName("Windows-1251")->toUnicode(data);
    }
    result.delimiter = detectDelimiter(text);
    result.rows = parse(text, result.delimiter);
    // Пустые строки в конце файла не нужны.
    while (!result.rows.isEmpty() && result.rows.last().join(QString()).trimmed().isEmpty())
        result.rows.removeLast();
    if (result.rows.isEmpty()) {
        setError(error, QStringLiteral("Файл пустой"));
        return false;
    }
    *table = std::move(result);
    return true;
}

QVariant CsvImport::cellValue(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    static const QRegularExpression number(QStringLiteral("^-?(0|[1-9]\\d{0,14})([.,]\\d+)?$"));
    if (number.match(t).hasMatch()) {
        QString dotted = t;
        dotted.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        if (!dotted.contains(QLatin1Char('.'))) {
            const qlonglong v = dotted.toLongLong(&ok);
            if (ok)
                return v;
        }
        const double d = dotted.toDouble(&ok);
        if (ok)
            return d;
    }
    return t;
}

bool CsvImport::writeXlsx(const Table &table, const QString &path, QString *error)
{
    QXlsx::Document doc;
    doc.addSheet(QStringLiteral("Лист1"));
    int width = 0;
    for (const QStringList &row : table.rows)
        width = std::max(width, int(row.size()));
    for (int r = 0; r < table.rows.size(); ++r) {
        const QStringList &row = table.rows.at(r);
        for (int c = 0; c < row.size(); ++c) {
            const QVariant value = cellValue(row.at(c));
            if (value.isValid())
                doc.write(r + 1, c + 1, value);
        }
    }
    for (int c = 1; c <= width; ++c)
        doc.setColumnWidth(c, c == 2 ? 50 : 14);
    if (!doc.saveAs(path)) {
        setError(error, QStringLiteral("Не удалось записать рабочую копию: %1").arg(path));
        return false;
    }
    return true;
}

} // namespace bh::core
