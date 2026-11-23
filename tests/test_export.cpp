// Этап 7: экспорт и прошлые версии (резервные копии).

#include "core/action_log.h"
#include "core/fund_export.h"
#include "core/fund_sheet.h"
#include "core/work_copy.h"
#include "io/xlsx_workbook.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using bh::core::ActionLog;
using bh::core::ColumnRole;
using bh::core::FundExport;
using bh::core::FundSheet;
using bh::core::FundSheetReader;
using bh::core::WorkCopies;
using bh::io::XlsxWorkbook;

class ExportTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultColumns();
    void xlsx();
    void csv();
    void backupsListAndPrune();
    void actionLog();

private:
    static QString dataPath(const QString &name)
    {
        return QDir(QStringLiteral(BH_TEST_DATA_DIR)).filePath(name);
    }
    FundSheet m_sheet;
};

void ExportTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    XlsxWorkbook wb;
    QString error;
    QVERIFY2(wb.open(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), &error), qPrintable(error));
    QVERIFY2(FundSheetReader::readBestSheet(wb, &m_sheet, &error), qPrintable(error));
    // Два выбывших для проверки столбцов выбытия.
    const int date = m_sheet.columnIndex(ColumnRole::WithdrawDate);
    const int reason = m_sheet.columnIndex(ColumnRole::WithdrawReason);
    for (int r : {0, 1}) {
        m_sheet.records[r].values[date] = QDate(2026, 11, 23);
        m_sheet.records[r].values[reason] = QStringLiteral("ветхость");
    }
}

void ExportTest::defaultColumns()
{
    const auto active = FundExport::defaultColumns(m_sheet, false);
    QVERIFY(active.contains(m_sheet.columnIndex(ColumnRole::AuthorTitle)));
    QVERIFY(!active.contains(m_sheet.columnIndex(ColumnRole::Status)));
    QVERIFY(!active.contains(m_sheet.columnIndex(ColumnRole::WithdrawDate)));
    const auto withdrawn = FundExport::defaultColumns(m_sheet, true);
    QVERIFY(withdrawn.contains(m_sheet.columnIndex(ColumnRole::WithdrawDate)));
    QVERIFY(withdrawn.contains(m_sheet.columnIndex(ColumnRole::WithdrawReason)));
}

void ExportTest::xlsx()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("экспорт.xlsx"));
    FundExport exporter;
    exporter.title = QStringLiteral("Книжный фонд");
    exporter.fundName = QStringLiteral("Богдановка");
    exporter.createdOn = QDate(2026, 11, 23);
    const int author = m_sheet.columnIndex(ColumnRole::AuthorTitle);
    const int inv = m_sheet.columnIndex(ColumnRole::Inventory);
    const int price = m_sheet.columnIndex(ColumnRole::Price);
    const int date = m_sheet.columnIndex(ColumnRole::WithdrawDate);
    exporter.columns = {author, inv, price, date};
    exporter.records = {5, 0, 1};  // порядок как на экране
    QString error;
    QVERIFY2(exporter.write(path, FundExport::Format::Xlsx, m_sheet, &error), qPrintable(error));

    XlsxWorkbook wb;
    QVERIFY2(wb.open(path, &error), qPrintable(error));
    QVERIFY(wb.sheetNames().contains(QStringLiteral("Экспорт")));
    const QString s = QStringLiteral("Экспорт");
    QCOMPARE(wb.cellValue(s, 1, 1).toString(), QStringLiteral("Книжный фонд"));
    QCOMPARE(wb.cellValue(s, 4, 1).toString(), m_sheet.columns.at(author).title);
    QCOMPARE(wb.cellValue(s, 4, 4).toString(), QStringLiteral("Дата выбытия"));
    QCOMPARE(wb.cellValue(s, 5, 1).toString(), bh::core::displayText(m_sheet.records.at(5).values.at(author)));
    QCOMPARE(wb.cellValue(s, 6, 1).toString(), bh::core::displayText(m_sheet.records.at(0).values.at(author)));
    QCOMPARE(wb.cellValue(s, 5, 2), m_sheet.records.at(5).values.at(inv));
    QVERIFY(wb.cellValue(s, 6, 4).isValid());  // дата выбытия у выбывшего
    QCOMPARE(wb.cellValue(s, 8, 1).toString(), QStringLiteral("Итого: 3 экз."));
    double expected = 0;
    for (int r : exporter.records) {
        double rub = 0;
        if (bh::core::priceInRubles(m_sheet.records.at(r).values.at(price), &rub))
            expected += rub;
    }
    QCOMPARE(wb.cellValue(s, 8, 3).toDouble(), expected);
}

