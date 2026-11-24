// Этап 4: внутренние ID экземпляров, правка и добавление записей, отмена,
// запись изменений в рабочую копию.

#include "core/copy_ids.h"
#include "core/fund_sheet.h"
#include "core/withdrawal_archive.h"
#include "core/withdrawal_report.h"
#include "io/xlsx_workbook.h"
#include "models/fund_filter_proxy.h"
#include "models/fund_table_model.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using bh::core::ColumnRole;
using bh::core::CopyIds;
using bh::core::FundSheet;
using bh::core::FundSheetReader;
using bh::io::XlsxWorkbook;

class EditingTest : public QObject
{
    Q_OBJECT

private slots:
    void valueFromInput();
    void idsSurviveReopen();
    void idsFollowShiftedRows();
    void editAddUndo();
    void writeAndReopen();
    void withdrawUndoRestore();
    void withdrawWriteAndReopen();
    void report();
    void archive();

private:
    static QString dataPath(const QString &name)
    {
        return QDir(QStringLiteral(BH_TEST_DATA_DIR)).filePath(name);
    }

    static bool openSheet(XlsxWorkbook &wb, const QString &path, FundSheet *sheet)
    {
        QString error;
        if (!wb.open(path, &error)) {
            qWarning("open: %s", qPrintable(error));
            return false;
        }
        if (!FundSheetReader::readBestSheet(wb, sheet, &error)) {
            qWarning("read: %s", qPrintable(error));
            return false;
        }
        return true;
    }
};

void EditingTest::valueFromInput()
{
    using bh::core::valueFromInput;
    QCOMPARE(valueFromInput(QStringLiteral(" 1234 "), ColumnRole::Inventory), QVariant(qlonglong(1234)));
    QCOMPARE(valueFromInput(QStringLiteral("12,5"), ColumnRole::Price), QVariant(12.5));
    QCOMPARE(valueFromInput(QStringLiteral("44р10к"), ColumnRole::Price), QVariant(QStringLiteral("44р10к")));
    QCOMPARE(valueFromInput(QStringLiteral("84"), ColumnRole::Department), QVariant(qlonglong(84)));
    QCOMPARE(valueFromInput(QStringLiteral("63,3"), ColumnRole::Department), QVariant(QStringLiteral("63,3")));
    QCOMPARE(valueFromInput(QStringLiteral("2001"), ColumnRole::AuthorTitle), QVariant(QStringLiteral("2001")));
    QCOMPARE(valueFromInput(QStringLiteral("бр"), ColumnRole::Inventory), QVariant(QStringLiteral("бр")));
    QVERIFY(!valueFromInput(QStringLiteral("   "), ColumnRole::AuthorTitle).isValid());
    // Было число в «прочем» столбце — число и остаётся.
    QCOMPARE(valueFromInput(QStringLiteral("7"), ColumnRole::Other, QVariant(3.0)), QVariant(qlonglong(7)));
}

void EditingTest::idsSurviveReopen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work.xlsx"));
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    QVERIFY(QFile::copy(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), work));

    QStringList ids;
    {
        XlsxWorkbook wb;
        FundSheet sheet;
        QVERIFY(openSheet(wb, work, &sheet));
        QVERIFY(CopyIds::load(wb).isEmpty());
        QCOMPARE(CopyIds::assign(&sheet, {}), sheet.records.size());
        QSet<QString> unique;
        for (const auto &r : sheet.records) {
            QVERIFY(r.id.startsWith(QStringLiteral("c-")));
            unique.insert(r.id);
            ids << r.id;
        }
        QCOMPARE(unique.size(), sheet.records.size());
        QString error;
        QVERIFY2(CopyIds::save(wb, sheet, &error), qPrintable(error));
        QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
    }

    XlsxWorkbook wb;
    FundSheet sheet;
    QVERIFY(openSheet(wb, saved, &sheet));
    QCOMPARE(sheet.sheetName, QStringLiteral("Лист1"));  // служебный лист не выбран как фонд
    QVERIFY(wb.isVeryHidden(CopyIds::sheetName()));
    const auto entries = CopyIds::load(wb);
    QCOMPARE(entries.size(), ids.size());
    QCOMPARE(CopyIds::assign(&sheet, entries), 0);
    for (int i = 0; i < sheet.records.size(); ++i)
        QCOMPARE(sheet.records.at(i).id, ids.at(i));
}

