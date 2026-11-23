#pragma once

#include <QMainWindow>

class QAbstractItemModel;

class QLabel;
class QStackedWidget;
class QTableView;
class QLineEdit;
class QPushButton;
class QTabWidget;

namespace bh::ui {

class FilterHeaderView;

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

    // Текст на вкладке «Книги» вместо таблицы (файл не открыт или ошибка).
    void setBooksMessage(const QString &text);
    // Показать таблицу книг с данной моделью.
    void showBooksTable(QAbstractItemModel *model);
    QTableView *booksTable() const { return m_booksTable; }
    FilterHeaderView *booksHeader() const { return m_booksHeader; }
    // «Показано 120 из 3138 экземпляров».
    void setRecordCount(int shown, int total, bool filtered);
    // Кнопка «Отменить»: пустой текст — отменять нечего.
    void setUndoText(const QString &text);

signals:
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void addBookRequested();
    void editRequested();
    void withdrawRequested();
    void exportRequested();
    void undoRequested();
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
    QStackedWidget *m_booksStack = nullptr;
    QTableView *m_booksTable = nullptr;
    FilterHeaderView *m_booksHeader = nullptr;
    QLabel *m_countLabel = nullptr;

    QPushButton *m_open = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_saveAs = nullptr;
    QPushButton *m_addBook = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_withdraw = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_undo = nullptr;
    QPushButton *m_resetFilters = nullptr;

    QString m_workFileName;
    bool m_unsaved = false;
};

} // namespace bh::ui