void ExportTest::csv()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("экспорт.csv"));
    FundExport exporter;
    const int author = m_sheet.columnIndex(ColumnRole::AuthorTitle);
    const int price = m_sheet.columnIndex(ColumnRole::Price);
    exporter.columns = {author, price};
    exporter.records = {0, 1, 2};
    QString error;
    QVERIFY2(exporter.write(path, FundExport::Format::Csv, m_sheet, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.startsWith("\xEF\xBB\xBF"));
    const QStringList lines = QString::fromUtf8(data.mid(3)).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines.at(0), m_sheet.columns.at(author).title + QLatin1Char(';') + m_sheet.columns.at(price).title);
    QVERIFY(lines.at(1).startsWith(bh::core::displayText(m_sheet.records.at(0).values.at(author))));

    exporter.columns.clear();
    QVERIFY(!exporter.write(path, FundExport::Format::Csv, m_sheet, &error));
}

void ExportTest::backupsListAndPrune()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("Фонд (рабочая копия).xlsx"));
    QVERIFY(QFile::copy(dataPath(QStringLiteral("fund_sample.xls")), work));  // содержимое не важно
    QDir(WorkCopies::backupDirectoryFor(work)).removeRecursively();

    QString error;
    const QString first = WorkCopies::backup(work, WorkCopies::saveBackupReason(), &error, 100, 3);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    const QString keep = WorkCopies::backup(work, QStringLiteral("перед очисткой"), &error);
    QVERIFY(!keep.isEmpty());
    for (int i = 0; i < 4; ++i)
        QVERIFY(!WorkCopies::backup(work, WorkCopies::saveBackupReason(), &error, 101 + i, 3).isEmpty());

    auto list = WorkCopies::backups(work);
    QCOMPARE(list.size(), 6);
    QCOMPARE(list.first().books, 104);  // новые первыми (в одну секунду — по имени)
    QCOMPARE(list.first().withdrawn, 3);
    QCOMPARE(list.first().reason, WorkCopies::saveBackupReason());
    QVERIFY(list.first().createdAt.isValid());
    bool foundKeep = false;
    for (const auto &b : list) {
        if (b.path == QFileInfo(keep).absoluteFilePath()) {
            foundKeep = true;
            QCOMPARE(b.reason, QStringLiteral("перед очисткой"));
            QCOMPARE(b.books, -1);
        }
    }
    QVERIFY(foundKeep);

    // Остаются 2 последние автоматические и копия перед очисткой.
    QCOMPARE(WorkCopies::pruneBackups(work, 2), 3);
    list = WorkCopies::backups(work);
    QCOMPARE(list.size(), 3);
    QVERIFY(QFileInfo::exists(keep));
    QVERIFY(!QFileInfo::exists(first));
    QDir(WorkCopies::backupDirectoryFor(work)).removeRecursively();
}

void ExportTest::actionLog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Журнал/журнал.jsonl"));
    ActionLog log(path);
    QVERIFY(log.append(QStringLiteral("open"), QStringLiteral("Фонд А.xlsx"), QStringLiteral("Рабочая копия"), 296));
    QVERIFY(log.append(QStringLiteral("withdraw"), QStringLiteral("Фонд А.xlsx"),
                       QStringLiteral("23.11.2026, причина: ветхость. инв. № 1, «Книга \"в кавычках\"»"), 2));
    QVERIFY(log.append(QStringLiteral("export"), QStringLiteral("Фонд Б.xlsx"), QStringLiteral("Весь фонд"), 10));
    // Повреждённая строка (например, оборванная при сбое питания) не мешает чтению.
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append));
        file.write("{\"time\":\"2026-11-23T10:");
        file.write("\n");
    }
    QVERIFY(log.append(QStringLiteral("save"), QStringLiteral("Фонд А.xlsx"), {}));

    const auto all = log.read();
    QCOMPARE(all.size(), 4);
    QCOMPARE(all.first().action, QStringLiteral("save"));  // новые первыми
    QCOMPARE(all.first().count, -1);
    QCOMPARE(all.last().action, QStringLiteral("open"));
    QCOMPARE(all.last().count, 296);
    QCOMPARE(all.last().actionTitle(), QStringLiteral("Открытие файла"));

    const auto fundA = log.read(500, QStringLiteral("Фонд А.xlsx"));
    QCOMPARE(fundA.size(), 3);
    QVERIFY(fundA.at(1).details.contains(QStringLiteral("\"в кавычках\"")));
    QCOMPARE(log.read(1).size(), 1);

    // Большой журнал откладывается в отдельный файл, новый начинается с нуля.
    log.setMaxBytes(200);
    QVERIFY(log.append(QStringLiteral("add"), QStringLiteral("Фонд А.xlsx"), QStringLiteral("инв. № 5"), 1));
    QCOMPARE(log.read().size(), 1);
    const QStringList files = QDir(QFileInfo(path).absolutePath()).entryList({QStringLiteral("*.jsonl")}, QDir::Files);
    QCOMPARE(files.size(), 2);
}

QTEST_GUILESS_MAIN(ExportTest)
#include "test_export.moc"
