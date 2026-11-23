#include "main_window.h"

#include "filter_header_view.h"
#include "fund_table_page.h"
#include "icons.h"

#include <QFontMetrics>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QCheckBox>
#include <QColor>
#include <QLabel>
#include <QItemSelectionModel>
#include <QLocale>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace bh::ui {

namespace {

constexpr int kButtonMinHeight = 48;

QLabel *createEmptyState(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("emptyState"));
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Учёт книжного фонда"));
    resize(1280, 720);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(10);

    // Строка с названием рабочего файла и признаком несохранённых изменений.
    auto *fileRow = new QHBoxLayout;
    m_fileLabel = new QLabel(central);
    m_fileLabel->setObjectName(QStringLiteral("fileLabel"));
    m_unsavedLabel = new QLabel(central);
    m_unsavedLabel->setObjectName(QStringLiteral("unsavedLabel"));
    // Длинное имя файла сокращается, а надпись о несохранённых изменениях видна всегда.
    m_fileLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    fileRow->addWidget(m_fileLabel, 1);
    m_unsavedLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    fileRow->addWidget(m_unsavedLabel);
    layout->addLayout(fileRow);

    layout->addWidget(createToolbar());
    layout->addWidget(createSearchRow());

    m_tabs = new QTabWidget(central);
    m_tabs->addTab(createBooksTab(), QStringLiteral("Книги"));
    m_tabs->addTab(createWithdrawnTab(), QStringLiteral("Выбывшие"));
    m_tabs->addTab(createImportExportTab(), QStringLiteral("Импорт и экспорт"));
    const int versionsTab = m_tabs->count() - 1;
    connect(m_tabs, &QTabWidget::currentChanged, this, [this, versionsTab](int index) {
        if (index == versionsTab)
            emit versionsTabShown();
    });
    m_logTab = createLogTab();
    m_tabs->addTab(m_logTab, QStringLiteral("Журнал"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (isLogTabActive())
            emit logTabShown();
    });
    m_tabs->addTab(createHelpTab(), QStringLiteral("Справка"));
    layout->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (isWithdrawnTabActive())
            emit withdrawnTabShown();
    });

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Откройте Excel-файл, чтобы начать работу"));

    setWorkFileName({});
    setUnsavedChanges(false);
}

QPushButton *MainWindow::createActionButton(const QString &text, const QString &toolTip)
{
    auto *button = new QPushButton(text);
    button->setMinimumHeight(kButtonMinHeight);
    // Кнопка не сжимается меньше своей подписи: текст всегда виден целиком.
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    button->setToolTip(toolTip);
    button->setCursor(Qt::PointingHandCursor);
    button->setIconSize(QSize(22, 22));
    return button;
}