void EditingTest::idsFollowShiftedRows()
{
    XlsxWorkbook wb;
    FundSheet sheet;
    QVERIFY(openSheet(wb, dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), &sheet));
    CopyIds::assign(&sheet, {});

    QVector<CopyIds::Entry> entries;
    for (const auto &r : sheet.records)
        entries.push_back({r.id, sheet.sheetName, r.excelRow, CopyIds::fingerprint(sheet, r)});
    const QString firstId = sheet.records.at(0).id;
    const QString secondId = sheet.records.at(1).id;

    // Как будто в Excel вставили строку выше: все строки сдвинулись на 1.
    for (auto &e : entries)
        e.row += 1;
    for (auto &r : sheet.records)
        r.id.clear();
    QCOMPARE(CopyIds::assign(&sheet, entries), 0);
    QCOMPARE(sheet.records.at(0).id, firstId);
    QCOMPARE(sheet.records.at(1).id, secondId);

    // Поправили ячейку на месте: ID тот же (по номеру строки).
    for (auto &e : entries)
        e.row -= 1;
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    sheet.records[0].values[author] = QStringLiteral("Другое название");
    for (auto &r : sheet.records)
        r.id.clear();
    CopyIds::assign(&sheet, entries);
    QCOMPARE(sheet.records.at(0).id, firstId);
}

void EditingTest::editAddUndo()
{
    XlsxWorkbook wb;
    FundSheet sheet;
    QVERIFY(openSheet(wb, dataPath(QStringLiteral("fund_bobrinka.xlsx")), &sheet));
    CopyIds::assign(&sheet, {});
    const int count = sheet.records.size();
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);

    bh::models::FundTableModel model;
    QSignalSpy modified(&model, &bh::models::FundTableModel::modifiedChanged);
    model.setSheet(sheet);
    QVERIFY(!model.isModified());
    QVERIFY(!model.canUndo());

    const QString oldTitle = model.index(5, author).data().toString();
    QVector<QVariant> values = model.sheet().records.at(5).values;
    QVERIFY(!model.updateRecord(5, values));  // ничего не изменилось
    values[author] = QStringLiteral("Новое название");
    QVERIFY(model.updateRecord(5, values));
    QVERIFY(model.isModified());
    QCOMPARE(modified.count(), 1);
    QCOMPARE(model.index(5, author).data().toString(), QStringLiteral("Новое название"));
    QVERIFY(model.canUndo());

    QVector<QVariant> fresh(sheet.columns.size());
    fresh[author] = QStringLiteral("Тестов Т. Новая книга");
    const int row = model.addRecord(fresh);
    QCOMPARE(row, count);
    QCOMPARE(model.rowCount(), count + 1);
    QCOMPARE(model.newRecordCount(), 1);
    QVERIFY(!model.index(row, 0).data(bh::models::FundTableModel::IdRole).toString().isEmpty());
    QCOMPARE(model.rowForId(model.index(row, 0).data(bh::models::FundTableModel::IdRole).toString()), row);

    model.undo();  // убирает новую книгу
    QCOMPARE(model.rowCount(), count);
    QCOMPARE(model.newRecordCount(), 0);
    model.undo();  // возвращает название
    QCOMPARE(model.index(5, author).data().toString(), oldTitle);
    QVERIFY(!model.canUndo());
    QVERIFY(!model.isModified());
}

