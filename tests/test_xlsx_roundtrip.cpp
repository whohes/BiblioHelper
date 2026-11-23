// Проверка главного риска из архитектуры: OpenXLSX должен менять только
// то, что мы правим, и не терять оформление пользовательского файла.
//
// Для каждого образца фонда: открыть копию, изменить одну ячейку, добавить
// скрытый служебный лист, сохранить в новый файл и сравнить части XLSX
// с исходником.

#include "core/work_copy.h"
#include "io/sheet_xml_rows.h"
#include "io/xlsx_workbook.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <private/qzipreader_p.h>

#include <cstdio>
#include <exception>

namespace {

QByteArray sha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result().toHex();
}

QMap<QString, QByteArray> zipParts(const QString &path)
{
    QMap<QString, QByteArray> parts;
    QZipReader zip(path);
    for (const auto &info : zip.fileInfoList())
        if (info.isFile)
            parts.insert(info.filePath, zip.fileData(info.filePath));
    return parts;
}

QString firstMatch(const QByteArray &xml, const QString &pattern)
{
    const auto m = QRegularExpression(pattern, QRegularExpression::DotMatchesEverythingOption)
                       .match(QString::fromUtf8(xml));
    return m.hasMatch() ? m.captured(0) : QString();
}

// OpenXLSX пересохраняет XML через pugixml: пропадает standalone="yes"
// в заголовке, меняются переводы строк, в styles.xml появляется пустой
// <numFmts>. На содержимое это не влияет, поэтому сравниваем без этого.
QByteArray normalizedXml(QByteArray xml)
{
    xml.replace("standalone=\"yes\"", "");
    QString s = QString::fromUtf8(xml);
    s.replace(QRegularExpression(QStringLiteral(">\\s+<")), QStringLiteral("><"));
    s.replace(QRegularExpression(QStringLiteral("<numFmts>\\s*</numFmts>")), QString());
    s.replace(QRegularExpression(QStringLiteral("\\s+\\?>")), QStringLiteral("?>"));
    return s.toUtf8();
}

// Ширины столбцов: <col min max width> раскрываются в «столбец → ширина»,
// потому что OpenXLSX может записать диапазон min..max по одному столбцу.
QMap<int, QString> columnWidths(const QByteArray &sheetXml)
{
    QMap<int, QString> widths;
    QRegularExpression re(QStringLiteral("<col ([^>]*)/>"));
    auto it = re.globalMatch(QString::fromUtf8(sheetXml));
    while (it.hasNext()) {
        const QString attrs = it.next().captured(1);
        auto attr = [&attrs](const QString &name) {
            const auto m = QRegularExpression(name + QStringLiteral("=\"([^\"]*)\"")).match(attrs);
            return m.hasMatch() ? m.captured(1) : QString();
        };
        const int min = attr(QStringLiteral("min")).toInt();
        const int max = attr(QStringLiteral("max")).toInt();
        QString rest = attrs;
        rest.remove(QRegularExpression(QStringLiteral("\\b(min|max)=\"[^\"]*\"")));
        for (int c = min; c <= max; ++c)
            widths.insert(c, rest.simplified());
    }
    return widths;
}

int countMatches(const QByteArray &xml, const QString &pattern)
{
    int n = 0;
    auto it = QRegularExpression(pattern).globalMatch(QString::fromUtf8(xml));
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

} // namespace

class XlsxRoundtripTest : public QObject
{
    Q_OBJECT

private slots:
    void roundtrip_data();
    void roundtrip();
    void cyrillicPath();
    void cyrillicTempFolder();
    void appendRows_data();
    void appendRows();
    void shiftReferences();
    void workCopies();

private:
    QString dataPath(const QString &name) const
    {
        return QDir(QStringLiteral(BH_TEST_DATA_DIR)).filePath(name);
    }
};

void XlsxRoundtripTest::roundtrip_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("headerRow");
    QTest::addColumn<QString>("headerTitle");
    QTest::addColumn<int>("editRow");

    // Строки заголовков и пример строки данных взяты из образцов.
    QTest::newRow("bogdanovka") << "fund_bogdanovka_2025.xlsx" << 4
                                << QStringLiteral("Автор книги и название") << 10;
    QTest::newRow("bobrinka") << "fund_bobrinka.xlsx" << 2
                              << QStringLiteral("автор книги и название") << 10;
}