QWidget *MainWindow::createToolbar()
{
    auto *bar = new QWidget;
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_open = createActionButton(QStringLiteral("Открыть Excel"),
                                QStringLiteral("Выбрать Excel-файл. Программа сделает его рабочую копию, "
                                               "оригинал не изменится."));
    m_save = createActionButton(QStringLiteral("Сохранить"),
                                QStringLiteral("Записать изменения в рабочую копию"));
    m_saveAs = createActionButton(QStringLiteral("Сохранить как…"),
                                  QStringLiteral("Сохранить рабочий файл в другое место под новым именем"));
    m_addBook = createActionButton(QStringLiteral("Добавить книгу"),
                                   QStringLiteral("Открыть форму для новой книги"));
    m_edit = createActionButton(QStringLiteral("Изменить"),
                                QStringLiteral("Изменить выбранную книгу или экземпляр"));
    m_withdraw = createActionButton(QStringLiteral("Выбытие"),
                                    QStringLiteral("Оформить выбытие отмеченных экземпляров"));
    m_export = createActionButton(QStringLiteral("Экспорт"),
                                  QStringLiteral("Сохранить список в новый Excel-файл"));

    m_undo = createActionButton(QStringLiteral("Отменить"),
                                QStringLiteral("Отменить последнее изменение (до сохранения)"));
    m_undo->setShortcut(QKeySequence::Undo);
    m_undo->setEnabled(false);

    // Кнопки по группам: файл | книги | выбытие и выгрузка.
    m_open->setIcon(makeIcon(IconKind::Open));
    m_save->setIcon(makeIcon(IconKind::Save));
    m_saveAs->setIcon(makeIcon(IconKind::SaveAs));
    m_addBook->setIcon(makeIcon(IconKind::Add));
    m_edit->setIcon(makeIcon(IconKind::Edit));
    m_undo->setIcon(makeIcon(IconKind::Undo));
    m_withdraw->setIcon(makeIcon(IconKind::Withdraw, QColor(0xa3, 0x3b, 0x2f)));
    m_export->setIcon(makeIcon(IconKind::Export));
    const QList<QList<QPushButton *>> groups{{m_open, m_save, m_saveAs},
                                             {m_addBook, m_edit, m_undo},
                                             {m_withdraw, m_export}};
    for (int g = 0; g < groups.size(); ++g) {
        if (g > 0) {
            auto *separator = new QFrame(bar);
            separator->setObjectName(QStringLiteral("toolbarSeparator"));
            separator->setFrameShape(QFrame::VLine);
            separator->setFixedWidth(1);
            row->addSpacing(6);
            row->addWidget(separator);
            row->addSpacing(6);
        }
        for (QPushButton *button : groups.at(g))
            row->addWidget(button);
    }
    row->addStretch(1);

    connect(m_open, &QPushButton::clicked, this, &MainWindow::openRequested);
    connect(m_save, &QPushButton::clicked, this, &MainWindow::saveRequested);
    connect(m_saveAs, &QPushButton::clicked, this, &MainWindow::saveAsRequested);
    connect(m_addBook, &QPushButton::clicked, this, &MainWindow::addBookRequested);
    connect(m_edit, &QPushButton::clicked, this, &MainWindow::editRequested);
    connect(m_withdraw, &QPushButton::clicked, this, &MainWindow::withdrawRequested);
    connect(m_export, &QPushButton::clicked, this, &MainWindow::exportRequested);
    connect(m_undo, &QPushButton::clicked, this, &MainWindow::undoRequested);
    return bar;
}

