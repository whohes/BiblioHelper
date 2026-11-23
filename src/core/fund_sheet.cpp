#include "fund_sheet.h"

#include "io/xlsx_font_colors.h"
#include "io/xlsx_workbook.h"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace bh::core {

namespace {

constexpr int kHeaderSearchRows = 40;
constexpr int kMaxColumns = 60;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString columnLetter(int column)
{
    QString letters;
    while (column > 0) {
        const int rem = (column - 1) % 26;
        letters.prepend(QChar('A' + rem));
        column = (column - 1) / 26;
    }
    return letters;
}

// Значение цветовых пометок в фондах (уточнено у библиотеки):
// красный шрифт — ветхая книга, синий — устаревшая по содержанию.
QString colorMarkLabel(const QString &colorName)
{
    if (colorName.isEmpty())
        return {};
    if (colorName == QStringLiteral("Красный"))
        return QStringLiteral("Ветхая");
    if (colorName == QStringLiteral("Синий"))
        return QStringLiteral("Устаревшая");
    return colorName + QStringLiteral(" шрифт");
}

bool isFilled(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return false;
    if (v.type() == QVariant::String)
        return !v.toString().trimmed().isEmpty();
    return true;
}

int headerScore(const QVector<QVariant> &values)
{
    int score = 0;
    for (const QVariant &v : values) {
        if (v.type() != QVariant::String)
            continue;
        if (FundSheetReader::roleForHeader(v.toString()) != ColumnRole::Other)
            ++score;
    }
    return score;
}

} // namespace

int FundSheet::columnIndex(ColumnRole role) const
{
    for (int i = 0; i < columns.size(); ++i) {
        if (columns.at(i).role == role)
            return i;
    }
    return -1;
}

QString normalizeForSearch(const QString &text)
{
    QString s = text.toLower();
    s.replace(QChar(0x0451), QChar(0x0435)); // ё → е
    return s.simplified();
}

QString displayText(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return {};
    switch (value.type()) {
    case QVariant::Double: {
        const double d = value.toDouble();
        if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15)
            return QString::number(static_cast<qint64>(d));
        return QLocale(QLocale::Russian).toString(d, 'g', 12);
    }
    case QVariant::Date:
        return value.toDate().toString(QStringLiteral("dd.MM.yyyy"));
    case QVariant::Bool:
        return value.toBool() ? QStringLiteral("Да") : QStringLiteral("Нет");
    case QVariant::String:
        return value.toString().simplified();
    default:
        return value.toString();
    }
}

QVariant valueFromInput(const QString &input, ColumnRole role, const QVariant &previous)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return {};
    const bool wasNumber = previous.type() == QVariant::Double || previous.type() == QVariant::LongLong
        || previous.type() == QVariant::Int;
    const bool numericColumn = role == ColumnRole::Number || role == ColumnRole::Inventory
        || role == ColumnRole::Quantity || role == ColumnRole::Price || role == ColumnRole::Year;
    // Шифр отдела «63,3» — не число, а «84» — число, как в файлах.
    const bool integersOnly = role == ColumnRole::Department;
    if (!wasNumber && !numericColumn && !integersOnly)
        return text;

    bool ok = false;
    const qlonglong integer = text.toLongLong(&ok);
    if (ok)
        return integer;
    if (integersOnly && !wasNumber)
        return text;
    QString decimal = text;
    decimal.replace(QLatin1Char(','), QLatin1Char('.'));
    const double d = decimal.toDouble(&ok);
    if (ok && !decimal.contains(QLatin1Char(' ')))
        return d;
    return text;
}

ColumnRole FundSheetReader::roleForHeader(const QString &header)
{
    const QString h = normalizeForSearch(header);
    if (h.isEmpty())
        return ColumnRole::Other;
    if (h == QStringLiteral("статус"))
        return ColumnRole::Status;
    if (h.contains(QStringLiteral("автор")) || h.contains(QStringLiteral("назван")))
        return ColumnRole::AuthorTitle;
    if (h.contains(QStringLiteral("инв")))
        return ColumnRole::Inventory;
    if (h.contains(QStringLiteral("кол")) && (h.contains(QStringLiteral("во")) || h.contains(QStringLiteral("шт"))))
        return ColumnRole::Quantity;
    if (h.contains(QStringLiteral("цена")) || h.contains(QStringLiteral("стоим")))
        return ColumnRole::Price;
    if (h.contains(QStringLiteral("год")))
        return ColumnRole::Year;
    if (h.contains(QStringLiteral("отдел")) || h.contains(QStringLiteral("шифр")))
        return ColumnRole::Department;
    if (h.startsWith(QChar(0x2116)) || h.contains(QStringLiteral("п/п")) || h.contains(QStringLiteral("пп")))
        return ColumnRole::Number;
    return ColumnRole::Other;
}