void EditingTest::writeAndReopen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work.xlsx"));
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    QVERIFY(QFile::copy(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), work));

    int count = 0;
    int lastExcelRow = 0;
    QString editedId;
    QString newId;
    int author = -1;
    int inventory = -1;
    {
        XlsxWorkbook wb;
        FundSheet sheet;
        QVERIFY(openSheet(wb, work, &sheet));
        CopyIds::assign(&sheet, {});
        count = sheet.records.size();
        lastExcelRow = sheet.records.last().excelRow;
        author = sheet.columnIndex(ColumnRole::AuthorTitle);
        inventory = sheet.columnIndex(ColumnRole::Inventory);

        bh::models::FundTableModel model;
        model.setSheet(sheet);
        QVector<QVariant> values = model.sheet().records.at(10).values;
        values[author] = QStringLiteral("Исправленное название");
        QVERIFY(model.updateRecord(10, values));
        editedId = model.sheet().records.at(10).id;

        QVector<QVariant> fresh(sheet.columns.size());
        fresh[author] = QStringLiteral("Тестов Т. Новая книга");
        fresh[inventory] = qlonglong(999001);
        const int row = model.addRecord(fresh);
        newId = model.sheet().records.at(row).id;

        QString error;
        QVERIFY2(model.writeChanges(wb, &error), qPrintable(error));
        QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
        model.markSaved();
        QVERIFY(!model.isModified());
        QCOMPARE(model.sheet().records.at(row).excelRow, lastExcelRow + 1);
    }

    XlsxWorkbook wb;
    FundSheet sheet;
    QVERIFY(openSheet(wb, saved, &sheet));
    QCOMPARE(sheet.records.size(), count + 1);
    QCOMPARE(CopyIds::assign(&sheet, CopyIds::load(wb)), 0);

    const auto &added = sheet.records.last();
    QCOMPARE(added.excelRow, lastExcelRow + 1);
    QCOMPARE(added.id, newId);
    QCOMPARE(bh::core::displayText(added.values.at(author)), QStringLiteral("Тестов Т. Новая книга"));
    QCOMPARE(bh::core::displayText(added.values.at(inventory)), QStringLiteral("999001"));
    QCOMPARE(sheet.records.at(10).id, editedId);
    QCOMPARE(bh::core::displayText(sheet.records.at(10).values.at(author)),
             QStringLiteral("Исправленное название"));
    // Итоговые строки уехали вниз, а не попали в записи.
    for (const auto &r : sheet.records)
        QVERIFY(!bh::core::displayText(r.values.at(author)).startsWith(QStringLiteral("По документам")));
}

void EditingTest::withdrawUndoRestore()
{
    XlsxWorkbook wb;
    FundSheet sheet;
    QVERIFY(openSheet(wb, dataPath(QStringLiteral("fund_bobrinka.xlsx")), &sheet));
    CopyIds::assign(&sheet, {});
    const int count = sheet.records.size();

    bh::models::FundTableModel model;
    model.setSheet(sheet);
    bh::models::FundFilterProxy active;
    active.setSourceModel(&model);
    active.setStatusFilter(bh::models::FundFilterProxy::StatusFilter::Active);
    active.setDynamicSortFilter(true);
    bh::models::FundFilterProxy withdrawn;
    withdrawn.setSourceModel(&model);
    withdrawn.setStatusFilter(bh::models::FundFilterProxy::StatusFilter::Withdrawn);
    withdrawn.setDynamicSortFilter(true);
    QCOMPARE(active.rowCount(), count);
    QCOMPARE(withdrawn.rowCount(), 0);

    const QDate date(2026, 11, 23);
    QCOMPARE(model.withdraw({3, 4}, date, QStringLiteral(" ветхость "), QStringLiteral("акт №5")), 2);
    QVERIFY(model.isWithdrawn(3));
    QVERIFY(model.isModified());
    QCOMPARE(active.rowCount(), count - 2);
    QCOMPARE(withdrawn.rowCount(), 2);
    const int status = model.sheet().columnIndex(ColumnRole::Status);
    const int dateCol = model.sheet().columnIndex(ColumnRole::WithdrawDate);
    const int reason = model.sheet().columnIndex(ColumnRole::WithdrawReason);
    QCOMPARE(model.index(3, status).data().toString(), QStringLiteral("Выбыло 23.11.2026, ветхость"));
    QCOMPARE(model.index(3, dateCol).data().toString(), QStringLiteral("23.11.2026"));
    QCOMPARE(model.index(4, reason).data().toString(), QStringLiteral("ветхость"));
    QCOMPARE(model.undoText(), QStringLiteral("Отменить выбытие 2 экз."));

    // Уже выбывший экземпляр второй раз не списывается.
    QCOMPARE(model.withdraw({3}, date, QStringLiteral("утеря"), {}), 0);

    // Сумма считается только по действующим.
    const int price = model.sheet().columnIndex(ColumnRole::Price);
    const int quantity = model.sheet().columnIndex(ColumnRole::Quantity);
    QCOMPARE(active.totals(price, quantity).rows, count - 2);

    model.undo();
    QVERIFY(!model.isWithdrawn(3));
    QCOMPARE(withdrawn.rowCount(), 0);
    QVERIFY(!model.isModified());

    QCOMPARE(model.withdraw({7}, date, QStringLiteral("утеря"), {}), 1);
    QCOMPARE(model.restore({7, 8}), 1);  // 8 не выбывал
    QVERIFY(!model.isWithdrawn(7));
    QVERIFY(model.index(7, status).data().toString().isEmpty());
    model.undo();  // отмена возврата: снова выбыл
    QVERIFY(model.isWithdrawn(7));
    QCOMPARE(model.index(7, reason).data().toString(), QStringLiteral("утеря"));
}