QWidget *MainWindow::createSearchRow()
{
    auto *bar = new QWidget;
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(QStringLiteral("Поиск: название, автор, инвентарный номер…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(40);
    {
        // В поле ввода значок рисуется мелко (16 px): берём лупу крупнее, без полей вокруг.
        const QPixmap big = makePixmap(IconKind::Search, 48, QColor(0x6b, 0x77, 0x86), 1.0);
        QIcon searchIcon(big.copy(6, 6, 38, 38));
        m_search->addAction(searchIcon, QLineEdit::LeadingPosition);
    }
    row->addWidget(m_search, 1);

    m_resetFilters = createActionButton(QStringLiteral("Сбросить фильтры"),
                                        QStringLiteral("Показать все записи без фильтров и поиска"));
    m_resetFilters->setMinimumHeight(40);
    m_resetFilters->setIcon(makeIcon(IconKind::ResetFilters));
    row->addWidget(m_resetFilters);

    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::searchTextChanged);
    connect(m_resetFilters, &QPushButton::clicked, this, [this] {
        m_search->clear();
        emit resetFiltersRequested();
    });
    return bar;
}

QWidget *MainWindow::createBooksTab()
{
    m_booksPage = new FundTablePage(QStringLiteral(
        "Программа сделает рабочую копию файла, ваш исходный файл не изменится."));
    m_booksPage->setEmptyTitle(QStringLiteral("Откройте Excel-файл с книжным фондом"));
    auto *open = createActionButton(QStringLiteral("Открыть Excel"), QStringLiteral("Выбрать Excel-файл с фондом"));
    open->setProperty("primary", true);
    open->setIcon(makeIcon(IconKind::Open, Qt::white));
    connect(open, &QPushButton::clicked, this, &MainWindow::openRequested);
    m_booksPage->setEmptyAction(open);
    return m_booksPage;
}

QWidget *MainWindow::createWithdrawnTab()
{
    m_withdrawnPage = new FundTablePage(QStringLiteral(
        "Откройте файл, выделите экземпляры на вкладке «Книги» и нажмите «Выбытие»."));
    m_withdrawnPage->setEmptyTitle(QStringLiteral("Здесь будут выбывшие экземпляры"));
    m_restore = createActionButton(QStringLiteral("Вернуть в фонд"),
                                   QStringLiteral("Отменить выбытие выбранных экземпляров: они вернутся на вкладку «Книги»"));
    m_report = createActionButton(QStringLiteral("Сформировать Excel"),
                                  QStringLiteral("Сохранить список выбывших в новый Excel-файл (отчёт). "
                                                 "Если выбраны строки — только их."));
    m_report->setIcon(makeIcon(IconKind::Report));
    m_restore->setIcon(makeIcon(IconKind::Restore));
    m_withdrawnPage->addButton(m_report);
    m_withdrawnPage->addButton(m_restore);
    connect(m_restore, &QPushButton::clicked, this, &MainWindow::restoreRequested);
    connect(m_report, &QPushButton::clicked, this, &MainWindow::reportRequested);
    return m_withdrawnPage;
}

QTableView *MainWindow::booksTable() const
{
    return m_booksPage->table();
}

FilterHeaderView *MainWindow::booksHeader() const
{
    return m_booksPage->header();
}

bool MainWindow::isWithdrawnTabActive() const
{
    return m_tabs->currentWidget() == m_withdrawnPage;
}

void MainWindow::showWithdrawnTab()
{
    m_tabs->setCurrentWidget(m_withdrawnPage);
}

void MainWindow::setUndoText(const QString &text)
{
    m_undo->setEnabled(!text.isEmpty());
    m_undo->setToolTip(text.isEmpty() ? QStringLiteral("Отменять нечего") : text + QStringLiteral(" (Ctrl+Z)"));
}

QWidget *MainWindow::createImportExportTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    // Экспорт.
    auto *exportTitle = new QLabel(QStringLiteral("Экспорт"), page);
    exportTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(exportTitle);
    auto *exportRow = new QHBoxLayout;
    auto *exportText = new QLabel(QStringLiteral("Выгрузить весь фонд, найденные книги или выбывшие в отдельный файл "
                                                 "Excel или CSV, с выбором столбцов."),
                                  page);
    exportText->setWordWrap(true);
    exportText->setObjectName(QStringLiteral("emptyState"));
    exportRow->addWidget(exportText, 1);
    m_exportTabButton = createActionButton(QStringLiteral("Экспорт…"), QStringLiteral("Выгрузить в Excel или CSV"));
    m_exportTabButton->setIcon(makeIcon(IconKind::Export));
    connect(m_exportTabButton, &QPushButton::clicked, this, &MainWindow::exportRequested);
    exportRow->addWidget(m_exportTabButton);
    layout->addLayout(exportRow);
    layout->addSpacing(8);

    // Прошлые версии.
    auto *versionsTitle = new QLabel(QStringLiteral("Прошлые версии"), page);
    versionsTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(versionsTitle);
    auto *versionsText = new QLabel(
        QStringLiteral("Перед каждым «Сохранить» программа откладывает копию рабочего файла. Хранятся 20 последних "
                       "таких копий; копии перед очисткой и восстановлением не удаляются. Выберите версию и нажмите "
                       "«Вернуть эту версию»: текущий файл тоже сохранится копией, его можно будет вернуть."),
        page);
    versionsText->setWordWrap(true);
    versionsText->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(versionsText);

    m_versions = new QTableWidget(0, 5, page);
    m_versions->setHorizontalHeaderLabels({QStringLiteral("Дата и время"), QStringLiteral("Когда сделана"),
                                           QStringLiteral("Книг в фонде"), QStringLiteral("Выбывших"),
                                           QStringLiteral("Размер")});
    m_versions->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_versions->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_versions->setSelectionMode(QAbstractItemView::SingleSelection);
    m_versions->verticalHeader()->hide();
    m_versions->verticalHeader()->setDefaultSectionSize(m_versions->fontMetrics().height() + 14);
    m_versions->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_versions->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_versions, 1);
    m_versionsEmpty = new QLabel(page);
    m_versionsEmpty->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(m_versionsEmpty);

    auto *buttons = new QHBoxLayout;
    m_restoreVersion = createActionButton(QStringLiteral("Вернуть эту версию"),
                                          QStringLiteral("Заменить рабочий файл выбранной версией"));
    m_restoreVersion->setIcon(makeIcon(IconKind::Restore));
    m_openBackups = createActionButton(QStringLiteral("Открыть папку с копиями"),
                                       QStringLiteral("Показать резервные копии в Проводнике"));
    m_openBackups->setIcon(makeIcon(IconKind::Open));
    buttons->addWidget(m_restoreVersion);
    buttons->addWidget(m_openBackups);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto selectedPath = [this]() -> QString {
        const auto rows = m_versions->selectionModel()->selectedRows();
        if (rows.isEmpty() || rows.first().row() >= m_versionList.size())
            return {};
        return m_versionList.at(rows.first().row()).path;
    };
    connect(m_versions->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this, selectedPath] { m_restoreVersion->setEnabled(!selectedPath().isEmpty()); });
    connect(m_restoreVersion, &QPushButton::clicked, this, [this, selectedPath] {
        const QString path = selectedPath();
        if (!path.isEmpty())
            emit restoreVersionRequested(path);
    });
    connect(m_versions, &QTableWidget::cellDoubleClicked, this, [this, selectedPath](int, int) {
        const QString path = selectedPath();
        if (!path.isEmpty())
            emit restoreVersionRequested(path);
    });
    connect(m_openBackups, &QPushButton::clicked, this, &MainWindow::openBackupFolderRequested);
    setVersions({});
    return page;
}

