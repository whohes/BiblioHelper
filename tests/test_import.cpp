// Этап 6: мастер импорта — чтение листа по настройкам, хранение настроек
// в рабочей копии, пометки «нет на месте».

#include "core/copy_ids.h"
#include "core/csv_import.h"
#include "core/xls_import.h"
#include "core/fund_sheet.h"
#include "core/import_layout.h"
#include "io/xlsx_workbook.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using bh::core::ColumnRole;
using bh::core::FundSheet;
using bh::core::FundSheetReader;
using bh::core::ImportLayout;
using bh::io::XlsxWorkbook;

class ImportTest : public QObject
{
    Q_OBJECT

private slots:
    void roleForHeaderSynonyms();
    void normalizeMark();
    void readWithLayout();
    void layoutSurvivesReopen();
    void marksBogdanovka();
    void marksBobrinka();
    void csvParse();
    void csvWindows1251();
    void xlsSample();

private:
    static QString dataPath(const QString &name)
    {
        return QDir(QStringLiteral(BH_TEST_DATA_DIR)).filePath(name);
    }
    static int markCount(const QVector<bh::core::MarkCount> &marks, const QString &key)
    {
        for (const auto &m : marks) {
            if (m.key == key)
                return m.count;
        }
        return 0;
    }
};

void ImportTest::roleForHeaderSynonyms()
{
    auto role = [](const char *h) { return FundSheetReader::roleForHeader(QString::fromUtf8(h)); };
    QCOMPARE(role("Заглавие"), ColumnRole::AuthorTitle);
    QCOMPARE(role("Наименование книги"), ColumnRole::AuthorTitle);
    QCOMPARE(role("Инвентарный номер"), ColumnRole::Inventory);
    QCOMPARE(role("Экз."), ColumnRole::Quantity);
    QCOMPARE(role("Количество"), ColumnRole::Quantity);
    QCOMPARE(role("Стоимость, руб."), ColumnRole::Price);
    QCOMPARE(role("ББК"), ColumnRole::Department);
    QCOMPARE(role("проверка"), ColumnRole::Mark);
    QCOMPARE(role("Отметка о наличии"), ColumnRole::Mark);
    QCOMPARE(role("Издательство"), ColumnRole::Other);
}

void ImportTest::normalizeMark()
{
    QCOMPARE(bh::core::normalizeMark(QStringLiteral(" Нет  уч. ")), QStringLiteral("нет уч"));
    QCOMPARE(bh::core::normalizeMark(QStringLiteral("не числ.")), QStringLiteral("не числ"));
    QVERIFY(bh::core::isDefaultWithdrawnMark(QStringLiteral("сп")));
    QVERIFY(bh::core::isDefaultWithdrawnMark(QStringLiteral("нет уч")));
    QVERIFY(bh::core::isDefaultWithdrawnMark(QStringLiteral("не числ")));
    QVERIFY(!bh::core::isDefaultWithdrawnMark(QStringLiteral("*")));
    QVERIFY(!bh::core::isDefaultWithdrawnMark(QStringLiteral("инв")));
}

void ImportTest::readWithLayout()
{
    XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), &error), qPrintable(error));

    FundSheet automatic;
    QVERIFY(FundSheetReader::read(wb, QStringLiteral("Лист1"), &automatic, &error));

    // Те же настройки, но столбец «Отдел» (G) — «Другое», а H — пометка.
    ImportLayout layout = ImportLayout::fromSheet(automatic);
    QCOMPARE(layout.headerRow, 4);
    QCOMPARE(layout.roles.value(2), ColumnRole::AuthorTitle);
    layout.roles.insert(7, ColumnRole::Other);
    FundSheet sheet;
    QVERIFY2(FundSheetReader::read(wb, layout, &sheet, &error), qPrintable(error));
    QCOMPARE(sheet.records.size(), automatic.records.size());
    QCOMPARE(sheet.columnIndex(ColumnRole::Department), -1);
    QCOMPARE(sheet.columns.at(sheet.columnIndex(ColumnRole::AuthorTitle)).excelColumn, 2);

    // Строка заголовков задана вручную: строка 3 — пустая шапка, книг нет.
    ImportLayout wrong = layout;
    wrong.headerRow = 3;
    FundSheet none;
    if (FundSheetReader::read(wb, wrong, &none, &error))
        QVERIFY(none.records.size() < automatic.records.size());

    // Неизвестный лист — понятная ошибка.
    ImportLayout missing = layout;
    missing.sheet = QStringLiteral("Нет такого");
    QVERIFY(!FundSheetReader::read(wb, missing, &none, &error));
    QVERIFY(error.contains(QStringLiteral("Нет такого")));
}