bool FundSheetReader::read(const io::XlsxWorkbook &workbook, const QString &sheet, FundSheet *out,
                           QString *error)
{
    const io::SheetSize size = workbook.sheetSize(sheet);
    if (size.rows < 2 || size.columns < 1) {
        setError(error, QStringLiteral("Лист «%1» пустой").arg(sheet));
        return false;
    }
    const int columnCount = std::min(size.columns, kMaxColumns);

    const QVector<io::SheetRow> rows = workbook.readRows(sheet, 1, size.rows, columnCount, error);
    if (rows.isEmpty()) {
        setError(error, QStringLiteral("Лист «%1» пустой").arg(sheet));
        return false;
    }

    // Строка заголовков: в первых строках листа, больше всего «узнаваемых» названий.
    int headerIndex = -1;
    int bestScore = 1;
    for (int i = 0; i < rows.size() && rows.at(i).row <= kHeaderSearchRows; ++i) {
        const int score = headerScore(rows.at(i).values);
        if (score > bestScore) {
            bestScore = score;
            headerIndex = i;
        }
    }
    if (headerIndex < 0) {
        setError(error, QStringLiteral("На листе «%1» не найдена строка с заголовками столбцов "
                                       "(«Автор и название», «Инв. №»…)").arg(sheet));
        return false;
    }
    const QVector<QVariant> &header = rows.at(headerIndex).values;

    // Какие столбцы заполнены хотя бы в одной строке ниже заголовка.
    QVector<bool> used(columnCount, false);
    for (int i = headerIndex + 1; i < rows.size(); ++i) {
        const auto &values = rows.at(i).values;
        for (int c = 0; c < columnCount; ++c)
            used[c] = used[c] || isFilled(values.at(c));
    }

    FundSheet result;
    result.sheetName = sheet;
    result.headerRow = rows.at(headerIndex).row;
    for (int c = 0; c < columnCount; ++c) {
        const QString title = displayText(header.at(c));
        if (title.isEmpty() && !used[c])
            continue;
        FundColumn column;
        column.excelColumn = c + 1;
        if (title.isEmpty()) {
            column.role = ColumnRole::Mark;
            column.title = QStringLiteral("Пометка (%1)").arg(columnLetter(c + 1));
        } else {
            column.role = roleForHeader(title);
            column.title = title;
            // «автор книги и название» → «Автор книги и название»
            column.title[0] = column.title.at(0).toUpper();
        }
        result.columns.push_back(column);
    }

    const int authorCol = result.columnIndex(ColumnRole::AuthorTitle);
    const int numberCol = result.columnIndex(ColumnRole::Number);
    const int invCol = result.columnIndex(ColumnRole::Inventory);
    const int yearCol = result.columnIndex(ColumnRole::Year);

    for (int i = headerIndex + 1; i < rows.size(); ++i) {
        const auto &source = rows.at(i).values;
        FundRecord record;
        record.excelRow = rows.at(i).row;
        record.values.reserve(result.columns.size());
        int filled = 0;
        for (const FundColumn &column : qAsConst(result.columns)) {
            const QVariant v = source.at(column.excelColumn - 1);
            record.values.push_back(v);
            filled += isFilled(v) ? 1 : 0;
        }
        auto has = [&](int idx) { return idx >= 0 && isFilled(record.values.at(idx)); };
        bool isBook;
        if (authorCol >= 0)
            isBook = has(authorCol) && (has(numberCol) || has(invCol) || has(yearCol));
        else
            isBook = filled >= 2;
        if (isBook)
            result.records.push_back(std::move(record));
    }

    addWithdrawalColumns(&result);
    *out = std::move(result);
    return true;
}

void FundSheetReader::addWithdrawalColumns(FundSheet *sheet)
{
    auto add = [sheet](int excelColumn, const QString &title, ColumnRole role, bool inFile) {
        if (sheet->columnIndex(role) >= 0)
            return;
        FundColumn column;
        column.excelColumn = excelColumn;
        column.title = title;
        column.role = role;
        column.inFile = inFile;
        sheet->columns.push_back(column);
        for (FundRecord &record : sheet->records) {
            record.values.push_back({});
            if (!record.colors.isEmpty())
                record.colors.push_back(0);
        }
    };
    int lastColumn = 0;
    for (const FundColumn &column : qAsConst(sheet->columns))
        lastColumn = std::max(lastColumn, column.excelColumn);
    add(lastColumn + 1, QStringLiteral("Статус"), ColumnRole::Status, false);
    add(0, QStringLiteral("Дата выбытия"), ColumnRole::WithdrawDate, false);
    add(0, QStringLiteral("Причина выбытия"), ColumnRole::WithdrawReason, false);
    add(0, QStringLiteral("Примечание"), ColumnRole::WithdrawNote, false);
}