void MainWindow::setVersions(const QVector<core::WorkCopies::BackupInfo> &versions)
{
    m_versionList = versions;
    m_versions->setRowCount(versions.size());
    const QLocale ru(QLocale::Russian);
    auto count = [&ru](int n) { return n < 0 ? QStringLiteral("—") : ru.toString(n); };
    for (int i = 0; i < versions.size(); ++i) {
        const auto &v = versions.at(i);
        m_versions->setItem(i, 0, new QTableWidgetItem(v.createdAt.toString(QStringLiteral("dd.MM.yyyy  HH:mm"))));
        m_versions->setItem(i, 1, new QTableWidgetItem(v.reason));
        auto *books = new QTableWidgetItem(count(v.books));
        books->setTextAlignment(Qt::AlignCenter);
        m_versions->setItem(i, 2, books);
        auto *withdrawn = new QTableWidgetItem(count(v.withdrawn));
        withdrawn->setTextAlignment(Qt::AlignCenter);
        m_versions->setItem(i, 3, withdrawn);
        auto *size = new QTableWidgetItem(QStringLiteral("%1 КБ").arg(ru.toString((v.size + 1023) / 1024)));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_versions->setItem(i, 4, size);
    }
    m_versions->clearSelection();
    m_restoreVersion->setEnabled(false);
    m_openBackups->setEnabled(!m_workFileName.isEmpty());
    m_versionsEmpty->setVisible(versions.isEmpty());
    m_versionsEmpty->setText(m_workFileName.isEmpty()
                                 ? QStringLiteral("Откройте файл фонда, чтобы увидеть его прошлые версии.")
                                 : QStringLiteral("Прошлых версий пока нет: первая появится при «Сохранить»."));
}

QWidget *MainWindow::createLogTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("Журнал действий"), page);
    title->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(title);
    auto *text = new QLabel(QStringLiteral("Здесь записано, что и когда делалось с фондом: импорт, добавление и "
                                           "изменение книг, выбытие, отчёты, очистка, экспорт, возврат версий и "
                                           "ошибки. Новые записи сверху."),
                            page);
    text->setWordWrap(true);
    text->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(text);

    m_log = new QTableWidget(0, 4, page);
    m_log->setHorizontalHeaderLabels({QStringLiteral("Дата и время"), QStringLiteral("Действие"),
                                      QStringLiteral("Экз."), QStringLiteral("Подробности")});
    m_log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_log->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_log->setWordWrap(false);
    m_log->setTextElideMode(Qt::ElideRight);
    m_log->verticalHeader()->hide();
    m_log->verticalHeader()->setDefaultSectionSize(m_log->fontMetrics().height() + 14);
    m_log->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_log->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_log, 1);
    m_logEmpty = new QLabel(page);
    m_logEmpty->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(m_logEmpty);

    auto *row = new QHBoxLayout;
    m_logCurrentOnly = new QCheckBox(QStringLiteral("Только открытый фонд"), page);
    m_logCurrentOnly->setChecked(true);
    connect(m_logCurrentOnly, &QCheckBox::toggled, this, &MainWindow::logScopeChanged);
    row->addWidget(m_logCurrentOnly);
    row->addStretch(1);
    auto *openFolder = createActionButton(QStringLiteral("Открыть папку журнала"),
                                          QStringLiteral("Показать файл журнала в Проводнике"));
    openFolder->setIcon(makeIcon(IconKind::Open));
    connect(openFolder, &QPushButton::clicked, this, &MainWindow::openLogFolderRequested);
    row->addWidget(openFolder);
    layout->addLayout(row);
    setLog({}, {});
    return page;
}