void EditingTest::withdrawWriteAndReopen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work.xlsx"));
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    const QString saved2 = tmp.filePath(QStringLiteral("saved2.xlsx"));
    QVERIFY(QFile::copy(dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), work));

    QString withdrawnId;
    int withdrawnRow = 0;
    int statusExcelCol = 0;
    int count = 0;
    {
        XlsxWorkbook wb;
        FundSheet sheet;
        QVERIFY(openSheet(wb, work, &sheet));
        CopyIds::assign(&sheet, {});
        count = sheet.records.size();
        const int status = sheet.columnIndex(ColumnRole::Status);
        QVERIFY(status >= 0);
        QVERIFY(!sheet.columns.at(status).inFile);
        statusExcelCol = sheet.columns.at(status).excelColumn;
        QVERIFY(statusExcelCol > 1);

        bh::models::FundTableModel model;
        model.setSheet(sheet);
        // Правка другой книги и новая книга в том же сохранении.
        QVector<QVariant> fresh(sheet.columns.size());
        fresh[sheet.columnIndex(ColumnRole::AuthorTitle)] = QStringLiteral("Тестов Т. Новая книга");
        fresh[sheet.columnIndex(ColumnRole::Inventory)] = qlonglong(999002);
        const int added = model.addRecord(fresh);
        QCOMPARE(model.withdraw({20, added}, QDate(2026, 11, 23), QStringLiteral("утеря"), QStringLiteral("акт 1")), 2);
        withdrawnId = model.sheet().records.at(20).id;
        withdrawnRow = model.sheet().records.at(20).excelRow;

        QString error;
        QVERIFY2(model.writeChanges(wb, &error), qPrintable(error));
        QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
        model.markSaved();
        QCOMPARE(wb.cellValue(sheet.sheetName, sheet.headerRow, statusExcelCol).toString(), QStringLiteral("Статус"));
    }

    FundSheet sheet;
    {
        XlsxWorkbook wb;
        QVERIFY(openSheet(wb, saved, &sheet));
        QCOMPARE(sheet.records.size(), count + 1);
        // «Статус» теперь в файле, на том же месте.
        const int status = sheet.columnIndex(ColumnRole::Status);
        QVERIFY(status >= 0);
        QVERIFY(sheet.columns.at(status).inFile);
        QCOMPARE(sheet.columns.at(status).excelColumn, statusExcelCol);
        QCOMPARE(wb.cellValue(sheet.sheetName, withdrawnRow, statusExcelCol).toString(),
                 QStringLiteral("Выбыло 23.11.2026, утеря"));
        // Прочие строки без статуса.
        QVERIFY(wb.cellValue(sheet.sheetName, withdrawnRow + 1, statusExcelCol).toString().isEmpty());

        QCOMPARE(CopyIds::assign(&sheet, CopyIds::load(wb)), 0);
        int withdrawnCount = 0;
        int row = -1;
        for (int i = 0; i < sheet.records.size(); ++i) {
            if (sheet.isWithdrawn(i)) {
                ++withdrawnCount;
                if (sheet.records.at(i).id == withdrawnId)
                    row = i;
            }
        }
        QCOMPARE(withdrawnCount, 2);
        QVERIFY(row >= 0);
        QCOMPARE(sheet.records.at(row).values.at(sheet.columnIndex(ColumnRole::WithdrawDate)).toDate(),
                 QDate(2026, 11, 23));
        QCOMPARE(bh::core::displayText(sheet.records.at(row).values.at(sheet.columnIndex(ColumnRole::WithdrawNote))),
                 QStringLiteral("акт 1"));
        QVERIFY(sheet.isWithdrawn(sheet.records.size() - 1));  // новая книга тоже выбыла

        // Вернуть в фонд и сохранить ещё раз.
        bh::models::FundTableModel model;
        model.setSheet(sheet);
        QCOMPARE(model.restore({row}), 1);
        QString error;
        QVERIFY2(model.writeChanges(wb, &error), qPrintable(error));
        QVERIFY2(wb.saveAs(saved2, &error), qPrintable(error));
    }

    XlsxWorkbook wb;
    FundSheet again;
    QVERIFY(openSheet(wb, saved2, &again));
    CopyIds::assign(&again, CopyIds::load(wb));
    int withdrawnCount = 0;
    for (int i = 0; i < again.records.size(); ++i)
        withdrawnCount += again.isWithdrawn(i) ? 1 : 0;
    QCOMPARE(withdrawnCount, 1);
    QVERIFY(wb.cellValue(again.sheetName, withdrawnRow, statusExcelCol).toString().isEmpty());
}

