#include "import_layout.h"

#include "io/xlsx_workbook.h"

#include <QRegularExpression>

#include <algorithm>
#include <iterator>

namespace bh::core {

namespace {

struct RoleInfo
{
    ColumnRole role;
    const char *key;
    const char *title;
};

// Порядок — как в списке мастера.
const RoleInfo kRoles[] = {
    {ColumnRole::AuthorTitle, "author_title", "Автор и название"},
    {ColumnRole::Inventory, "inventory", "Инв. №"},
    {ColumnRole::Number, "number", "№ п/п"},
    {ColumnRole::Quantity, "quantity", "Количество"},
    {ColumnRole::Price, "price", "Цена"},
    {ColumnRole::Year, "year", "Год выпуска"},
    {ColumnRole::Department, "department", "Отдел / шифр"},
    {ColumnRole::Mark, "mark", "Пометка о наличии"},
    {ColumnRole::Status, "status", "Статус (пишет программа)"},
    {ColumnRole::Other, "other", "Другое (показывать как есть)"},
};

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

QVector<ColumnRole> importableRoles()
{
    QVector<ColumnRole> roles;
    for (const RoleInfo &info : kRoles) {
        if (info.role != ColumnRole::Status)
            roles.push_back(info.role);
    }
    return roles;
}

QString roleTitle(ColumnRole role)
{
    for (const RoleInfo &info : kRoles) {
        if (info.role == role)
            return QString::fromUtf8(info.title);
    }
    return QString::fromUtf8(kRoles[std::size(kRoles) - 1].title);
}

QString roleKey(ColumnRole role)
{
    for (const RoleInfo &info : kRoles) {
        if (info.role == role)
            return QString::fromLatin1(info.key);
    }
    return QStringLiteral("other");
}

ColumnRole roleFromKey(const QString &key, bool *ok)
{
    for (const RoleInfo &info : kRoles) {
        if (key == QLatin1String(info.key)) {
            if (ok)
                *ok = true;
            return info.role;
        }
    }
    if (ok)
        *ok = false;
    return ColumnRole::Other;
}

ImportLayout ImportLayout::fromSheet(const FundSheet &sheet)
{
    ImportLayout layout;
    layout.sheet = sheet.sheetName;
    layout.headerRow = sheet.headerRow;
    for (const FundColumn &column : sheet.columns) {
        if (column.excelColumn > 0 && column.inFile)
            layout.roles.insert(column.excelColumn, column.role);
    }
    return layout;
}

QString ImportLayout::sheetName()
{
    return QStringLiteral("_BiblioHelperImport");
}

ImportLayout ImportLayout::load(const io::XlsxWorkbook &workbook)
{
    ImportLayout layout;
    if (!workbook.sheetNames().contains(sheetName()))
        return layout;
    const io::SheetSize size = workbook.sheetSize(sheetName());
    const auto rows = workbook.readRows(sheetName(), 1, std::max(size.rows, 1), 3);
    for (const auto &row : rows) {
        const QString key = row.values.value(0).toString();
        const QString value = row.values.value(1).toString();
        if (key == QLatin1String("sheet")) {
            layout.sheet = value;
        } else if (key == QLatin1String("header_row")) {
            layout.headerRow = value.toInt();
        } else if (key == QLatin1String("column")) {
            bool ok = false;
            const int column = value.toInt();
            const ColumnRole role = roleFromKey(row.values.value(2).toString(), &ok);
            if (ok && column > 0)
                layout.roles.insert(column, role);
        }
    }
    if (!layout.isValid())
        return {};
    return layout;
}

bool ImportLayout::save(io::XlsxWorkbook &workbook, QString *error) const
{
    if (!isValid()) {
        setError(error, QStringLiteral("Не заданы лист и строка заголовков"));
        return false;
    }
    if (!workbook.ensureVeryHiddenSheet(sheetName(), error))
        return false;
    QVector<QStringList> rows;
    rows.push_back({QStringLiteral("version"), QStringLiteral("1")});
    rows.push_back({QStringLiteral("sheet"), sheet});
    rows.push_back({QStringLiteral("header_row"), QString::number(headerRow)});
    QList<int> columns = roles.keys();
    std::sort(columns.begin(), columns.end());
    for (int column : qAsConst(columns))
        rows.push_back({QStringLiteral("column"), QString::number(column), roleKey(roles.value(column))});
    return workbook.replaceSheetText(sheetName(), rows, error);
}

QString normalizeMark(const QString &text)
{
    QString s = text.toLower();
    s.replace(QChar(0x0451), QChar(0x0435));  // ё → е
    s.replace(QLatin1Char('.'), QLatin1Char(' '));
    return s.simplified();
}

bool isDefaultWithdrawnMark(const QString &key)
{
    return key == QStringLiteral("сп") || key == QStringLiteral("нет уч") || key == QStringLiteral("не числ");
}

QString markWithdrawalReason()
{
    return QStringLiteral("по пометке в исходном файле");
}

QVector<MarkCount> countMarks(const FundSheet &sheet)
{
    QVector<int> markColumns;
    for (int c = 0; c < sheet.columns.size(); ++c) {
        if (sheet.columns.at(c).role == ColumnRole::Mark)
            markColumns.push_back(c);
    }
    QHash<QString, int> index;                 // ключ → позиция в result
    QVector<QHash<QString, int>> spellings;    // как записана пометка → сколько раз
    QVector<MarkCount> result;
    for (const FundRecord &record : sheet.records) {
        QSet<QString> seen;  // одна строка считается один раз, даже если пометка в двух столбцах
        for (int c : qAsConst(markColumns)) {
            const QString text = displayText(record.values.value(c)).trimmed();
            const QString key = normalizeMark(text);
            if (key.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);
            auto it = index.find(key);
            if (it == index.end()) {
                it = index.insert(key, result.size());
                MarkCount mark;
                mark.key = key;
                result.push_back(mark);
                spellings.push_back({});
            }
            ++result[*it].count;
            ++spellings[*it][text];
        }
    }
    for (int i = 0; i < result.size(); ++i) {
        int best = 0;
        for (auto it = spellings.at(i).constBegin(); it != spellings.at(i).constEnd(); ++it) {
            if (it.value() > best || (it.value() == best && it.key() < result[i].text)) {
                best = it.value();
                result[i].text = it.key();
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const MarkCount &a, const MarkCount &b) {
        return a.count != b.count ? a.count > b.count : a.key < b.key;
    });
    return result;
}

QHash<int, QString> recordsWithMarks(const FundSheet &sheet, const QSet<QString> &keys)
{
    QHash<int, QString> result;
    if (keys.isEmpty())
        return result;
    QVector<int> markColumns;
    for (int c = 0; c < sheet.columns.size(); ++c) {
        if (sheet.columns.at(c).role == ColumnRole::Mark)
            markColumns.push_back(c);
    }
    for (int r = 0; r < sheet.records.size(); ++r) {
        for (int c : qAsConst(markColumns)) {
            const QString text = displayText(sheet.records.at(r).values.value(c)).trimmed();
            if (keys.contains(normalizeMark(text))) {
                result.insert(r, text);
                break;
            }
        }
    }
    return result;
}

} // namespace bh::core