bool MainWindow::isLogTabActive() const
{
    return m_tabs && m_logTab && m_tabs->currentWidget() == m_logTab;
}

void MainWindow::setLog(const QVector<core::LogEntry> &entries, const QString &fund)
{
    const bool allFunds = fund.isEmpty();
    m_log->setColumnCount(allFunds ? 5 : 4);
    QStringList headers = {QStringLiteral("Дата и время"), QStringLiteral("Действие"), QStringLiteral("Экз."),
                           QStringLiteral("Подробности")};
    if (allFunds)
        headers.insert(3, QStringLiteral("Фонд"));
    m_log->setHorizontalHeaderLabels(headers);
    m_log->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_log->horizontalHeader()->setSectionResizeMode(headers.size() - 1, QHeaderView::Stretch);
    m_log->setRowCount(entries.size());
    const QLocale ru(QLocale::Russian);
    for (int i = 0; i < entries.size(); ++i) {
        const core::LogEntry &e = entries.at(i);
        int c = 0;
        m_log->setItem(i, c++, new QTableWidgetItem(e.time.toString(QStringLiteral("dd.MM.yyyy  HH:mm"))));
        auto *action = new QTableWidgetItem(e.actionTitle());
        if (e.action == QLatin1String("error"))
            action->setForeground(QColor(0xb4, 0x23, 0x18));
        m_log->setItem(i, c++, action);
        auto *count = new QTableWidgetItem(e.count < 0 ? QString() : ru.toString(e.count));
        count->setTextAlignment(Qt::AlignCenter);
        m_log->setItem(i, c++, count);
        if (allFunds)
            m_log->setItem(i, c++, new QTableWidgetItem(e.fund));
        auto *details = new QTableWidgetItem(e.details);
        details->setToolTip(e.details);
        m_log->setItem(i, c++, details);
    }
    m_logEmpty->setVisible(entries.isEmpty());
    m_logEmpty->setText(allFunds ? QStringLiteral("Журнал пока пуст.")
                                 : QStringLiteral("Для этого фонда в журнале пока нет записей."));
    m_logCurrentOnly->setEnabled(!m_workFileName.isEmpty());
}