void ImportTest::layoutSurvivesReopen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    {
        XlsxWorkbook wb;
        QString error;
        QVERIFY2(wb.open(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), &error), qPrintable(error));
        QVERIFY(!ImportLayout::load(wb).isValid());
        FundSheet automatic;
        QVERIFY(FundSheetReader::read(wb, QStringLiteral("Лист1"), &automatic, &error));
        ImportLayout layout = ImportLayout::fromSheet(automatic);
        layout.roles.insert(7, ColumnRole::Other);  // выбор пользователя в мастере
        FundSheet sheet;
        QVERIFY(FundSheetReader::read(wb, layout, &sheet, &error));
        bh::core::CopyIds::assign(&sheet, {});
        // Настройки пишутся вместе с ID.
        QVERIFY2(bh::core::CopyIds::save(wb, sheet, &error), qPrintable(error));
        QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
    }

    XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(saved, &error), qPrintable(error));
    QVERIFY(wb.isVeryHidden(ImportLayout::sheetName()));
    const ImportLayout layout = ImportLayout::load(wb);
    QVERIFY(layout.isValid());
    QCOMPARE(layout.sheet, QStringLiteral("Лист1"));
    QCOMPARE(layout.headerRow, 4);
    QCOMPARE(layout.roles.value(7), ColumnRole::Other);
    QCOMPARE(layout.roles.value(8), ColumnRole::Mark);

    // readBestSheet читает по сохранённым настройкам, служебные листы не берёт.
    FundSheet sheet;
    QVERIFY2(FundSheetReader::readBestSheet(wb, &sheet, &error), qPrintable(error));
    QCOMPARE(sheet.sheetName, QStringLiteral("Лист1"));
    QCOMPARE(sheet.columnIndex(ColumnRole::Department), -1);
    QCOMPARE(sheet.records.size(), 3138);
}

void ImportTest::marksBogdanovka()
{
    XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), &error), qPrintable(error));
    FundSheet sheet;
    QVERIFY(FundSheetReader::readBestSheet(wb, &sheet, &error));

    const auto marks = bh::core::countMarks(sheet);
    QVERIFY(!marks.isEmpty());
    QCOMPARE(marks.first().key, QStringLiteral("*"));  // самая частая — первой
    QCOMPARE(markCount(marks, QStringLiteral("сп")), 150);
    QCOMPARE(markCount(marks, QStringLiteral("инв")), 20);

    QSet<QString> defaults;
    for (const auto &m : marks) {
        if (bh::core::isDefaultWithdrawnMark(m.key))
            defaults.insert(m.key);
    }
    const auto withdrawn = bh::core::recordsWithMarks(sheet, defaults);
    QCOMPARE(withdrawn.size(), 150);
    for (auto it = withdrawn.constBegin(); it != withdrawn.constEnd(); ++it)
        QCOMPARE(bh::core::normalizeMark(it.value()), QStringLiteral("сп"));
}

void ImportTest::marksBobrinka()
{
    XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(dataPath(QStringLiteral("fund_bobrinka.xlsx")), &error), qPrintable(error));
    FundSheet sheet;
    QVERIFY(FundSheetReader::readBestSheet(wb, &sheet, &error));
    // «проверка» (I) теперь тоже столбец пометок.
    int markColumns = 0;
    for (const auto &c : sheet.columns)
        markColumns += c.role == ColumnRole::Mark ? 1 : 0;
    QCOMPARE(markColumns, 3);  // H, «проверка» (I) и J без названия

    const auto marks = bh::core::countMarks(sheet);
    const int netUch = markCount(marks, QStringLiteral("нет уч"));
    const int neChisl = markCount(marks, QStringLiteral("не числ"));
    QVERIFY(netUch > 0);
    QVERIFY(neChisl > 0);
    const auto withdrawn = bh::core::recordsWithMarks(
        sheet, {QStringLiteral("нет уч"), QStringLiteral("не числ"), QStringLiteral("сп")});
    QVERIFY(withdrawn.size() >= std::max(netUch, neChisl));
    qInfo("Бобринка: нет уч — %d, не числ — %d, всего выбывших по пометкам — %d", netUch, neChisl,
          int(withdrawn.size()));
}