void XlsxRoundtripTest::roundtrip()
{
    QFETCH(QString, file);
    QFETCH(int, headerRow);
    QFETCH(QString, headerTitle);
    QFETCH(int, editRow);

    const QString original = dataPath(file);
    QVERIFY2(QFile::exists(original), qPrintable(original));
    const QByteArray originalHash = sha256(original);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work.xlsx"));
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    QVERIFY(QFile::copy(original, work));

    // 1. Открыть рабочую копию, изменить одну ячейку, добавить служебный лист.
    QString sheet;
    QString editedBefore;
    {
        bh::io::XlsxWorkbook wb;
        QString error;
        QVERIFY2(wb.open(work, &error), qPrintable(error));
        const QStringList sheets = wb.sheetNames();
        QCOMPARE(sheets, QStringList({"Лист1", "Лист2", "Лист3"}));
        sheet = sheets.first();

        QCOMPARE(wb.cellValue(sheet, headerRow, 2).toString(), headerTitle);
        editedBefore = wb.cellValue(sheet, editRow, 2).toString();
        QVERIFY(!editedBefore.isEmpty());

        QVERIFY2(wb.setCellValue(sheet, editRow, 2, editedBefore + QStringLiteral(" (изм.)"), &error),
                 qPrintable(error));
        QVERIFY2(wb.ensureVeryHiddenSheet(QStringLiteral("_BiblioHelper"), &error), qPrintable(error));
        QVERIFY(wb.setCellValue(QStringLiteral("_BiblioHelper"), 1, 1, QStringLiteral("copy_id"), &error));
        QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
    }

    // Для разбора различий: BH_KEEP_OUTPUT=<папка> сохраняет результат.
    const QString keepDir = qEnvironmentVariable("BH_KEEP_OUTPUT");
    if (!keepDir.isEmpty()) {
        const QString kept = QDir(keepDir).filePath(QStringLiteral("saved_") + file);
        QFile::remove(kept);
        QFile::copy(saved, kept);
    }

    // 2. Оригинал и рабочая копия до сохранения не изменились.
    QCOMPARE(sha256(original), originalHash);
    QCOMPARE(sha256(work), originalHash);

    // 3. Изменение и служебный лист читаются обратно.
    {
        bh::io::XlsxWorkbook wb;
        QString error;
        QVERIFY2(wb.open(saved, &error), qPrintable(error));
        QCOMPARE(wb.sheetNames(),
                 QStringList({"Лист1", "Лист2", "Лист3", "_BiblioHelper"}));
        QCOMPARE(wb.cellValue(sheet, editRow, 2).toString(), editedBefore + QStringLiteral(" (изм.)"));
        QCOMPARE(wb.cellValue(sheet, headerRow, 2).toString(), headerTitle);
        QVERIFY(wb.isVeryHidden(QStringLiteral("_BiblioHelper")));
    }

    // 4. Части файла, которые мы не трогали, совпадают по содержимому.
    const auto before = zipParts(original);
    const auto after = zipParts(saved);
    QVERIFY(!before.isEmpty());
    const QStringList untouched = {
        QStringLiteral("xl/styles.xml"),
        QStringLiteral("xl/theme/theme1.xml"),
        QStringLiteral("xl/printerSettings/printerSettings1.bin"), // двоичный, сравнивается как есть
        QStringLiteral("xl/worksheets/sheet2.xml"),
        QStringLiteral("xl/worksheets/sheet3.xml"),
        QStringLiteral("xl/worksheets/_rels/sheet1.xml.rels"),
    };
    for (const QString &part : untouched) {
        QVERIFY2(after.contains(part), qPrintable(QStringLiteral("пропала часть ") + part));
        QVERIFY2(normalizedXml(after.value(part)) == normalizedXml(before.value(part)),
                 qPrintable(QStringLiteral("изменилась часть ") + part));
    }

    // 5. На изменённом листе сохранились автофильтр, ширины столбцов,
    //    объединённые ячейки и оформление строк.
    const QByteArray sheetBefore = before.value(QStringLiteral("xl/worksheets/sheet1.xml"));
    const QByteArray sheetAfter = after.value(QStringLiteral("xl/worksheets/sheet1.xml"));
    QCOMPARE(firstMatch(sheetAfter, QStringLiteral("<autoFilter[^>]*>")),
             firstMatch(sheetBefore, QStringLiteral("<autoFilter[^>]*>")));
    QVERIFY(!firstMatch(sheetAfter, QStringLiteral("<autoFilter[^>]*>")).isEmpty());
    QCOMPARE(columnWidths(sheetAfter), columnWidths(sheetBefore));
    QVERIFY(!columnWidths(sheetAfter).isEmpty());
    QCOMPARE(firstMatch(sheetAfter, QStringLiteral("<mergeCells.*</mergeCells>")),
             firstMatch(sheetBefore, QStringLiteral("<mergeCells.*</mergeCells>")));
    QCOMPARE(countMatches(sheetAfter, QStringLiteral("<row ")), countMatches(sheetBefore, QStringLiteral("<row ")));
    QCOMPARE(countMatches(sheetAfter, QStringLiteral(" s=\"\\d+\"")),
             countMatches(sheetBefore, QStringLiteral(" s=\"\\d+\"")));
    QCOMPARE(countMatches(sheetAfter, QStringLiteral("<f>|<f ")), countMatches(sheetBefore, QStringLiteral("<f>|<f ")));

    // 6. В книге служебный лист помечен как veryHidden, имена листов не поменялись.
    const QByteArray workbookXml = after.value(QStringLiteral("xl/workbook.xml"));
    QVERIFY(workbookXml.contains("name=\"_BiblioHelper\""));
    QVERIFY(workbookXml.contains("veryHidden"));
}