void EditingTest::report()
{
    XlsxWorkbook source;
    FundSheet sheet;
    QVERIFY(openSheet(source, dataPath(QStringLiteral("fund_bogdanovka_2025.xlsx")), &sheet));
    CopyIds::assign(&sheet, {});
    const int author = sheet.columnIndex(ColumnRole::AuthorTitle);
    const int price = sheet.columnIndex(ColumnRole::Price);

    // Три экземпляра: цена числом, цена «NрMк» и строка без цены (если найдётся).
    QVector<int> rows;
    int textPrice = -1;
    int noPrice = -1;
    for (int i = 0; i < sheet.records.size(); ++i) {
        const QVariant p = sheet.records.at(i).values.at(price);
        const QString t = bh::core::displayText(p);
        double rub = 0;
        if (textPrice < 0 && p.type() == QVariant::String && bh::core::parseRubles(t, &rub) && t.contains(QStringLiteral("р")))
            textPrice = i;
        if (noPrice < 0 && !bh::core::priceInRubles(p, &rub))
            noPrice = i;
    }
    QVERIFY(textPrice > 0 && noPrice > 0 && textPrice != noPrice);
    int active = 1;
    while (active == textPrice || active == noPrice)
        ++active;
    // active — не выбыл, повтор textPrice — дубль: в отчёт не попадают.
    rows << 0 << textPrice << noPrice << active << textPrice;

    bh::models::FundTableModel model;
    model.setSheet(sheet);
    QCOMPARE(model.withdraw({0, textPrice, noPrice}, QDate(2026, 11, 20), QStringLiteral("ветхость"),
                            QStringLiteral("акт № 3")), 3);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // Кириллица в пути, как в «Документы\\BiblioHelper\\Отчёты».
    const QString path = tmp.filePath(QStringLiteral("Отчёты/Выбывшие 2026-11-23.xlsx"));
    QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("Отчёты")));
    bh::core::WithdrawalReport report;
    report.fundName = QStringLiteral("Фонд Богдановской библиотеки 2025");
    report.createdOn = QDate(2026, 11, 23);
    report.records = rows;
    QString error;
    QVERIFY2(report.write(path, model.sheet(), &error), qPrintable(error));

    XlsxWorkbook wb;
    QVERIFY2(wb.open(path, &error), qPrintable(error));
    const QString s = wb.sheetNames().first();
    QCOMPARE(s, QStringLiteral("Выбывшие"));
    QVERIFY(wb.cellValue(s, 2, 1).toString().contains(QStringLiteral("Богдановской")));
    const QStringList titles = bh::core::WithdrawalReport::columnTitles();
    for (int c = 0; c < titles.size(); ++c)
        QCOMPARE(wb.cellValue(s, 4, c + 1).toString(), titles.at(c));

    double expected = 0;
    for (int i = 0; i < 3; ++i) {
        const auto &record = model.sheet().records.at(rows.at(i));
        const int r = 5 + i;
        QCOMPARE(wb.cellValue(s, r, 1).toInt(), i + 1);
        QCOMPARE(bh::core::displayText(wb.cellValue(s, r, 2)), bh::core::displayText(record.values.at(author)));
        // Год выпуска и отдел — как в фонде.
        QCOMPARE(bh::core::displayText(wb.cellValue(s, r, 4)),
                 bh::core::displayText(record.values.at(model.sheet().columnIndex(ColumnRole::Year))));
        QCOMPARE(bh::core::displayText(wb.cellValue(s, r, 5)),
                 bh::core::displayText(record.values.at(model.sheet().columnIndex(ColumnRole::Department))));
        QCOMPARE(wb.cellValue(s, r, 8).toString(), QStringLiteral("ветхость"));
        QCOMPARE(wb.cellValue(s, r, 9).toString(), QStringLiteral("акт № 3"));
        double rub = 0;
        if (bh::core::priceInRubles(record.values.at(price), &rub)) {
            QVERIFY(qAbs(wb.cellValue(s, r, 6).toDouble() - rub) < 1e-9);
            expected += rub;
        }
    }
    // «Сорок четыре рубля десять копеек» стали числом.
    double rub = 0;
    QVERIFY(bh::core::parseRubles(bh::core::displayText(model.sheet().records.at(textPrice).values.at(price)), &rub));
    QCOMPARE(wb.cellValue(s, 6, 6).toDouble(), rub);
    // Без цены — как в фонде.
    QCOMPARE(bh::core::displayText(wb.cellValue(s, 7, 6)),
             bh::core::displayText(model.sheet().records.at(noPrice).values.at(price)));
    // Дата выбытия — дата Excel (серийное число 46346 = 20.11.2026).
    QCOMPARE(wb.cellValue(s, 5, 7).toInt(), 46346);
    // Итог.
    QCOMPARE(wb.cellValue(s, 8, 2).toString(), QStringLiteral("Итого: 3 экз."));
    QVERIFY(qAbs(wb.cellValue(s, 8, 6).toDouble() - expected) < 1e-6);
    QVERIFY(wb.cellValue(s, 9, 2).toString().isEmpty());
}

