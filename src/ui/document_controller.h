#pragma once

#include "core/work_copy.h"
#include "io/xlsx_workbook.h"

#include <QObject>

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

private:
    bool confirmDiscardChanges();
    void showError(const QString &title, const QString &text);

    MainWindow *m_window;
    core::WorkCopies m_workCopies;
    io::XlsxWorkbook m_workbook;
    QString m_originalPath;
    QString m_workPath;
};

} // namespace bh::ui