void XlsxRoundtripTest::cyrillicPath()
{
    // Имена файлов фондов на русском: путь должен работать и в Windows.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.filePath(QStringLiteral("Рабочие_файлы"));
    QVERIFY(QDir().mkpath(dir));
    const QString work = QDir(dir).filePath(QStringLiteral("Фонд Богдановской библиотеки 2025.xlsx"));
    const QString saved = QDir(dir).filePath(QStringLiteral("Фонд (сохранён).xlsx"));
    QVERIFY(QFile::copy(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), work));

    bh::io::XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(work, &error), qPrintable(error));
    QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
    QVERIFY(QFile::exists(saved));

    // Сохранённый файл открывается снова, данные на месте.
    bh::io::XlsxWorkbook reopened;
    QVERIFY2(reopened.open(saved, &error), qPrintable(error));
    QCOMPARE(reopened.sheetNames(), wb.sheetNames());
}

void XlsxRoundtripTest::workCopies()
{
    // Рабочая копия создаётся рядом с другими, исходник не трогается,
    // повторное открытие того же файла не затирает прежнюю копию.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString original = dataPath(QStringLiteral("fund_bobrinka.xlsx"));
    const QByteArray before = sha256(original);

    bh::core::WorkCopies copies(tmp.filePath(QStringLiteral("Рабочие файлы")));
    QString error;
    const QString first = copies.create(original, &error);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("fund_bobrinka (рабочая копия).xlsx"));
    const QString second = copies.create(original, &error);
    QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("fund_bobrinka (рабочая копия 2).xlsx"));

    bh::io::XlsxWorkbook wb;
    QVERIFY2(wb.open(first, &error), qPrintable(error));
    QVERIFY(wb.setCellValue(wb.sheetNames().first(), 10, 1, QStringLiteral("Проверка"), &error));
    QVERIFY2(wb.saveAs(first, &error), qPrintable(error)); // «Сохранить» поверх рабочей копии
    wb.close();

    bh::io::XlsxWorkbook reopened;
    QVERIFY2(reopened.open(first, &error), qPrintable(error));
    QCOMPARE(reopened.cellValue(reopened.sheetNames().first(), 10, 1).toString(),
             QStringLiteral("Проверка"));
    QCOMPARE(sha256(original), before);
    QVERIFY(copies.create(tmp.filePath(QStringLiteral("нет такого.xlsx")), &error).isEmpty());
}

