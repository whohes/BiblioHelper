#pragma once

#include "core/work_copy.h"
#include "io/xlsx_workbook.h"
#include "models/fund_filter_proxy.h"
#include "models/fund_table_model.h"

#include <QObject>
#include <QTimer>

namespace bh::ui {

class MainWindow;

// Связывает кнопки главного окна с рабочим файлом: открыть (через
// рабочую копию), сохранить, сохранить как.
class DocumentController : public QObject
{
    Q_OBJECT

public:
    explicit DocumentController(MainWindow *window);

    // Открыть файл без диалога (например, переданный в командной строке).
    bool openFile(const QString &path);

private slots:
    void open();
    void save();
    void saveAs();
    void showColumnFilter(int column, const QPoint &globalPos);
    void addBook();
    void editSelected();
    void editRow(int modelRow);
    void undo();
    void updateCounts();
    void fitColumns();
    void selectRecord(const QString &id);
    QString duplicateInventoryText(const QString &inventory, const QString &exceptId) const;

private:
    bool confirmDiscardChanges();
    void showError(const QString &title, const QString &text);

    MainWindow *m_window;
    core::WorkCopies m_workCopies;
    io::XlsxWorkbook m_workbook;
    QString m_originalPath;
    QString m_workPath;

    models::FundTableModel *m_model;
    models::FundFilterProxy *m_proxy;
    QTimer m_searchTimer;  // поиск запускается после паузы в наборе
    QString m_pendingSearch;
};

} // namespace bh::ui
