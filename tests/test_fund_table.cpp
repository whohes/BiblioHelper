// Чтение листа фонда, поиск, фильтры и сортировка на образцах фондов.

#include "core/fund_sheet.h"
#include "io/xlsx_workbook.h"
#include "models/fund_filter_proxy.h"
#include "models/fund_table_model.h"

#include <QDir>
#include <QElapsedTimer>
#include <QtTest>

using bh::core::ColumnRole;

class FundTableTest : public QObject
{
    Q_OBJECT

private slots:
    void readSheet_data();
    void readSheet();
    void searchAndFilters();
    void sorting();
    void normalize();

private:
    static QString dataPath(const QString &name)
    {
        return QDir(QStringLiteral(BH_TEST_DATA_DIR)).filePath(name);
    }

    static bh::core::FundSheet load(const QString &file)
    {
        bh::io::XlsxWorkbook wb;
        QString error;
        bh::core::FundSheet sheet;
        if (!wb.open(dataPath(file), &error))
            qFatal("open: %s", qPrintable(error));
        if (!bh::core::FundSheetReader::readBestSheet(wb, &sheet, &error))
            qFatal("read: %s", qPrintable(error));
        return sheet;
    }

    static int countVisible(const bh::models::FundFilterProxy &proxy) { return proxy.rowCount(); }
};

void FundTableTest::readSheet_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("headerRow");
    QTest::addColumn<int>("records");
    QTest::addColumn<int>("firstExcelRow");
    QTest::addColumn<QString>("firstTitle");

    // Ожидаемые числа посчитаны независимо (openpyxl) по тем же правилам.
    QTest::newRow("bogdanovka") << "fund_bogdanovka_2025.xlsx" << 4 << 3138 << 5
                                << QStringLiteral("Маленький принц Антология семейного чтения");
    QTest::newRow("bobrinka") << "fund_bobrinka.xlsx" << 2 << 8243 << 3
                              << QStringLiteral("Рекемчук А. Избранные произведения Т. 2");
}

void FundTableTest::readSheet()
{
    QFETCH(QString, file);
    QFETCH(int, headerRow);
    QFETCH(int, records);
    QFETCH(int, firstExcelRow);
    QFETCH(QString, firstTitle);

    QElapsedTimer timer;
    timer.start();
    const bh::core::FundSheet sheet = load(file);
    qInfo("%s: %lld мс", qPrintable(file), timer.elapsed());

    QCOMPARE(sheet.sheetName, QStringLiteral("Лист1"));
    QCOMPARE(sheet.headerRow, headerRow);
    QCOMPARE(sheet.records.size(), records);
    QCOMPARE(sheet.records.first().excelRow, firstExcelRow);

    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    QVERIFY(author >= 0);
    QCOMPARE(bh::core::displayText(sheet.records.first().values.at(author)), firstTitle);
    QVERIFY(sheet.columnIndex(ColumnRole::Inventory) >= 0);
    QVERIFY(sheet.columnIndex(ColumnRole::Year) >= 0);
    QVERIFY(sheet.columnIndex(ColumnRole::Price) >= 0);
    QVERIFY(sheet.columnIndex(ColumnRole::Mark) >= 0);  // столбец H без заголовка
    QCOMPARE(sheet.columns.at(author).title.at(0), QChar(0x0410)); // «А» заглавная

    // Итоговые строки внизу не попали в записи.
    for (const auto &record : sheet.records)
        QVERIFY(!bh::core::displayText(record.values.at(author)).startsWith(QStringLiteral("По документам")));
}