void XlsxRoundtripTest::cyrillicTempFolder()
{
#ifndef Q_OS_WIN
    QSKIP("Проверка нужна только для Windows (имя пользователя на русском)");
#else
    // Если имя пользователя Windows на русском, временная папка тоже
    // с русскими буквами. Программа должна найти другую служебную папку.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString russianTemp = tmp.filePath(QStringLiteral("Временные"));
    QVERIFY(QDir().mkpath(russianTemp));
    const QByteArray oldTmp = qgetenv("TMP");
    const QByteArray oldTemp = qgetenv("TEMP");
    qputenv("TMP", QDir::toNativeSeparators(russianTemp).toLocal8Bit());
    qputenv("TEMP", QDir::toNativeSeparators(russianTemp).toLocal8Bit());

    const QString root = bh::io::XlsxWorkbook::stagingRoot();
    bh::io::XlsxWorkbook wb;
    QString error;
    const bool opened = wb.open(dataPath(QStringLiteral("fund_bobrinka.xlsx")), &error);
    wb.close();

    qputenv("TMP", oldTmp);
    qputenv("TEMP", oldTemp);
    QVERIFY2(!root.isEmpty(), "служебная папка не найдена");
    for (const QChar c : root)
        QVERIFY2(c.unicode() <= 0x7e, qPrintable(root));
    QVERIFY2(opened, qPrintable(error));
#endif
}

// Свой main вместо QTEST_GUILESS_MAIN: если тест аварийно завершится
// (std::terminate), вывод не потеряется в буфере и будет видна причина.
void XlsxRoundtripTest::shiftReferences()
{
    using bh::io::sheetxml::shiftReferences;
    // Итог по данным растягивается, ссылки внутри данных не трогаются.
    QCOMPARE(shiftReferences(QStringLiteral("SUM(D3:D8245)"), 8245, 2), QStringLiteral("SUM(D3:D8247)"));
    QCOMPARE(shiftReferences(QStringLiteral("$B$511"), 8245, 2), QStringLiteral("$B$511"));
    QCOMPARE(shiftReferences(QStringLiteral("A8246"), 8245, 2), QStringLiteral("A8248"));
    QCOMPARE(shiftReferences(QStringLiteral("A2:J8245"), 8245, 1), QStringLiteral("A2:J8246"));
    QCOMPARE(shiftReferences(QStringLiteral("A3144:B3146"), 3143, 1), QStringLiteral("A3145:B3147"));
    // Не ссылки: имена функций, текст в кавычках, другие листы.
    QCOMPARE(shiftReferences(QStringLiteral("LOG10(A9000)"), 8245, 1), QStringLiteral("LOG10(A9001)"));
    QCOMPARE(shiftReferences(QStringLiteral("\"A9000\"&A9000"), 8245, 1), QStringLiteral("\"A9000\"&A9001"));
    QCOMPARE(shiftReferences(QStringLiteral("Лист2!A9000+A9000"), 8245, 1), QStringLiteral("Лист2!A9000+A9001"));
    QCOMPARE(shiftReferences(QStringLiteral("A1:A100"), 8245, 1), QStringLiteral("A1:A100"));
}

void XlsxRoundtripTest::appendRows_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("lastDataRow");
    QTest::addColumn<QString>("filterBefore");
    QTest::addColumn<QString>("filterAfter");

    QTest::newRow("bogdanovka") << "fund_bogdanovka_2025.xlsx" << 3143 << "A5:J3143" << "A5:J3145";
    QTest::newRow("bobrinka") << "fund_bobrinka.xlsx" << 8245 << "A2:J8245" << "A2:J8247";
}

