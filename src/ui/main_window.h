#pragma once

#include "core/action_log.h"
#include "core/work_copy.h"

#include <QMainWindow>
#include <QVector>

class QAbstractItemModel;

class QCheckBox;
class QLabel;
class QStackedWidget;
class QTableView;
class QLineEdit;
class QPushButton;
class QTabBar;
class QTabWidget;
class QTableWidget;

namespace bh::ui {

class FilterHeaderView;
class FundTablePage;

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

    // Вкладки со списками: «Книги» (действующие) и «Выбывшие».
    FundTablePage *booksPage() const { return m_booksPage; }
    FundTablePage *withdrawnPage() const { return m_withdrawnPage; }
    QTableView *booksTable() const;
    FilterHeaderView *booksHeader() const;
    bool isWithdrawnTabActive() const;
    void showWithdrawnTab();
    // Кнопка «Отменить»: пустой текст — отменять нечего.
    void setUndoText(const QString &text);
    // Список прошлых версий (резервных копий) на вкладке «Импорт и экспорт».
    void setVersions(const QVector<core::WorkCopies::BackupInfo> &versions);
    // Журнал действий на вкладке «Журнал»; fund — чей журнал показан (пусто — всех фондов).
    void setLog(const QVector<core::LogEntry> &entries, const QString &fund);
    bool isLogTabActive() const;
    // Ярлыки листов-фондов внизу окна, как в Excel. Меньше двух листов — скрыты.
    void setSheetTabs(const QStringList &sheets, const QString &current);
    // Вкладка «Справка» (F1).
    void showHelp();

signals:
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void addBookRequested();
    void editRequested();
    void withdrawRequested();
    void exportRequested();
    void undoRequested();
    void restoreRequested();  // «Вернуть в фонд» на вкладке «Выбывшие»
    void reportRequested();   // «Сформировать Excel» на вкладке «Выбывшие»
    void withdrawnTabShown();
    void resetFiltersRequested();
    void searchTextChanged(const QString &text);
    void restoreVersionRequested(const QString &backupPath);
    void openBackupFolderRequested();
    void versionsTabShown();
    void logTabShown();
    void logScopeChanged(bool currentFundOnly);
    void openLogFolderRequested();
    // Книги из нескольких экземпляров (вкладка «Книги»).
    void findBooksRequested();
    void mergeBooksRequested();
    void splitBooksRequested();
    void expandAllBooksToggled(bool expand);
    void settingsRequested();
    // Пользователь щёлкнул по ярлыку другого листа.
    void sheetRequested(const QString &sheet);
    void reportProblemRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *createToolbar();
    QWidget *createSearchRow();
    QWidget *createBooksTab();
    QWidget *createWithdrawnTab();
    QWidget *createImportExportTab();
    QWidget *createLogTab();
    QWidget *createHelpTab();
    QPushButton *createActionButton(const QString &text, const QString &toolTip);
    void updateDocumentActions();
    void setPrimary(QPushButton *button, bool primary);

    QLabel *m_fileLabel = nullptr;
    QLabel *m_unsavedLabel = nullptr;
    QLineEdit *m_search = nullptr;
    QTabWidget *m_tabs = nullptr;
    FundTablePage *m_booksPage = nullptr;
    FundTablePage *m_withdrawnPage = nullptr;
    QPushButton *m_restore = nullptr;
    QPushButton *m_report = nullptr;
    QPushButton *m_findBooks = nullptr;
    QPushButton *m_mergeBooks = nullptr;
    QPushButton *m_splitBooks = nullptr;
    QCheckBox *m_expandAll = nullptr;

    QPushButton *m_open = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_saveAs = nullptr;
    QPushButton *m_addBook = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_withdraw = nullptr;
    QPushButton *m_export = nullptr;
    QPushButton *m_undo = nullptr;
    QPushButton *m_resetFilters = nullptr;
    QPushButton *m_exportTabButton = nullptr;
    QTableWidget *m_versions = nullptr;
    QPushButton *m_restoreVersion = nullptr;
    QPushButton *m_openBackups = nullptr;
    QLabel *m_versionsEmpty = nullptr;
    QVector<core::WorkCopies::BackupInfo> m_versionList;
    QWidget *m_logTab = nullptr;
    QWidget *m_helpTab = nullptr;
    QTabBar *m_sheetTabs = nullptr;
    QLabel *m_sheetLabel = nullptr;
    QWidget *m_sheetRow = nullptr;
    QTableWidget *m_log = nullptr;
    QCheckBox *m_logCurrentOnly = nullptr;
    QLabel *m_logEmpty = nullptr;

    QString m_workFileName;
    bool m_unsaved = false;
};

} // namespace bh::ui