bool FundSheet::isWithdrawn(const FundRecord &record) const
{
    const int date = columnIndex(ColumnRole::WithdrawDate);
    return date >= 0 && date < record.values.size() && record.values.at(date).isValid()
        && !record.values.at(date).isNull();
}

// Старая запись цены: «2р20к», «202р», «45к», «44 р. 10 коп.» → рубли.
bool parseRubles(const QString &text, double *rubles)
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*(?:(\\d+)\\s*р(?:уб)?\\.?)?\\s*(?:(\\d{1,2})\\s*к(?:оп)?\\.?)?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch() || (m.captured(1).isEmpty() && m.captured(2).isEmpty()))
        return false;
    *rubles = m.captured(1).toDouble() + (m.captured(2).isEmpty() ? 0.0 : m.captured(2).toDouble() / 100.0);
    return true;
}

bool priceInRubles(const QVariant &value, double *rubles)
{
    if (value.type() == QVariant::Double || value.type() == QVariant::LongLong || value.type() == QVariant::Int) {
        *rubles = value.toDouble();
        return true;
    }
    const QString text = displayText(value);
    if (text.isEmpty())
        return false;
    bool ok = false;
    QString decimal = text;
    decimal.replace(QLatin1Char(','), QLatin1Char('.'));
    const double d = decimal.toDouble(&ok);
    if (ok) {
        *rubles = d;
        return true;
    }
    return parseRubles(text, rubles);
}

QString archiveSheetName()
{
    return QStringLiteral("Архив выбывших");
}

QStringList withdrawalReasons()
{
    return {QStringLiteral("ветхость"), QStringLiteral("утеря"), QStringLiteral("устаревшая по содержанию"),
            QStringLiteral("непрофильная"), QStringLiteral("другое")};
}

QString withdrawalStatusText(const QDate &date, const QString &reason)
{
    QString text = QStringLiteral("Выбыло ") + date.toString(QStringLiteral("dd.MM.yyyy"));
    if (!reason.trimmed().isEmpty())
        text += QStringLiteral(", ") + reason.trimmed();
    return text;
}

void FundSheetReader::applyFontColors(FundSheet *sheet, const io::FontColors &colors)
{
    if (colors.isEmpty() || sheet->columnIndex(ColumnRole::ColorMark) >= 0)
        return;

    QVector<QString> marks(sheet->records.size());
    bool any = false;
    for (int i = 0; i < sheet->records.size(); ++i) {
        FundRecord &record = sheet->records[i];
        QStringList names;
        QVector<QRgb> rowColors(sheet->columns.size(), 0);
        bool rowHas = false;
        for (int c = 0; c < sheet->columns.size(); ++c) {
            const auto it = colors.constFind({record.excelRow, sheet->columns.at(c).excelColumn});
            if (it == colors.constEnd())
                continue;
            rowColors[c] = it.value();
            rowHas = true;
            const QString name = colorMarkLabel(io::colorName(it.value()));
            if (!name.isEmpty() && !names.contains(name))
                names << name;
        }
        if (!rowHas)
            continue;
        record.colors = rowColors;
        names.sort();
        marks[i] = names.join(QStringLiteral(", "));
        any = true;
    }
    if (!any)
        return;

    FundColumn column;
    column.excelColumn = 0;  // нет в Excel: вычисляется программой
    column.title = QStringLiteral("Цветовая пометка");
    column.role = ColumnRole::ColorMark;
    sheet->columns.push_back(column);
    for (int i = 0; i < sheet->records.size(); ++i) {
        FundRecord &record = sheet->records[i];
        record.values.push_back(marks.at(i).isEmpty() ? QVariant() : QVariant(marks.at(i)));
        if (!record.colors.isEmpty())
            record.colors.push_back(0);
    }
}

bool FundSheetReader::readBestSheet(const io::XlsxWorkbook &workbook, FundSheet *out, QString *error)
{
    FundSheet best;
    QString firstError;
    for (const QString &sheet : workbook.sheetNames()) {
        if (workbook.isVeryHidden(sheet) || sheet == archiveSheetName())
            continue;
        FundSheet candidate;
        QString sheetError;
        if (FundSheetReader::read(workbook, sheet, &candidate, &sheetError)) {
            if (candidate.records.size() > best.records.size())
                best = std::move(candidate);
        } else if (firstError.isEmpty()) {
            firstError = sheetError;
        }
    }
    if (best.columns.isEmpty()) {
        setError(error, firstError.isEmpty() ? QStringLiteral("В файле нет листов с книгами") : firstError);
        return false;
    }
    *out = std::move(best);
    return true;
}

} // namespace bh::core
