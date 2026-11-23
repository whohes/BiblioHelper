#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

namespace bh::ui {

// Главное окно: крупные кнопки с подписями, вкладки и строка состояния.
// Кнопки работы с документом включаются, когда открыт рабочий файл.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Состояние документа, которое видит пользователь.
    void setWorkFileName(const QString &fileName);
    void setUnsavedChanges(bool unsaved);
    bool hasUnsavedChanges() const { return m_unsaved; }

    // Текст на вкладке «Книги», пока нет таблицы.
    void setBooksMessage(const QString &text);

signals:
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void addBookRequested();
    void editRequested();
    void withdrawRequested();
    void exportRequested();
    void resetFiltersRequested();
    void searchTextChanged(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createToolbar();
    QWidget *createSearchRow();
    QWidget *createBooksTab();
    QWidget *createWithdrawnTab();
    QWidget *createImportExportTab();
    QWidget *createHelpTab();
    QPushButton *createActionButton(const QString &text, const QString &toolTip);
    void updateDocumentActions();

    QLabel *m_fileLabel = nullptr;
    QLabel *m_unsavedLabel = nullptr;
    QLineEdit *m_search = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_booksMessage = nullptr;

    QPushButton *m_open = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_saveAs = nullptr;
    QPushButton *m_addBook = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_withdraw = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_resetFilters = nullptr;

    QString m_workFileName;
    bool m_unsaved = false;
};

} // namespace bh::ui