void XlsxRoundtripTest::appendRows()
{
    QFETCH(QString, file);
    QFETCH(int, lastDataRow);
    QFETCH(QString, filterBefore);
    QFETCH(QString, filterAfter);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work.xlsx"));
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    QVERIFY(QFile::copy(dataPath(file), work));

    bh::io::XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(work, &error), qPrintable(error));
    const QString sheet = wb.sheetNames().first();
    // Что было под данными (итоги, пояснения) — должно уехать на 2 строки вниз.
    QMap<int, QVariant> footer;
    QMap<int, QString> footerFormula;
    for (int r = lastDataRow + 1; r <= lastDataRow + 4; ++r) {
        for (int c = 1; c <= 4; ++c) {
            footer.insert(r * 100 + c, wb.cellValue(sheet, r, c));
            footerFormula.insert(r * 100 + c, wb.cellFormula(sheet, r, c));
        }
    }

    QVERIFY2(wb.appendRowsAfter(sheet, lastDataRow, 2, lastDataRow, &error), qPrintable(error));
    QVERIFY(wb.setCellValue(sheet, lastDataRow + 1, 2, QStringLiteral("Новая книга 1"), &error));
    QVERIFY(wb.setCellValue(sheet, lastDataRow + 2, 2, QStringLiteral("Новая книга 2"), &error));
    QVERIFY(wb.setCellValue(sheet, lastDataRow + 2, 6, 2026, &error));
    QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
    wb.close();

    bh::io::XlsxWorkbook out;
    QVERIFY2(out.open(saved, &error), qPrintable(error));
    QCOMPARE(out.cellValue(sheet, lastDataRow + 1, 2).toString(), QStringLiteral("Новая книга 1"));
    QCOMPARE(out.cellValue(sheet, lastDataRow + 2, 6).toLongLong(), 2026LL);
    for (auto it = footer.cbegin(); it != footer.cend(); ++it) {
        const int r = it.key() / 100, c = it.key() % 100;
        if (footerFormula.value(it.key()).isEmpty())
            QCOMPARE(out.cellValue(sheet, r + 2, c), it.value());
    }
    if (file.contains(QLatin1String("bobrinka")))
        QCOMPARE(out.cellFormula(sheet, lastDataRow + 3, 4), QStringLiteral("SUM(D3:D8247)"));

    const auto before = zipParts(dataPath(file));
    const auto after = zipParts(saved);
    const QString sheetPart = QStringLiteral("xl/worksheets/sheet1.xml");
    QVERIFY(firstMatch(before.value(sheetPart), QStringLiteral("<autoFilter ref=\"[^\"]*\"")).contains(filterBefore));
    QVERIFY2(firstMatch(after.value(sheetPart), QStringLiteral("<autoFilter ref=\"[^\"]*\"")).contains(filterAfter),
             qPrintable(firstMatch(after.value(sheetPart), QStringLiteral("<autoFilter ref=\"[^\"]*\""))));
    // Новая строка оформлена как строка-образец (высота, стиль ячеек).
    const QString templateRow = firstMatch(before.value(sheetPart),
                                           QStringLiteral("<row r=\"%1\"[^>]*>").arg(lastDataRow));
    const QString newRow = firstMatch(after.value(sheetPart),
                                      QStringLiteral("<row r=\"%1\"[^>]*>").arg(lastDataRow + 1));
    QCOMPARE(QString(newRow).replace(QString::number(lastDataRow + 1), QString()),
             QString(templateRow).replace(QString::number(lastDataRow), QString()));
    QVERIFY(after.value(QStringLiteral("xl/workbook.xml")).contains("fullCalcOnLoad=\"1\""));
    // Строки данных выше не изменились.
    QCOMPARE(firstMatch(after.value(sheetPart), QStringLiteral("<row r=\"10\".*?</row>")),
             firstMatch(normalizedXml(before.value(sheetPart)), QStringLiteral("<row r=\"10\".*?</row>")));
}

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::set_terminate([] {
        std::fprintf(stderr, "std::terminate: ");
        try {
            if (auto e = std::current_exception())
                std::rethrow_exception(e);
            std::fprintf(stderr, "без исключения\n");
        } catch (const std::exception &e) {
            std::fprintf(stderr, "%s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "неизвестное исключение\n");
        }
        std::fflush(stderr);
        std::abort();
    });

    QCoreApplication app(argc, argv);
    XlsxRoundtripTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "test_xlsx_roundtrip.moc"
