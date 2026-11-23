// Проверка главного риска из архитектуры: OpenXLSX должен менять только
// то, что мы правим, и не терять оформление пользовательского файла.
//
// Для каждого образца фонда: открыть копию, изменить одну ячейку, добавить
// скрытый служебный лист, сохранить в новый файл и сравнить части XLSX
// с исходником.

#include "core/work_copy.h"
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
