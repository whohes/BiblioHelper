#include "fund_export.h"

#include "io/xlsx_workbook.h"

#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>

#include "xlsxdocument.h"
#include "xlsxformat.h"

#include <algorithm>

namespace bh::core {

namespace {

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
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

QXlsx::Format cellFormat(bool bold = false)
{
    QXlsx::Format f;
    f.setFontName(QStringLiteral("Times New Roman"));
    f.setFontSize(12);
    f.setFontBold(bold);
    f.setBorderStyle(QXlsx::Format::BorderThin);
    f.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    return f;
}

double columnWidth(ColumnRole role)
{
    switch (role) {
    case ColumnRole::AuthorTitle: return 60;
    case ColumnRole::Number: return 8;
    case ColumnRole::Quantity: return 9;
    case ColumnRole::Inventory:
    case ColumnRole::Price:
    case ColumnRole::Year:
    case ColumnRole::Department: return 12;
    case ColumnRole::Mark: return 9;
    case ColumnRole::Status:
    case ColumnRole::WithdrawReason:
    case ColumnRole::WithdrawNote: return 28;
    case ColumnRole::WithdrawDate: return 14;
    default: return 16;
    }
}

QString csvField(const QString &text)
{
    if (text.contains(QLatin1Char(';')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n'))
        || text.contains(QLatin1Char('\r'))) {
        QString quoted = text;
        quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    return text;
}

} // namespace

QVector<int> FundExport::defaultColumns(const FundSheet &sheet, bool withdrawn)
{
    QVector<int> columns;
    for (int c = 0; c < sheet.columns.size(); ++c) {
        const ColumnRole role = sheet.columns.at(c).role;
        const bool withdrawal = role == ColumnRole::WithdrawDate || role == ColumnRole::WithdrawReason
            || role == ColumnRole::WithdrawNote;
        if (role == ColumnRole::Status || (withdrawal && !withdrawn))
            continue;
        columns.push_back(c);
    }
    return columns;
}

bool FundExport::write(const QString &path, Format format, const FundSheet &sheet, QString *error) const
{
    if (columns.isEmpty()) {
        setError(error, QStringLiteral("Не выбрано ни одного столбца"));
        return false;
    }
    return format == Format::Csv ? writeCsv(path, sheet, error) : writeXlsx(path, sheet, error);
}

bool FundExport::writeCsv(const QString &path, const FundSheet &sheet, QString *error) const
{
    QString text;
    QStringList line;
    for (int c : columns)
        line << csvField(sheet.columns.at(c).title);
    text += line.join(QLatin1Char(';')) + QStringLiteral("\r\n");
    for (int r : records) {
        if (r < 0 || r >= sheet.records.size())
            continue;
        line.clear();
        for (int c : columns)
            line << csvField(displayText(sheet.records.at(r).values.value(c)));
        text += line.join(QLatin1Char(';')) + QStringLiteral("\r\n");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Не удалось создать файл: %1").arg(file.errorString()));
        return false;
    }
    file.write("\xEF\xBB\xBF");
    file.write(text.toUtf8());
    if (!file.commit()) {
        setError(error, QStringLiteral("Не удалось записать файл: %1\n\nЕсли он открыт в Excel, закройте его.")
                            .arg(file.errorString()));
        return false;
    }
    return true;
}

bool FundExport::writeXlsx(const QString &path, const FundSheet &sheet, QString *error) const
{
    QXlsx::Document doc;
    const QString sheetName = QStringLiteral("Экспорт");
    doc.addSheet(sheetName);
    const int lastColumn = columns.size();

    QXlsx::Format titleFormat;
    titleFormat.setFontName(QStringLiteral("Times New Roman"));
    titleFormat.setFontSize(14);
    titleFormat.setFontBold(true);
    doc.write(1, 1, title.isEmpty() ? QStringLiteral("Книжный фонд") : title, titleFormat);
    QXlsx::Format plain;
    plain.setFontName(QStringLiteral("Times New Roman"));
    plain.setFontSize(12);
    const QString fund = fundName.isEmpty() ? QString() : QStringLiteral("Фонд: %1. ").arg(fundName);
    doc.write(2, 1, fund + QStringLiteral("Выгружено %1.").arg(createdOn.toString(QStringLiteral("dd.MM.yyyy"))),
              plain);

    const int headerRow = 4;
    QXlsx::Format header = cellFormat(true);
    header.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    header.setTextWrap(true);
    header.setPatternBackgroundColor(QColor(0xE8, 0xEC, 0xF2));
    for (int i = 0; i < lastColumn; ++i) {
        doc.write(headerRow, i + 1, sheet.columns.at(columns.at(i)).title, header);
        doc.setColumnWidth(i + 1, columnWidth(sheet.columns.at(columns.at(i)).role));
    }

    QXlsx::Format text = cellFormat();
    text.setTextWrap(true);
    QXlsx::Format centered = cellFormat();
    centered.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    QXlsx::Format money = cellFormat();
    money.setNumberFormat(QStringLiteral("#,##0.00"));
    QXlsx::Format day = centered;
    day.setNumberFormat(QStringLiteral("dd.mm.yyyy"));

    const int priceColumn = sheet.columnIndex(ColumnRole::Price);
    const int quantityColumn = sheet.columnIndex(ColumnRole::Quantity);
    const int pricePosition = columns.indexOf(priceColumn);
    int row = headerRow;
    int count = 0;
    double sum = 0;
    for (int r : records) {
        if (r < 0 || r >= sheet.records.size())
            continue;
        const FundRecord &record = sheet.records.at(r);
        ++row;
        ++count;
        double copies = quantityColumn >= 0 ? record.values.value(quantityColumn).toDouble() : 1;
        if (copies <= 0)
            copies = 1;
        for (int i = 0; i < lastColumn; ++i) {
            const int c = columns.at(i);
            const ColumnRole role = sheet.columns.at(c).role;
            const QVariant value = record.values.value(c);
            if (value.type() == QVariant::Date) {
                doc.write(row, i + 1, value.toDate(), day);
            } else if (role == ColumnRole::AuthorTitle || role == ColumnRole::WithdrawReason
                       || role == ColumnRole::WithdrawNote || role == ColumnRole::Status) {
                doc.write(row, i + 1, displayText(value), text);
            } else if (role == ColumnRole::Price) {
                // Цена как в фонде: число остаётся числом, «2р20к» и «б/ц» — текстом.
                double rubles = 0;
                if (priceInRubles(value, &rubles))
                    sum += rubles * copies;
                if (value.type() == QVariant::Double || value.type() == QVariant::LongLong
                    || value.type() == QVariant::Int)
                    doc.write(row, i + 1, value, money);
                else
                    doc.write(row, i + 1, displayText(value), centered);
            } else if (value.type() == QVariant::String) {
                doc.write(row, i + 1, displayText(value), centered);
            } else if (value.isValid()) {
                doc.write(row, i + 1, value, centered);
            } else {
                doc.write(row, i + 1, QString(), centered);
            }
        }
    }

    // Итог: число экземпляров и сумма (сумма — если выгружается цена).
    ++row;
    QXlsx::Format total = cellFormat(true);
    for (int i = 0; i < lastColumn; ++i)
        doc.write(row, i + 1, QString(), total);
    const int authorPosition = columns.indexOf(sheet.columnIndex(ColumnRole::AuthorTitle));
    doc.write(row, (authorPosition >= 0 ? authorPosition : 0) + 1, QStringLiteral("Итого: %1 экз.").arg(count), total);
    if (pricePosition >= 0 && pricePosition != authorPosition) {
        QXlsx::Format totalMoney = total;
        totalMoney.setNumberFormat(QStringLiteral("#,##0.00"));
        doc.write(row, pricePosition + 1, sum, totalMoney);
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        setError(error, QStringLiteral("Не удалось создать временную папку"));
        return false;
    }
    const QString draft = tmp.filePath(QStringLiteral("export.xlsx"));
    if (!doc.saveAs(draft)) {
        setError(error, QStringLiteral("Не удалось записать файл экспорта"));
        return false;
    }
    io::XlsxWorkbook workbook;
    const QString filterRef = QStringLiteral("A%1:%2%3")
                                  .arg(headerRow)
                                  .arg(columnLetter(lastColumn))
                                  .arg(std::max(headerRow + 1, row - 1));
    if (!workbook.open(draft, error) || !workbook.setReportLayout(sheetName, headerRow, filterRef, error))
        return false;
    if (!workbook.saveAs(path, error)) {
        if (error)
            *error = QStringLiteral("%1\n\nЕсли файл с таким именем открыт в Excel, закройте его.").arg(*error);
        return false;
    }
    return true;
}

} // namespace bh::core
