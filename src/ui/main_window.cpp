#include "main_window.h"

#include "filter_header_view.h"
#include "fund_table_page.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
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
    fileRow->addWidget(m_unsavedLabel);
    layout->addLayout(fileRow);

    layout->addWidget(createToolbar());
    layout->addWidget(createSearchRow());

    m_tabs = new QTabWidget(central);
    m_tabs->addTab(createBooksTab(), QStringLiteral("Книги"));
    m_tabs->addTab(createWithdrawnTab(), QStringLiteral("Выбывшие"));
    m_tabs->addTab(createImportExportTab(), QStringLiteral("Импорт и экспорт"));
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
    m_edit = createActionButton(QStringLiteral("Редактировать"),
                                QStringLiteral("Изменить выбранную книгу или экземпляр"));
    m_withdraw = createActionButton(QStringLiteral("Выбытие"),
                                    QStringLiteral("Оформить выбытие отмеченных экземпляров"));
    m_export = createActionButton(QStringLiteral("Экспорт Excel"),
                                  QStringLiteral("Сохранить список в новый Excel-файл"));

    m_undo = createActionButton(QStringLiteral("Отменить"),
                                QStringLiteral("Отменить последнее изменение (до сохранения)"));
    m_undo->setShortcut(QKeySequence::Undo);
    m_undo->setEnabled(false);

    for (auto *button : {m_open, m_save, m_saveAs, m_addBook, m_edit, m_undo, m_withdraw, m_export})
        row->addWidget(button);

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
    row->addWidget(m_search, 1);

    m_resetFilters = createActionButton(QStringLiteral("Сбросить фильтры"),
                                        QStringLiteral("Показать все записи без фильтров и поиска"));
    m_resetFilters->setMinimumHeight(40);
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
        "Здесь появится список книг.\n\nНажмите «Открыть Excel» и выберите файл с книжным фондом."));
    return m_booksPage;
}

QWidget *MainWindow::createWithdrawnTab()
{
    m_withdrawnPage = new FundTablePage(QStringLiteral(
        "Здесь будут выбывшие экземпляры.\n\nОткройте файл, выберите экземпляры на вкладке «Книги» "
        "и нажмите «Выбытие»."));
    m_restore = createActionButton(QStringLiteral("Вернуть в фонд"),
                                   QStringLiteral("Отменить выбытие выбранных экземпляров: они вернутся на вкладку «Книги»"));
    m_report = createActionButton(QStringLiteral("Сформировать Excel"),
                                  QStringLiteral("Сохранить список выбывших в новый Excel-файл (отчёт). "
                                                 "Если выбраны строки — только их."));
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
    return createEmptyState(QStringLiteral(
        "Здесь будут импорт из нескольких файлов, восстановление прошлых версий и экспорт."));
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
    m_unsavedLabel->setText(unsaved ? QStringLiteral("● Есть несохранённые изменения")
                                    : (m_workFileName.isEmpty() ? QString()
                                                                : QStringLiteral("Все изменения сохранены")));
    m_unsavedLabel->setProperty("unsaved", unsaved);
    m_unsavedLabel->style()->unpolish(m_unsavedLabel);
    m_unsavedLabel->style()->polish(m_unsavedLabel);
    updateDocumentActions();
}

void MainWindow::updateDocumentActions()
{
    const bool hasDocument = !m_workFileName.isEmpty();
    for (auto *button : {m_saveAs, m_addBook, m_edit, m_withdraw, m_export, m_resetFilters, m_restore, m_report})
        button->setEnabled(hasDocument);
    m_save->setEnabled(hasDocument && m_unsaved);
    m_search->setEnabled(hasDocument);
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