void FundTableTest::searchAndFilters()
{
    bh::models::FundTableModel model;
    model.setSheet(load(QStringLiteral("fund_bogdanovka_2025.xlsx")));
    bh::models::FundFilterProxy proxy;
    proxy.setSourceModel(&model);
    const int total = model.rowCount();
    QCOMPARE(countVisible(proxy), total);

    // Поиск без учёта регистра, лишних пробелов и ё/е; все слова сразу.
    proxy.setSearchText(QStringLiteral("  абгарян   ЯБЛОКА "));
    QCOMPARE(countVisible(proxy), 1);
    QCOMPARE(proxy.index(0, model.sheet().columnIndex(ColumnRole::Inventory)).data().toString(),
             QStringLiteral("54803"));
    proxy.setSearchText(QStringLiteral("54803"));
    QVERIFY(countVisible(proxy) >= 1);
    proxy.setSearchText(QString());
    QCOMPARE(countVisible(proxy), total);

    // Фильтр по значениям столбца «Год выпуска».
    const int year = model.sheet().columnIndex(ColumnRole::Year);
    const auto values = proxy.columnValues(year);
    QVERIFY(values.size() > 10);
    int expected2018 = 0;
    for (const auto &v : values) {
        if (v.first == QLatin1String("2018"))
            expected2018 = v.second;
    }
    QVERIFY(expected2018 > 0);
    proxy.setColumnFilter(year, {QStringLiteral("2018")});
    QCOMPARE(countVisible(proxy), expected2018);
    QVERIFY(proxy.hasAnyFilter());

    // Значения другого столбца считаются среди уже отфильтрованных строк.
    int sum = 0;
    for (const auto &v : proxy.columnValues(model.sheet().columnIndex(ColumnRole::Department)))
        sum += v.second;
    QCOMPARE(sum, expected2018);

    // Годы в списке идут по возрастанию как числа, «(пусто)» в конце
    // (в образце есть и опечатки вроде 0,95 и 11990 — они тоже числа).
    const QLocale ru(QLocale::Russian);
    double prev = -1e18;
    for (const auto &v : values) {
        if (v.first == bh::models::FundFilterProxy::emptyValueLabel()) {
            QCOMPARE(v, values.last());
            continue;
        }
        bool ok = false;
        const double d = ru.toDouble(v.first, &ok);
        QVERIFY2(ok, qPrintable(v.first));
        QVERIFY(d > prev);
        prev = d;
    }

    proxy.clearAllFilters();
    QCOMPARE(countVisible(proxy), total);
    QVERIFY(!proxy.hasAnyFilter());
}

void FundTableTest::sorting()
{
    bh::models::FundTableModel model;
    model.setSheet(load(QStringLiteral("fund_bobrinka.xlsx")));
    bh::models::FundFilterProxy proxy;
    proxy.setSourceModel(&model);

    // Инвентарные номера: числа по величине, текст («б/н»…) после чисел.
    const int inv = model.sheet().columnIndex(ColumnRole::Inventory);
    proxy.sort(inv, Qt::AscendingOrder);
    double previous = -1;
    bool textStarted = false;
    for (int r = 0; r < proxy.rowCount(); ++r) {
        const QVariant key = proxy.index(r, inv).data(bh::models::FundTableModel::SortRole);
        if (key.type() == QVariant::Double) {
            QVERIFY2(!textStarted, "число после текста");
            QVERIFY(key.toDouble() >= previous);
            previous = key.toDouble();
        } else {
            textStarted = true;
        }
    }

    // Название: по алфавиту без учёта регистра.
    const int author = model.sheet().columnIndex(ColumnRole::AuthorTitle);
    proxy.sort(author, Qt::AscendingOrder);
    const QString first = proxy.index(0, author).data().toString();
    const QString last = proxy.index(proxy.rowCount() - 1, author).data().toString();
    QVERIFY2(QString::localeAwareCompare(first.toLower(), last.toLower()) <= 0 || first.at(0).isDigit(),
             qPrintable(first + " / " + last));
}

void FundTableTest::normalize()
{
    QCOMPARE(bh::core::normalizeForSearch(QStringLiteral("  Ёлка   ЗЕЛЁНАЯ ")), QStringLiteral("елка зеленая"));
    QCOMPARE(bh::core::displayText(QVariant(2007.0)), QStringLiteral("2007"));
    QCOMPARE(bh::core::displayText(QVariant(2.4)), QStringLiteral("2,4"));
    QCOMPARE(bh::core::FundSheetReader::roleForHeader(QStringLiteral("Инв. №")), ColumnRole::Inventory);
    QCOMPARE(bh::core::FundSheetReader::roleForHeader(QStringLiteral("№ п/п")), ColumnRole::Number);
    QCOMPARE(bh::core::FundSheetReader::roleForHeader(QStringLiteral("кол-во шт.")), ColumnRole::Quantity);
}

QTEST_GUILESS_MAIN(FundTableTest)
#include "test_fund_table.moc"