QWidget *MainWindow::createHelpTab()
{
    auto *help = new QTextBrowser;
    help->setOpenExternalLinks(false);
    help->setHtml(QStringLiteral(
        "<h2>Начало работы</h2>"
        "<p>1. Нажмите <b>«Открыть Excel»</b> и выберите файл с книжным фондом.</p>"
        "<p>2. Программа сделает <b>рабочую копию</b> файла в папке «Документы\\BiblioHelper\\Рабочие файлы». "
        "Ваш исходный файл останется без изменений.</p>"
        "<p>3. Все изменения попадают в рабочую копию после нажатия <b>«Сохранить»</b>.</p>"
        "<h2>Мастер импорта</h2>"
        "<p>Когда файл открывается впервые, программа показывает мастер импорта из трёх шагов:</p>"
        "<p>1. <b>Лист и строка с названиями столбцов.</b> Программа находит их сама и выделяет строку синим. "
        "Если ошиблась, выберите другой лист или щёлкните нужную строку.</p>"
        "<p>2. <b>Что записано в столбцах.</b> Назначение подобрано по названиям («Инв. №», «Цена», «Год»…), "
        "его можно поменять. Столбцы без названия, где ставили отметки при проверке, — «Пометка о наличии».</p>"
        "<p>3. <b>Пометки о наличии.</b> Отметьте, какие пометки значат «книги нет на месте» (по умолчанию "
        "«сп», «нет уч», «не числ»). Такие экземпляры попадут на вкладку «Выбывшие» с причиной "
        "«по пометке в исходном файле». Ошибку можно исправить кнопкой «Вернуть в фонд».</p>"
        "<p>Настройки мастера запоминаются в рабочей копии: при следующем открытии мастер не появится.</p>"
        "<p>Кроме Excel (.xlsx) можно открыть <b>.xls</b> (Excel 97–2003) и <b>.csv</b> (кодировка UTF-8 или "
        "Windows-1251, разделитель «;», «,» или табуляция). Они переводятся в рабочую копию .xlsx; "
        "оформление из .xls не переносится, только значения.</p>"
        "<h2>Экспорт</h2>"
        "<p>Кнопка <b>«Экспорт»</b> сохраняет в отдельный файл весь фонд, найденные сейчас книги (с учётом поиска "
        "и фильтров) или выбывшие. Можно выбрать столбцы и формат: Excel (с итогом, фильтрами и закреплённой "
        "шапкой) или CSV для других программ. Рабочая копия фонда при этом не меняется.</p>"
        "<h2>Прошлые версии</h2>"
        "<p>Перед каждым «Сохранить» программа откладывает копию рабочего файла в «Документы\\BiblioHelper\\"
        "Резервные копии». Хранятся 20 последних таких копий; копии перед очисткой и восстановлением не удаляются.</p>"
        "<p>Чтобы вернуть прошлую версию, откройте вкладку <b>«Импорт и экспорт»</b>, выберите версию по дате и "
        "нажмите <b>«Вернуть эту версию»</b>. Текущий файл программа сначала тоже сохранит копией, так что "
        "возврат можно отменить, вернув её.</p>"
        "<h2>Журнал действий</h2>"
        "<p>На вкладке <b>«Журнал»</b> записано, что и когда делалось: открытие и импорт файла, добавление и "
        "изменение книг (что было и что стало), выбытие и возврат в фонд, сохранение, отчёты, очистка, экспорт, "
        "возврат прошлых версий и ошибки. Галочка «Только открытый фонд» показывает записи текущего файла; "
        "снимите её, чтобы увидеть все. Журнал хранится в файле «Документы\\BiblioHelper\\Журнал\\журнал.jsonl»; "
        "его можно приложить к сообщению о проблеме.</p>"
        "<h2>Поиск и фильтры</h2>"
        "<p><b>Поиск</b>: начните вводить название, автора или инвентарный номер в строке поиска. "
        "Можно вводить несколько слов: «абгарян яблоки». Большие и маленькие буквы, «е» и «ё» не различаются.</p>"
        "<p><b>Сортировка</b>: щёлкните по названию столбца. Второй щелчок — в обратном порядке.</p>"
        "<p><b>Фильтр</b>: нажмите значок <b>▼</b> справа в названии столбца и отметьте нужные значения, "
        "как в Excel. Столбец с фильтром подсвечен жёлтым.</p>"
        "<p>В окне фильтра есть <b>условия</b>: «содержит», «начинается с», «больше», «меньше», "
        "«между» (например, год выпуска между 2000 и 2010), «пусто».</p>"
        "<p>Книги, выделенные в Excel <b>красным</b> шрифтом (ветхие) или <b>синим</b> (устаревшие), "
        "показываются тем же цветом. В столбце <b>«Цветовая пометка»</b> написано «Ветхая» или "
        "«Устаревшая» — по нему можно отфильтровать, например, все ветхие книги.</p>"
        "<p>Кнопка <b>«Сбросить фильтры»</b> снова показывает все книги.</p>"
        "<p>Слева у каждой строки — её номер в Excel-файле.</p>"
        "<h2>Выбытие</h2>"
        "<p>1. На вкладке «Книги» выделите экземпляры, которые выбывают. Несколько строк — щелчками "
        "с нажатой клавишей <b>Ctrl</b>, подряд — с <b>Shift</b>.</p>"
        "<p>2. Нажмите <b>«Выбытие»</b>, проверьте список, укажите дату и причину, при необходимости примечание "
        "(например, номер акта), и нажмите «Оформить выбытие».</p>"
        "<p>3. Экземпляры уйдут с вкладки «Книги» на вкладку <b>«Выбывшие»</b>. Из файла они не удаляются: "
        "в Excel у них появится столбец <b>«Статус»</b> с текстом «Выбыло 23.11.2026, ветхость».</p>"
        "<p>Ошиблись — нажмите «Отменить» (Ctrl+Z) или на вкладке «Выбывшие» выберите экземпляр и нажмите "
        "<b>«Вернуть в фонд»</b>.</p>"
        "<p><b>«Сформировать Excel»</b> на вкладке «Выбывшие» сохраняет отчёт: каждый экземпляр отдельной строкой, "
        "с датой, причиной и итоговой суммой. Если выделены строки — в отчёт войдут только они, иначе все показанные.</p>"
        "<p>После отчёта программа спросит, <b>очистить ли выбывшие</b>. Если согласиться (и подтвердить ещё раз), "
        "она сохранит изменения, сделает резервную копию в папке «Документы\\BiblioHelper\\Резервные копии» "
        "и перенесёт строки, вошедшие в отчёт, на лист <b>«Архив выбывших»</b> той же рабочей копии. "
        "Безвозвратно ничего не удаляется.</p>"
        "<p>Подробная справка появится в следующих версиях программы.</p>"));
    return help;
}