void ImportTest::csvParse()
{
    using bh::core::CsvImport;
    const auto rows = CsvImport::parse(QStringLiteral("a;\"b;c\";\"d \"\"e\"\"\"\r\n1;\"много\nстрок\";\n"),
                                       QLatin1Char(';'));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0), (QStringList{QStringLiteral("a"), QStringLiteral("b;c"), QStringLiteral("d \"e\"")}));
    QCOMPARE(rows.at(1), (QStringList{QStringLiteral("1"), QStringLiteral("много\nстрок"), QString()}));

    QCOMPARE(CsvImport::detectDelimiter(QStringLiteral("a,b,c\n1,2,3\n4,5,6\n")), QLatin1Char(','));
    QCOMPARE(CsvImport::detectDelimiter(QStringLiteral("a;b,x;c\n1;2,5;3\n")), QLatin1Char(';'));
    QCOMPARE(CsvImport::detectDelimiter(QStringLiteral("a\tb\tc\n1\t2\t3\n")), QLatin1Char('\t'));

    QCOMPARE(CsvImport::cellValue(QStringLiteral("2018")), QVariant(qlonglong(2018)));
    QCOMPARE(CsvImport::cellValue(QStringLiteral("131,6")), QVariant(131.6));
    QCOMPARE(CsvImport::cellValue(QStringLiteral("033")), QVariant(QStringLiteral("033")));
    QCOMPARE(CsvImport::cellValue(QStringLiteral("2р20к")), QVariant(QStringLiteral("2р20к")));
    QVERIFY(!CsvImport::cellValue(QStringLiteral("  ")).isValid());

    QCOMPARE(CsvImport::detectEncoding(QByteArray("\xEF\xBB\xBF" "abc")), CsvImport::Encoding::Utf8);
    QCOMPARE(CsvImport::detectEncoding(QStringLiteral("Абрамов").toUtf8()), CsvImport::Encoding::Utf8);
    QCOMPARE(CsvImport::detectEncoding(QByteArray("\xC0\xE1\xF0\xE0\xEC\xEE\xE2")),  // «Абрамов» в 1251
             CsvImport::Encoding::Windows1251);
}

void ImportTest::csvWindows1251()
{
    using bh::core::CsvImport;
    CsvImport::Table table;
    QString error;
    QVERIFY2(CsvImport::read(dataPath(QStringLiteral("fund_sample_1251.csv")), &table, &error), qPrintable(error));
    QCOMPARE(table.encoding, CsvImport::Encoding::Windows1251);
    QCOMPARE(table.delimiter, QLatin1Char(';'));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString xlsx = tmp.filePath(QStringLiteral("фонд.xlsx"));
    QVERIFY2(CsvImport::writeXlsx(table, xlsx, &error), qPrintable(error));

    XlsxWorkbook wb;
    QVERIFY2(wb.open(xlsx, &error), qPrintable(error));
    FundSheet sheet;
    QVERIFY2(FundSheetReader::readBestSheet(wb, &sheet, &error), qPrintable(error));
    QCOMPARE(sheet.headerRow, 4);
    QCOMPARE(sheet.records.size(), 296);
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    QVERIFY(bh::core::displayText(sheet.records.first().values.at(author)).startsWith(QStringLiteral("Маленький принц")));
    const int inv = sheet.columnIndex(ColumnRole::Inventory);
    QCOMPARE(sheet.records.first().values.at(inv), QVariant(qlonglong(46275)));
    QCOMPARE(markCount(bh::core::countMarks(sheet), QStringLiteral("сп")), 26);
}

void ImportTest::xlsSample()
{
    using bh::core::XlsImport;
    QVector<XlsImport::Sheet> sheets;
    QString error;
    QVERIFY2(XlsImport::read(dataPath(QStringLiteral("fund_sample.xls")), &sheets, &error), qPrintable(error));
    QVERIFY(!sheets.isEmpty());

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString xlsx = tmp.filePath(QStringLiteral("фонд.xlsx"));
    QVERIFY2(XlsImport::writeXlsx(sheets, xlsx, &error), qPrintable(error));

    XlsxWorkbook wb;
    QVERIFY2(wb.open(xlsx, &error), qPrintable(error));
    FundSheet sheet;
    QVERIFY2(FundSheetReader::readBestSheet(wb, &sheet, &error), qPrintable(error));
    QCOMPARE(sheet.headerRow, 4);
    QCOMPARE(sheet.records.size(), 296);
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    QVERIFY(bh::core::displayText(sheet.records.first().values.at(author)).startsWith(QStringLiteral("Маленький принц")));
    const int price = sheet.columnIndex(ColumnRole::Price);
    QCOMPARE(sheet.records.first().values.at(price).toDouble(), 400.0);
    QCOMPARE(markCount(bh::core::countMarks(sheet), QStringLiteral("сп")), 26);

    // Не XLS — понятная ошибка, а не падение.
    QVERIFY(!XlsImport::read(dataPath(QStringLiteral("fund_sample_1251.csv")), &sheets, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(ImportTest)
#include "test_import.moc"
