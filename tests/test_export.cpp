// Этап 7: экспорт и прошлые версии (резервные копии).

#include "core/action_log.h"
#include "core/app_settings.h"
#include "core/update_info.h"
#include "core/problem_report.h"
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
    void problemReport();
    void settings();
    void updateInfo();

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
    QCoreApplication::setOrganizationName(QStringLiteral("BiblioHelperTest"));
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

void ExportTest::problemReport()
{
    bh::core::ProblemReport report;
    report.description = QStringLiteral("Не сохраняется & пишет «файл занят» + 100%");
    report.appVersion = QStringLiteral("0.1.0");
    report.system = QStringLiteral("Windows 7 SP 1");
    report.fund = QStringLiteral("Фонд А.xlsx");
    for (int i = 0; i < 30; ++i) {
        bh::core::LogEntry e;
        e.time = QDateTime(QDate(2026, 11, 23), QTime(10, i));
        e.action = QStringLiteral("edit");
        e.details = QString(200, QLatin1Char('x'));
        e.count = 1;
        report.log.push_back(e);
    }
    const QString body = report.body();
    QVERIFY(body.startsWith(report.description));
    QVERIFY(body.contains(QStringLiteral("BiblioHelper 0.1.0")));
    QVERIFY(body.contains(QStringLiteral("Фонд А.xlsx")));
    QCOMPARE(body.count(QStringLiteral("Изменение")), 20);  // не больше 20 записей журнала

    const QUrl url = report.mailto(QStringLiteral("bibliohelpers@gmail.com"), 1800);
    QCOMPARE(url.scheme(), QStringLiteral("mailto"));
    QCOMPARE(url.path(), QStringLiteral("bibliohelpers@gmail.com"));
    // «&» и «+» в тексте не ломают ссылку: тема и текст читаются обратно целиком.
    const QString query = url.query(QUrl::FullyEncoded);
    const QStringList parts = query.split(QLatin1Char('&'));
    QCOMPARE(parts.size(), 2);
    const QString mailBody = QUrl::fromPercentEncoding(parts.at(1).mid(5).toLatin1());
    QVERIFY(mailBody.startsWith(report.description));
    QVERIFY(mailBody.size() <= 1800);
    QVERIFY(mailBody.contains(QStringLiteral("Последние действия")));
    QCOMPARE(QUrl::fromPercentEncoding(parts.at(0).mid(8).toLatin1()), report.subject());

    report.includeDetails = false;
    QCOMPARE(report.body(), report.description);
}

void ExportTest::settings()
{
    namespace S = bh::core::AppSettings;
    QCOMPARE(S::supportEmail(), QStringLiteral("bibliohelpers@gmail.com"));
    QTemporaryDir dir;
    S::setDataRoot(dir.path());
    QCOMPARE(S::dataRoot(), QDir::cleanPath(dir.path()));
    QCOMPARE(WorkCopies::defaultDirectory(), QDir(dir.path()).filePath(QStringLiteral("Рабочие файлы")));
    QCOMPARE(bh::core::ActionLog::defaultPath(), QDir(dir.path()).filePath(QStringLiteral("Журнал/журнал.jsonl")));
    S::setDataRoot({});
    QCOMPARE(S::dataRoot(), S::defaultDataRoot());
    S::setInterfaceScale(130);
    QCOMPARE(S::interfaceScale(), 100);
    S::setInterfaceScale(125);
    QCOMPARE(S::interfaceScale(), 125);
    S::setInterfaceScale(100);
    S::setKeepSaveBackups(0);
    QCOMPARE(S::keepSaveBackups(), 1);
    S::setKeepSaveBackups(20);
}

void ExportTest::updateInfo()
{
    using namespace bh::core;
    QVERIFY(Updates::compareVersions(QStringLiteral("1.10.0"), QStringLiteral("1.9.3")) > 0);
    QVERIFY(Updates::compareVersions(QStringLiteral("v1.2"), QStringLiteral("1.2.0")) == 0);
    QVERIFY(Updates::compareVersions(QStringLiteral("1.0.0"), QStringLiteral("1.0.1")) < 0);
    QVERIFY(Updates::compareVersions(QStringLiteral("2.0.0-beta"), QStringLiteral("1.9")) > 0);

    const QByteArray json = R"({"tag_name":"v1.2.0","body":"* Исправлено","html_url":"https://github.com/whohes/BiblioHelper/releases/tag/v1.2.0",
        "draft":false,"prerelease":false,"assets":[
        {"name":"notes.txt","browser_download_url":"https://example.org/notes.txt","size":10},
        {"name":"BiblioHelper-1.2.0-setup.exe","browser_download_url":"https://github.com/whohes/BiblioHelper/releases/download/v1.2.0/BiblioHelper-1.2.0-setup.exe","size":12345}]})";
    const auto info = Updates::parseLatestRelease(json);
    QVERIFY(info.has_value());
    QCOMPARE(info->version, QStringLiteral("1.2.0"));
    QCOMPARE(info->setupName, QStringLiteral("BiblioHelper-1.2.0-setup.exe"));
    QCOMPARE(info->setupSize, qint64(12345));
    QCOMPARE(info->notes, QStringLiteral("* Исправлено"));

    // Без установщика, по http или предварительная версия — не предлагаем.
    QVERIFY(!Updates::parseLatestRelease(R"({"tag_name":"v1.3","assets":[]})"));
    QVERIFY(!Updates::parseLatestRelease(
        R"({"tag_name":"v1.3","assets":[{"name":"a-setup.exe","browser_download_url":"http://x/a-setup.exe"}]})"));
    QVERIFY(!Updates::parseLatestRelease(
        R"({"tag_name":"v1.3","prerelease":true,"assets":[{"name":"a-setup.exe","browser_download_url":"https://x/a-setup.exe"}]})"));
    QVERIFY(!Updates::parseLatestRelease("not json"));

    QVERIFY(AppSettings::checkUpdates());
    AppSettings::setCheckUpdates(false);
    QVERIFY(!AppSettings::checkUpdates());
    AppSettings::setCheckUpdates(true);
    AppSettings::setLastUpdateCheck(QDate(2026, 11, 24));
    QCOMPARE(AppSettings::lastUpdateCheck(), QDate(2026, 11, 24));
}

QTEST_GUILESS_MAIN(ExportTest)
#include "test_export.moc"
