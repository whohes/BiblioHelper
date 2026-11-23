#pragma once

#include "core/work_copy.h"
#include "io/xlsx_workbook.h"
#include "models/fund_filter_proxy.h"
#include "models/fund_table_model.h"

#include <QObject>
#include <QTimer>

class QTableView;

namespace bh::ui {

class FundTablePage;
class MainWindow;

// Связывает кнопки главного окна с рабочим файлом: открыть (через
// рабочую копию), сохранить, правка, выбытие, отчёт.
// Обе вкладки («Книги» и «Выбывшие») показывают одну модель через
// разные фильтры: действующие и выбывшие экземпляры.
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
    void showColumnFilter(FundTablePage *page, models::FundFilterProxy *proxy, int column,
                          const QPoint &globalPos);
    void addBook();
    void editSelected();
    void editRow(int modelRow);
    void undo();
    void withdrawSelected();
    void restoreSelected();
    void createReport();
    // Очистка после отчёта: два подтверждения, резервная копия, перенос в архив.
    void cleanupReported(const QStringList &ids);
    void updateCounts();
    void fitColumns(QTableView *table);
    void updateColumnVisibility();
    void selectRecord(const QString &id);
    QString duplicateInventoryText(const QString &inventory, const QString &exceptId) const;

private:
    bool confirmDiscardChanges();
    // Строки модели, выделенные в таблице (по порядку на экране).
    QVector<int> selectedModelRows(QTableView *table, models::FundFilterProxy *proxy) const;
    QString countText(models::FundFilterProxy *proxy, bool withdrawn) const;
    void showError(const QString &title, const QString &text);

    MainWindow *m_window;
    core::WorkCopies m_workCopies;
    io::XlsxWorkbook m_workbook;
    QString m_originalPath;
    QString m_workPath;

    models::FundTableModel *m_model;
    models::FundFilterProxy *m_proxy;           // «Книги»: действующие
    models::FundFilterProxy *m_withdrawnProxy;  // «Выбывшие»
    QTimer m_searchTimer;  // поиск запускается после паузы в наборе
    QString m_pendingSearch;
    bool m_fitWithdrawnPending = false;
};

} // namespace bh::ui