void MainWindow::setWorkFileName(const QString &fileName)
{
    m_workFileName = fileName;
    m_fileLabel->setText(fileName.isEmpty()
                             ? QStringLiteral("Рабочий файл не открыт")
                             : QStringLiteral("Рабочий файл: %1").arg(fileName));
    updateDocumentActions();
}

void MainWindow::setUnsavedChanges(bool unsaved)
{
    m_unsaved = unsaved;
    // Состояние передаётся словами, а не только цветом.
    m_unsavedLabel->setText(unsaved ? QStringLiteral("Есть несохранённые изменения")
                                    : (m_workFileName.isEmpty() ? QString()
                                                                : QStringLiteral("Все изменения сохранены")));
    m_unsavedLabel->setProperty("unsaved", unsaved);
    m_unsavedLabel->style()->unpolish(m_unsavedLabel);
    m_unsavedLabel->style()->polish(m_unsavedLabel);
    // Подпись в плашке жирная: ширину считаем по жирному шрифту, иначе текст обрезается.
    QFont bold = m_unsavedLabel->font();
    bold.setBold(true);
    m_unsavedLabel->setMinimumWidth(QFontMetrics(bold).horizontalAdvance(m_unsavedLabel->text()) + 32);
    m_unsavedLabel->updateGeometry();
    updateDocumentActions();
}

void MainWindow::setPrimary(QPushButton *button, bool primary)
{
    if (button->property("primary").toBool() == primary)
        return;
    button->setProperty("primary", primary);
    const bool open = button == m_open;
    button->setIcon(makeIcon(open ? IconKind::Open : IconKind::Save,
                             primary ? QColor(Qt::white) : QColor(0x2f, 0x3b, 0x4c)));
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void MainWindow::updateDocumentActions()
{
    const bool hasDocument = !m_workFileName.isEmpty();
    for (auto *button : {m_saveAs, m_addBook, m_edit, m_withdraw, m_export, m_exportTabButton, m_resetFilters, m_restore, m_report})
        button->setEnabled(hasDocument);
    m_save->setEnabled(hasDocument && m_unsaved);
    m_search->setEnabled(hasDocument);
    // Главная кнопка — цветом: без файла «Открыть Excel», с изменениями «Сохранить».
    setPrimary(m_open, !hasDocument);
    setPrimary(m_save, hasDocument && m_unsaved);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_unsaved) {
        event->accept();
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Несохранённые изменения"));
    box.setText(QStringLiteral("В рабочем файле есть несохранённые изменения. Что сделать?"));
    auto *save = box.addButton(QStringLiteral("Сохранить"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("Не сохранять"), QMessageBox::DestructiveRole);
    auto *cancel = box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();

    if (box.clickedButton() == save) {
        emit saveRequested();
        // Если сохранить не удалось, флаг останется и окно не закроется.
        if (m_unsaved) {
            event->ignore();
            return;
        }
        event->accept();
    } else if (box.clickedButton() == discard) {
        event->accept();
    } else {
        Q_UNUSED(cancel);
        event->ignore();
    }
}

} // namespace bh::ui
