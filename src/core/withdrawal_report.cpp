#include "withdrawal_report.h"

#include <QTemporaryDir>
#include <QSet>

#include "io/xlsx_workbook.h"

#include "xlsxdocument.h"
#include "xlsxformat.h"

namespace bh::core {

namespace {

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
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

} // namespace

QStringList WithdrawalReport::columnTitles()
{
    return {QStringLiteral("№"),
            QStringLiteral("Автор и название"),
            QStringLiteral("Инв. №"),
            QStringLiteral("Цена, руб."),
            QStringLiteral("Дата выбытия"),
            QStringLiteral("Причина выбытия"),
            QStringLiteral("Примечание")};
}

bool WithdrawalReport::write(const QString &path, const FundSheet &sheet, QString *error) const
{
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    const int inventory = sheet.columnIndex(ColumnRole::Inventory);
    const int price = sheet.columnIndex(ColumnRole::Price);
    const int quantity = sheet.columnIndex(ColumnRole::Quantity);
    const int date = sheet.columnIndex(ColumnRole::WithdrawDate);
    const int reason = sheet.columnIndex(ColumnRole::WithdrawReason);
    const int note = sheet.columnIndex(ColumnRole::WithdrawNote);
    auto value = [](const FundRecord &record, int column) {
        return column >= 0 && column < record.values.size() ? record.values.at(column) : QVariant();
    };

    QXlsx::Document doc;
    doc.renameSheet(doc.currentWorksheet()->sheetName(), QStringLiteral("Выбывшие"));
    const QStringList titles = columnTitles();
    const int lastColumn = titles.size();

    QXlsx::Format title;
    title.setFontName(QStringLiteral("Times New Roman"));
    title.setFontSize(14);
    title.setFontBold(true);
    doc.write(1, 1, QStringLiteral("Отчёт о выбывших экземплярах"), title);
    QXlsx::Format plain;
    plain.setFontName(QStringLiteral("Times New Roman"));
    plain.setFontSize(12);
    const QString fund = fundName.isEmpty() ? QString() : QStringLiteral("Фонд: %1. ").arg(fundName);
    doc.write(2, 1, fund + QStringLiteral("Сформирован %1.").arg(createdOn.toString(QStringLiteral("dd.MM.yyyy"))),
              plain);

    const int headerRow = 4;
    QXlsx::Format header = cellFormat(true);
    header.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    header.setTextWrap(true);
    header.setPatternBackgroundColor(QColor(0xE8, 0xEC, 0xF2));
    for (int c = 0; c < lastColumn; ++c)
        doc.write(headerRow, c + 1, titles.at(c), header);

    QXlsx::Format text = cellFormat();
    text.setTextWrap(true);
    QXlsx::Format centered = cellFormat();
    centered.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    QXlsx::Format money = cellFormat();
    money.setNumberFormat(QStringLiteral("#,##0.00"));
    QXlsx::Format day = centered;
    day.setNumberFormat(QStringLiteral("dd.mm.yyyy"));

    int row = headerRow;
    int count = 0;
    double sum = 0;
    QSet<int> seen;
    for (int index : records) {
        if (index < 0 || index >= sheet.records.size() || !sheet.isWithdrawn(index) || seen.contains(index))
            continue;
        seen.insert(index);
        const FundRecord &record = sheet.records.at(index);
        ++row;
        ++count;
        doc.write(row, 1, count, centered);
        doc.write(row, 2, displayText(value(record, author)), text);
        // Инв. номер — как в фонде: число остаётся числом, «б/н» — текстом.
        const QVariant inv = value(record, inventory);
        doc.write(row, 3, inv.isValid() ? inv : QVariant(QString()), centered);
        double rubles = 0;
        if (priceInRubles(value(record, price), &rubles)) {
            double copies = value(record, quantity).toDouble();
            if (copies <= 0)
                copies = 1;
            sum += rubles * copies;
            doc.write(row, 4, rubles, money);
        } else {
            // «б/ц», «нет» — как в фонде.
            doc.write(row, 4, displayText(value(record, price)), centered);
        }
        doc.write(row, 5, value(record, date).toDate(), day);
        doc.write(row, 6, displayText(value(record, reason)), text);
        doc.write(row, 7, displayText(value(record, note)), text);
    }

    ++row;
    QXlsx::Format total = cellFormat(true);
    doc.write(row, 1, QString(), total);
    doc.write(row, 2, QStringLiteral("Итого: %1 экз.").arg(count), total);
    doc.write(row, 3, QString(), total);
    QXlsx::Format totalMoney = total;
    totalMoney.setNumberFormat(QStringLiteral("#,##0.00"));
    doc.write(row, 4, sum, totalMoney);
    for (int c = 5; c <= lastColumn; ++c)
        doc.write(row, c, QString(), total);

    const double widths[] = {6, 60, 12, 12, 14, 26, 30};
    for (int c = 0; c < lastColumn; ++c)
        doc.setColumnWidth(c + 1, widths[c]);

    // QXlsx не умеет печать по ширине, закрепление строк и автофильтр:
    // сохраняем во временный файл и дописываем это через OpenXLSX.
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        setError(error, QStringLiteral("Не удалось создать временную папку"));
        return false;
    }
    const QString draft = tmp.filePath(QStringLiteral("report.xlsx"));
    if (!doc.saveAs(draft)) {
        setError(error, QStringLiteral("Не удалось записать отчёт"));
        return false;
    }
    io::XlsxWorkbook workbook;
    const QString filterRef = QStringLiteral("A%1:%2%3")
                                  .arg(headerRow)
                                  .arg(QChar('A' + lastColumn - 1))
                                  .arg(std::max(headerRow + 1, row - 1));
    if (!workbook.open(draft, error) || !workbook.setReportLayout(QStringLiteral("Выбывшие"), headerRow, filterRef, error))
        return false;
    // Сохранение — через папку с латинским путём и QSaveFile, как у рабочих копий.
    if (!workbook.saveAs(path, error)) {
        if (error)
            *error = QStringLiteral("%1\n\nЕсли файл с таким именем открыт в Excel, закройте его.").arg(*error);
        return false;
    }
    return true;
}

} // namespace bh::core