void EditingTest::archive()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString work = tmp.filePath(QStringLiteral("work.xlsx"));
    const QString saved = tmp.filePath(QStringLiteral("saved.xlsx"));
    const QString cleaned = tmp.filePath(QStringLiteral("cleaned.xlsx"));
    QVERIFY(QFile::copy(dataPath(QStringLiteral("fund_bobrinka.xlsx")), work));

    // Строки Excel: 10 (обычная), 2254 (на неё ссылается G24: =$G$2254), 8245 (последняя, в =SUM).
    const QVector<int> excelRows{10, 2254, 8245};
    QStringList titles;
    QString cachedG24;
    int count = 0;
    {
        XlsxWorkbook wb;
        FundSheet sheet;
        QVERIFY(openSheet(wb, work, &sheet));
        CopyIds::assign(&sheet, {});
        count = sheet.records.size();
        QCOMPARE(wb.cellFormula(sheet.sheetName, 24, 7), QStringLiteral("$G$2254"));
        QCOMPARE(wb.cellFormula(sheet.sheetName, 8246, 4), QStringLiteral("SUM(D3:D8245)"));
        cachedG24 = bh::core::displayText(wb.cellValue(sheet.sheetName, 24, 7));
        bh::models::FundTableModel model;
        model.setSheet(sheet);
        QVector<int> rows;
        for (int i = 0; i < sheet.records.size(); ++i) {
            if (excelRows.contains(sheet.records.at(i).excelRow)) {
                rows << i;
                titles << bh::core::displayText(sheet.records.at(i).values.at(sheet.columnIndex(ColumnRole::AuthorTitle)));
            }
        }
        QCOMPARE(rows.size(), 3);
        QCOMPARE(model.withdraw(rows, QDate(2026, 11, 23), QStringLiteral("ветхость"), {}), 3);
        QString error;
        QVERIFY2(model.writeChanges(wb, &error), qPrintable(error));
        QVERIFY2(wb.saveAs(saved, &error), qPrintable(error));
    }

    QStringList ids;
    {
        XlsxWorkbook wb;
        FundSheet sheet;
        QVERIFY(openSheet(wb, saved, &sheet));
        CopyIds::assign(&sheet, CopyIds::load(wb));
        QVector<int> withdrawn;
        for (int i = 0; i < sheet.records.size(); ++i) {
            if (sheet.isWithdrawn(i))
                withdrawn << i;
            else if (ids.size() < 20)
                ids << sheet.records.at(i).id;
        }
        QCOMPARE(withdrawn.size(), 3);
        QString error;
        QCOMPARE(bh::core::WithdrawalArchive::archive(wb, &sheet, withdrawn, &error), 3);
        QCOMPARE(sheet.records.size(), count - 3);
        QVERIFY2(wb.saveAs(cleaned, &error), qPrintable(error));
    }

    XlsxWorkbook wb;
    FundSheet sheet;
    QVERIFY(openSheet(wb, cleaned, &sheet));
    QCOMPARE(sheet.sheetName, QStringLiteral("Лист1"));  // архив не считается фондом
    QCOMPARE(sheet.records.size(), count - 3);
    QCOMPARE(CopyIds::assign(&sheet, CopyIds::load(wb)), 0);
    for (int i = 0; i < ids.size(); ++i)
        QCOMPARE(sheet.records.at(i).id, ids.at(i));
    for (int i = 0; i < sheet.records.size(); ++i)
        QVERIFY(!sheet.isWithdrawn(i));

    const QString s = sheet.sheetName;
    // Итог сжался на 3 строки и уехал вверх.
    QCOMPARE(wb.cellFormula(s, 8243, 4), QStringLiteral("SUM(D3:D8242)"));
    // G24 → G23: ссылался на удалённую строку — стал значением.
    QVERIFY(!wb.cellHasFormula(s, 23, 7));
    QCOMPARE(bh::core::displayText(wb.cellValue(s, 23, 7)), cachedG24);
    // G27 (=$G$2250) → G26 и =$G$2248 (удалены 10 и 2254 → минус одна строка выше 2250).
    QCOMPARE(wb.cellFormula(s, 26, 7), QStringLiteral("$G$2249"));

    // Архив: заголовок и три строки по порядку, со статусом.
    QVERIFY(wb.sheetNames().contains(bh::core::archiveSheetName()));
    const QString a = bh::core::archiveSheetName();
    QCOMPARE(bh::core::displayText(wb.cellValue(a, 1, 2)), QStringLiteral("автор книги и название"));
    for (int i = 0; i < 3; ++i)
        QCOMPARE(bh::core::displayText(wb.cellValue(a, 2 + i, 2)), titles.at(i));
    const int statusCol = sheet.columns.at(sheet.columnIndex(ColumnRole::Status)).excelColumn;
    QCOMPARE(wb.cellValue(a, 2, statusCol).toString(), QStringLiteral("Выбыло 23.11.2026, ветхость"));
    QVERIFY(wb.cellValue(a, 5, 2).toString().isEmpty());
}

QTEST_GUILESS_MAIN(EditingTest)
#include "test_editing.moc"
