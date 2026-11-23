// Этап 4: внутренние ID экземпляров, правка и добавление записей, отмена,
// запись изменений в рабочую копию.

#include "core/copy_ids.h"
#include "core/fund_sheet.h"
#include "io/xlsx_workbook.h"
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

QTEST_GUILESS_MAIN(EditingTest)
#include "test_editing.moc"
