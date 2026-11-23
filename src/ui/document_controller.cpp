#include "document_controller.h"

#include "core/import_layout.h"
#include "ui/import_wizard.h"

#include "main_window.h"

#include "core/copy_ids.h"
#include "core/fund_sheet.h"
#include "core/withdrawal_archive.h"
#include "core/withdrawal_report.h"
#include "fund_table_page.h"
#include "record_dialog.h"
#include "withdraw_dialog.h"
#include "io/xlsx_font_colors.h"
#include "filter_header_view.h"
#include "filter_popup.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QApplication>
#include <QLocale>
#include <QMap>
#include <QScreen>
#include <QStatusBar>
#include <QTableView>

namespace bh::ui {

namespace {

const QString kLastDirKey = QStringLiteral("files/lastDirectory");

bool samePath(const QString &a, const QString &b)
{
    const QString ca = QFileInfo(a).canonicalFilePath();
    const QString cb = QFileInfo(b).canonicalFilePath();
    if (ca.isEmpty() || cb.isEmpty())
        return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
    return ca.compare(cb, Qt::CaseInsensitive) == 0;
}

} // namespace

DocumentController::DocumentController(MainWindow *window)
    : QObject(window)
    , m_window(window)
    , m_model(new models::FundTableModel(this))
    , m_proxy(new models::FundFilterProxy(this))
    , m_withdrawnProxy(new models::FundFilterProxy(this))
{
    m_proxy->setSourceModel(m_model);
    m_proxy->setStatusFilter(models::FundFilterProxy::StatusFilter::Active);
    m_withdrawnProxy->setSourceModel(m_model);
    m_withdrawnProxy->setStatusFilter(models::FundFilterProxy::StatusFilter::Withdrawn);

    connect(window, &MainWindow::openRequested, this, &DocumentController::open);
    connect(window, &MainWindow::saveRequested, this, &DocumentController::save);
    connect(window, &MainWindow::saveAsRequested, this, &DocumentController::saveAs);

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(250);
    connect(window, &MainWindow::searchTextChanged, this, [this](const QString &text) {
        m_pendingSearch = text;
        m_searchTimer.start();
    });
    connect(&m_searchTimer, &QTimer::timeout, this, [this] {
        m_proxy->setSearchText(m_pendingSearch);
        m_withdrawnProxy->setSearchText(m_pendingSearch);
    });
    connect(window, &MainWindow::resetFiltersRequested, this, [this] {
        m_searchTimer.stop();
        for (FundTablePage *page : {m_window->booksPage(), m_window->withdrawnPage()}) {
            page->table()->sortByColumn(-1, Qt::AscendingOrder);
            page->header()->setSortIndicator(-1, Qt::AscendingOrder);
        }
        m_proxy->clearAllFilters();
        m_withdrawnProxy->clearAllFilters();
    });

    const QVector<QPair<FundTablePage *, models::FundFilterProxy *>> views{
        {window->booksPage(), m_proxy}, {window->withdrawnPage(), m_withdrawnProxy}};
    for (const auto &view : views) {
        FundTablePage *page = view.first;
        models::FundFilterProxy *proxy = view.second;
        connect(page->header(), &FilterHeaderView::filterRequested, this,
                [this, page, proxy](int column, const QPoint &pos) { showColumnFilter(page, proxy, column, pos); });
        connect(proxy, &models::FundFilterProxy::filtersChanged, this, &DocumentController::updateCounts);
        connect(proxy, &QAbstractItemModel::rowsInserted, this, &DocumentController::updateCounts);
        connect(proxy, &QAbstractItemModel::rowsRemoved, this, &DocumentController::updateCounts);
        connect(proxy, &QAbstractItemModel::modelReset, this, &DocumentController::updateCounts);
        // Изменённые данные могут выпасть из фильтра: прокси пересчитывает строку сама.
        proxy->setDynamicSortFilter(true);
        connect(page->table(), &QAbstractItemView::doubleClicked, this, [this, proxy](const QModelIndex &index) {
            editRow(proxy->mapToSource(index).row());
        });
    }

    connect(window, &MainWindow::addBookRequested, this, &DocumentController::addBook);
    connect(window, &MainWindow::editRequested, this, &DocumentController::editSelected);
    connect(window, &MainWindow::undoRequested, this, &DocumentController::undo);
    connect(window, &MainWindow::withdrawRequested, this, &DocumentController::withdrawSelected);
    connect(window, &MainWindow::restoreRequested, this, &DocumentController::restoreSelected);
    connect(window, &MainWindow::reportRequested, this, &DocumentController::createReport);
    // Ширину столбцов «Выбывших» подбираем, когда вкладка видна: до этого у таблицы нет размера.
    connect(window, &MainWindow::withdrawnTabShown, this, [this] {
        if (m_fitWithdrawnPending && m_window->withdrawnPage()->isTableShown()) {
            m_fitWithdrawnPending = false;
            fitColumns(m_window->withdrawnPage()->table());
        }
    });
    connect(m_model, &models::FundTableModel::modifiedChanged, this, [this](bool modified) {
        m_window->setUnsavedChanges(modified);
    });
    connect(m_model, &models::FundTableModel::undoChanged, this,
            [this] { m_window->setUndoText(m_model->undoText()); });
    // Правка цены или выбытие меняют сумму.
    connect(m_model, &QAbstractItemModel::dataChanged, this, &DocumentController::updateCounts);
}

QString DocumentController::duplicateInventoryText(const QString &inventory, const QString &exceptId) const
{
    const int inv = m_model->sheet().columnIndex(core::ColumnRole::Inventory);
    const int author = m_model->sheet().columnIndex(core::ColumnRole::AuthorTitle);
    bool isNumber = false;
    inventory.toLongLong(&isNumber);
    if (inv < 0 || !isNumber)  // «б/н», «бр» повторяются у многих — не предупреждаем
        return {};
    QStringList same;
    for (int r = 0; r < m_model->rowCount() && same.size() < 3; ++r) {
        const QModelIndex index = m_model->index(r, inv);
        if (index.data().toString() == inventory && index.data(models::FundTableModel::IdRole).toString() != exceptId)
            same << QStringLiteral("«%1» (строка %2)")
                        .arg(author >= 0 ? m_model->index(r, author).data().toString().left(50) : QString())
                        .arg(index.data(models::FundTableModel::ExcelRowRole).toInt());
    }
    if (same.isEmpty())
        return {};
    return QStringLiteral("Такой инвентарный номер уже есть: %1. Сохранить всё равно можно.")
        .arg(same.join(QStringLiteral(", ")));
}

void DocumentController::selectRecord(const QString &id)
{
    const int row = m_model->rowForId(id);
    if (row < 0)
        return;
    const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(row, 0));
    QTableView *table = m_window->booksTable();
    if (!proxyIndex.isValid()) {
        m_window->statusBar()->showMessage(
            QStringLiteral("Книга сохранена, но сейчас скрыта поиском или фильтром. «Сбросить фильтры» покажет её."),
            10000);
        return;
    }
    table->selectRow(proxyIndex.row());
    table->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
}

void DocumentController::addBook()
{
    if (!m_workbook.isOpen() || m_model->columnCount() == 0)
        return;
    const core::FundSheet &sheet = m_model->sheet();
    QVector<QVariant> values(sheet.columns.size());
    // Номер по порядку — следующий после самого большого; количество — 1.
    const int number = sheet.columnIndex(core::ColumnRole::Number);
    if (number >= 0) {
        qlonglong max = 0;
        for (const auto &record : sheet.records)
            max = std::max(max, record.values.at(number).toLongLong());
        values[number] = max + 1;
    }
    const int quantity = sheet.columnIndex(core::ColumnRole::Quantity);
    if (quantity >= 0)
        values[quantity] = 1;

    RecordDialog dialog(sheet, values, 0, m_window);
    dialog.setDuplicateCheck([this](const QString &inv) { return duplicateInventoryText(inv, {}); });
    if (dialog.exec() != QDialog::Accepted)
        return;
    const int row = m_model->addRecord(dialog.values());
    selectRecord(m_model->index(row, 0).data(models::FundTableModel::IdRole).toString());
    m_window->statusBar()->showMessage(QStringLiteral("Книга добавлена. Нажмите «Сохранить», чтобы записать её в Excel."), 10000);
}

QVector<int> DocumentController::selectedModelRows(QTableView *table, models::FundFilterProxy *proxy) const
{
    QVector<int> result;
    if (!table->selectionModel())
        return result;
    QModelIndexList rows = table->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &index : qAsConst(rows))
        result.push_back(proxy->mapToSource(index).row());
    return result;
}

void DocumentController::editSelected()
{
    const bool withdrawnTab = m_window->isWithdrawnTabActive();
    QTableView *table = withdrawnTab ? m_window->withdrawnPage()->table() : m_window->booksTable();
    models::FundFilterProxy *proxy = withdrawnTab ? m_withdrawnProxy : m_proxy;
    const QVector<int> rows = selectedModelRows(table, proxy);
    if (rows.size() != 1) {
        QMessageBox::information(m_window, QStringLiteral("Изменить"),
                                 rows.isEmpty() ? QStringLiteral("Сначала щёлкните по строке с книгой, которую нужно изменить.")
                                                : QStringLiteral("Выбрано несколько строк. Выберите одну книгу."));
        return;
    }
    editRow(rows.first());
}

void DocumentController::editRow(int modelRow)
{
    if (modelRow < 0 || modelRow >= m_model->rowCount())
        return;
    // Диалог работает с копией и ID, а не с номером строки: сортировка и
    // фильтры, пока он открыт, не могут подменить книгу.
    const QString id = m_model->index(modelRow, 0).data(models::FundTableModel::IdRole).toString();
    const core::FundRecord record = m_model->sheet().records.at(modelRow);
    RecordDialog dialog(m_model->sheet(), record.values, record.excelRow, m_window);
    dialog.setDuplicateCheck([this, id](const QString &inv) { return duplicateInventoryText(inv, id); });
    if (dialog.exec() != QDialog::Accepted)
        return;
    const int row = m_model->rowForId(id);
    if (row < 0)
        return;
    if (m_model->updateRecord(row, dialog.values())) {
        if (!m_model->isWithdrawn(row))
            selectRecord(id);
        m_window->statusBar()->showMessage(QStringLiteral("Изменения внесены. Нажмите «Сохранить», чтобы записать их в Excel."), 10000);
    }
}

void DocumentController::undo()
{
    if (!m_model->canUndo())
        return;
    m_model->undo();
    m_window->statusBar()->showMessage(QStringLiteral("Последнее изменение отменено"), 5000);
}

QString DocumentController::countText(models::FundFilterProxy *proxy, bool withdrawn) const
{
    const core::FundSheet &sheet = m_model->sheet();
    const int price = sheet.columnIndex(core::ColumnRole::Price);
    const auto totals = proxy->totals(price, sheet.columnIndex(core::ColumnRole::Quantity));
    int total = 0;
    for (int r = 0; r < m_model->rowCount(); ++r)
        total += m_model->isWithdrawn(r) == withdrawn ? 1 : 0;

    QString sum;
    if (price >= 0) {
        sum = QStringLiteral(" на сумму %1 руб.").arg(QLocale(QLocale::Russian).toString(totals.sum, 'f', 2));
        if (totals.withoutPrice > 0)
            sum += QStringLiteral(" (без цены: %1)").arg(totals.withoutPrice);
    }
    const QString what = withdrawn ? QStringLiteral("выбывших экземпляров") : QStringLiteral("экземпляров");
    if (proxy->hasAnyFilter())
        return QStringLiteral("Показано %1 из %2 %3%4. Включены поиск или фильтры — «Сбросить фильтры» покажет все.")
            .arg(totals.rows).arg(total).arg(what, sum);
    if (withdrawn && total == 0)
        return QStringLiteral("Выбывших экземпляров нет. Выбытие оформляется кнопкой «Выбытие» на вкладке «Книги».");
    return QStringLiteral("Всего %1: %2%3").arg(what).arg(total).arg(sum);
}

void DocumentController::updateCounts()
{
    const QVector<QPair<FundTablePage *, models::FundFilterProxy *>> views{
        {m_window->booksPage(), m_proxy}, {m_window->withdrawnPage(), m_withdrawnProxy}};
    for (const auto &view : views) {
        QList<int> filtered;
        for (int c = 0; c < m_model->columnCount(); ++c) {
            if (view.second->isColumnFiltered(c))
                filtered << c;
        }
        view.first->header()->setFilteredColumns(filtered);
        view.first->setCountText(countText(view.second, view.second == m_withdrawnProxy),
                                 view.second->hasAnyFilter());
    }
}

void DocumentController::updateColumnVisibility()
{
    using core::ColumnRole;
    const core::FundSheet &sheet = m_model->sheet();
    QTableView *books = m_window->booksTable();
    QTableView *withdrawn = m_window->withdrawnPage()->table();
    for (int c = 0; c < sheet.columns.size(); ++c) {
        const ColumnRole role = sheet.columns.at(c).role;
        const bool withdrawalInfo = role == ColumnRole::WithdrawDate || role == ColumnRole::WithdrawReason
            || role == ColumnRole::WithdrawNote;
        // «Статус» дублирует дату и причину — он только для Excel.
        books->setColumnHidden(c, withdrawalInfo || role == ColumnRole::Status);
        withdrawn->setColumnHidden(c, role == ColumnRole::Status || role == ColumnRole::Quantity
                                          || role == ColumnRole::Mark);
    }
    // «Выбывшие»: дата, причина и примечание сразу после инв. номера, как в отчёте.
    QHeaderView *header = withdrawn->horizontalHeader();
    int after = header->visualIndex(std::max(sheet.columnIndex(ColumnRole::Inventory),
                                             sheet.columnIndex(ColumnRole::AuthorTitle)));
    for (ColumnRole role : {ColumnRole::WithdrawDate, ColumnRole::WithdrawReason, ColumnRole::WithdrawNote}) {
        const int c = sheet.columnIndex(role);
        if (c < 0 || after < 0)
            continue;
        header->moveSection(header->visualIndex(c), ++after);
    }
}

void DocumentController::withdrawSelected()
{
    if (!m_workbook.isOpen() || m_model->columnCount() == 0)
        return;
    if (m_window->isWithdrawnTabActive()) {
        QMessageBox::information(m_window, QStringLiteral("Выбытие"),
                                 QStringLiteral("Выбытие оформляется на вкладке «Книги»: выделите там экземпляры "
                                                "и нажмите «Выбытие».\n\nЧтобы вернуть экземпляр в фонд, выберите его "
                                                "здесь и нажмите «Вернуть в фонд»."));
        return;
    }
    const QVector<int> rows = selectedModelRows(m_window->booksTable(), m_proxy);
    if (rows.isEmpty()) {
        QMessageBox::information(m_window, QStringLiteral("Выбытие"),
                                 QStringLiteral("Сначала выделите в списке экземпляры, которые выбывают.\n\n"
                                                "Несколько строк — щелчками с нажатой клавишей Ctrl, "
                                                "подряд — с клавишей Shift."));
        return;
    }
    WithdrawDialog dialog(m_model->sheet(), rows, m_window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const int count = m_model->withdraw(rows, dialog.date(), dialog.reason(), dialog.note());
    m_window->booksTable()->clearSelection();
    m_window->statusBar()->showMessage(
        QStringLiteral("Оформлено выбытие %1 экз. Они на вкладке «Выбывшие». "
                       "Нажмите «Сохранить», чтобы записать изменения в Excel.").arg(count),
        12000);
}

void DocumentController::restoreSelected()
{
    QTableView *table = m_window->withdrawnPage()->table();
    const QVector<int> rows = selectedModelRows(table, m_withdrawnProxy);
    if (rows.isEmpty()) {
        QMessageBox::information(m_window, QStringLiteral("Вернуть в фонд"),
                                 QStringLiteral("Сначала выделите в списке выбывших экземпляры, которые нужно вернуть."));
        return;
    }
    const int count = m_model->restore(rows);
    m_window->statusBar()->showMessage(
        QStringLiteral("Возвращено в фонд: %1 экз. Они снова на вкладке «Книги».").arg(count), 10000);
}

void DocumentController::createReport()
{
    if (!m_workbook.isOpen())
        return;
    QTableView *table = m_window->withdrawnPage()->table();
    QVector<int> rows = selectedModelRows(table, m_withdrawnProxy);
    const bool onlySelected = !rows.isEmpty();
    if (rows.isEmpty()) {
        // Все показанные, в порядке на экране (с учётом сортировки и фильтров).
        for (int r = 0; r < m_withdrawnProxy->rowCount(); ++r)
            rows.push_back(m_withdrawnProxy->mapToSource(m_withdrawnProxy->index(r, 0)).row());
    }
    if (rows.isEmpty()) {
        QMessageBox::information(m_window, QStringLiteral("Сформировать Excel"),
                                 QStringLiteral("В списке выбывших нет экземпляров для отчёта."));
        return;
    }

    QDir reports(core::WorkCopies::reportsDirectory());
    reports.mkpath(QStringLiteral("."));
    const QString suggested = reports.filePath(
        QStringLiteral("Выбывшие %1.xlsx").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
    QString path = QFileDialog::getSaveFileName(
        m_window,
        onlySelected ? QStringLiteral("Отчёт по выбранным выбывшим (%1 экз.)").arg(rows.size())
                     : QStringLiteral("Отчёт по выбывшим (%1 экз.)").arg(rows.size()),
        QDir::cleanPath(suggested), QStringLiteral("Файлы Excel (*.xlsx)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".xlsx");
    if (samePath(path, m_originalPath) || samePath(path, m_workPath)) {
        showError(QStringLiteral("Сформировать Excel"),
                  QStringLiteral("Этот файл — ваш фонд. Отчёт сохраняется в отдельный файл: выберите другое имя."));
        return;
    }

    core::WithdrawalReport report;
    report.fundName = QFileInfo(m_originalPath).completeBaseName();
    report.createdOn = QDate::currentDate();
    report.records = rows;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = report.write(path, m_model->sheet(), &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        showError(QStringLiteral("Не удалось сформировать Excel"), error);
        return;
    }

    // ID экземпляров, вошедших в отчёт: очистка работает только по ним.
    QStringList reportedIds;
    for (int row : qAsConst(rows)) {
        if (m_model->isWithdrawn(row))
            reportedIds << m_model->index(row, 0).data(models::FundTableModel::IdRole).toString();
    }

    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Сформировать Excel"));
    box.setText(QStringLiteral("Excel-файл успешно сформирован: %1 экз.\n%2\n\n"
                               "Вы хотите очистить выбывшие экземпляры из текущего рабочего списка?")
                    .arg(reportedIds.size())
                    .arg(QDir::toNativeSeparators(path)));
    auto *clean = box.addButton(QStringLiteral("Да, очистить"), QMessageBox::YesRole);
    auto *keep = box.addButton(QStringLiteral("Нет, оставить"), QMessageBox::NoRole);
    box.setDefaultButton(keep);
    auto *openCheck = new QCheckBox(QStringLiteral("Открыть отчёт"), &box);
    box.setCheckBox(openCheck);
    box.exec();
    if (openCheck->isChecked())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    if (box.clickedButton() == clean)
        cleanupReported(reportedIds);
}

void DocumentController::cleanupReported(const QStringList &ids)
{
    if (ids.isEmpty())
        return;
    QMessageBox confirm(m_window);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(QStringLiteral("Очистка выбывших"));
    confirm.setText(QStringLiteral(
        "Будет очищено %1 экз. — только те, что вошли в отчёт.\n\n"
        "Их строки перейдут с листа фонда на лист «%2» в рабочей копии, а на вкладке «Выбывшие» их "
        "больше не будет. Остальные книги не изменятся.\n\n"
        "Перед этим программа сохранит изменения и сделает резервную копию рабочего файла. "
        "Отменить очистку кнопкой «Отменить» нельзя, только восстановлением резервной копии.\n\n"
        "Очистить?")
                        .arg(ids.size())
                        .arg(core::archiveSheetName()));
    auto *yes = confirm.addButton(QStringLiteral("Очистить %1 экз.").arg(ids.size()), QMessageBox::AcceptRole);
    auto *no = confirm.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    confirm.setDefaultButton(no);
    confirm.exec();
    if (confirm.clickedButton() != yes)
        return;

    // 1. Несохранённые изменения — в файл, чтобы строки в Excel и в программе совпадали.
    QString error;
    if (m_model->isModified()) {
        save();
        if (m_model->isModified())
            return;  // сохранить не удалось, сообщение уже показано
    }
    // 2. Резервная копия.
    const QString backup = core::WorkCopies::backup(m_workPath, QStringLiteral("перед очисткой"), &error);
    if (backup.isEmpty()) {
        showError(QStringLiteral("Очистка не выполнена"), error);
        return;
    }
    // 3. Перенос в архив и сохранение.
    core::FundSheet sheet = m_model->sheet();
    QVector<int> records;
    for (const QString &id : ids) {
        const int row = m_model->rowForId(id);
        if (row >= 0)
            records << row;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int moved = core::WithdrawalArchive::archive(m_workbook, &sheet, records, &error);
    const bool saved = moved >= 0 && m_workbook.saveAs(m_workPath, &error);
    QApplication::restoreOverrideCursor();
    if (!saved) {
        // Файл на диске прежний: перечитываем его, чтобы в памяти не осталось полуготовых правок.
        QString reopenError;
        m_workbook.open(m_workPath, &reopenError);
        showError(QStringLiteral("Очистка не выполнена"),
                  QStringLiteral("%1\n\nРабочий файл не изменён. Если он открыт в Excel, закройте его и "
                                 "попробуйте ещё раз.").arg(error));
        return;
    }
    m_model->setSheet(std::move(sheet));
    m_window->setUnsavedChanges(false);
    QMessageBox::information(
        m_window, QStringLiteral("Очистка выбывших"),
        QStringLiteral("Очищено %1 экз. Их строки — на листе «%2» рабочей копии.\n\nРезервная копия до очистки:\n%3")
            .arg(moved)
            .arg(core::archiveSheetName(), QDir::toNativeSeparators(backup)));
}

void DocumentController::showColumnFilter(FundTablePage *page, models::FundFilterProxy *proxy, int column,
                                          const QPoint &globalPos)
{
    if (column < 0 || column >= m_model->columnCount())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto values = proxy->columnValues(column);
    QApplication::restoreOverrideCursor();

    const models::ColumnCondition condition = proxy->columnCondition(column);
    FilterPopup popup(m_model->sheet().columns.at(column).title, values, proxy->columnFilter(column),
                      proxy->hasColumnFilter(column),
                      proxy->hasColumnCondition(column) ? &condition : nullptr, m_window);
    // Окно — под заголовком столбца, но в пределах экрана.
    popup.adjustSize();
    QPoint pos = globalPos;
    if (const QScreen *screen = QApplication::screenAt(globalPos)) {
        const QRect area = screen->availableGeometry();
        pos.setX(std::min(pos.x(), area.right() - popup.width()));
        pos.setY(std::min(pos.y(), area.bottom() - popup.height()));
        pos.setX(std::max(pos.x(), area.left()));
        pos.setY(std::max(pos.y(), area.top()));
    }
    popup.move(pos);
    if (popup.exec() != QDialog::Accepted)
        return;

    QTableView *table = page->table();
    switch (popup.result()) {
    case FilterPopup::Result::Apply:
        if (popup.allChecked())
            proxy->clearColumnFilter(column);
        else
            proxy->setColumnFilter(column, popup.checkedValues());
        break;
    case FilterPopup::Result::ApplyCondition:
        proxy->setColumnCondition(column, popup.condition());
        break;
    case FilterPopup::Result::Clear:
        proxy->clearColumnFilter(column);
        break;
    case FilterPopup::Result::SortAscending:
        table->sortByColumn(column, Qt::AscendingOrder);
        break;
    case FilterPopup::Result::SortDescending:
        table->sortByColumn(column, Qt::DescendingOrder);
        break;
    case FilterPopup::Result::Cancelled:
        break;
    }
}

void DocumentController::open()
{
    if (!confirmDiscardChanges())
        return;

    QSettings settings;
    const QString startDir = settings.value(kLastDirKey,
                                            QDir::homePath()).toString();
    const QString path = QFileDialog::getOpenFileName(
        m_window, QStringLiteral("Выберите Excel-файл с книжным фондом"), startDir,
        QStringLiteral("Файлы Excel и CSV (*.xlsx *.xls *.csv);;Все файлы (*)"));
    if (path.isEmpty())
        return;
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    openFile(path);
}

bool DocumentController::openFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xls")) {
        QMessageBox box(QMessageBox::Information, QStringLiteral("Старый формат Excel"),
                        QStringLiteral("Это файл Excel 97–2003 (.xls). Программа перенесёт данные в новый файл "
                                       ".xlsx (рабочую копию) и дальше будет работать с ним.\n\n"
                                       "Переносятся только значения ячеек: шрифты, цвета, ширина столбцов и "
                                       "рамки не переносятся. Исходный файл не изменится."),
                        QMessageBox::NoButton, m_window);
        auto *proceed = box.addButton(QStringLiteral("Продолжить"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
        box.setDefaultButton(proceed);
        box.exec();
        if (box.clickedButton() != proceed)
            return false;
    }

    QString error;
    const QString workPath = m_workCopies.create(path, &error);
    if (workPath.isEmpty()) {
        showError(QStringLiteral("Не удалось открыть файл"), error);
        return false;
    }

    // Файл, который программа видит впервые (нет сохранённых настроек чтения),
    // открывается через мастер импорта. Мастер читает свою копию книги, чтобы
    // «Отмена» не закрывала уже открытый документ.
    bool imported = false;
    core::ImportLayout layout;
    QSet<QString> withdrawnMarks;
    {
        io::XlsxWorkbook probe;
        if (!probe.open(workPath, &error)) {
            QFile::remove(workPath);
            showError(QStringLiteral("Не удалось открыть файл"),
                      QStringLiteral("Файл «%1» не удалось прочитать как Excel-файл.\n\n%2")
                          .arg(QFileInfo(path).fileName(), error));
            return false;
        }
        layout = core::ImportLayout::load(probe);
        if (!layout.isValid() || !probe.sheetNames().contains(layout.sheet)) {
            ImportWizard wizard(probe, QFileInfo(path).fileName(), m_window);
            if (wizard.exec() != QDialog::Accepted) {
                probe.close();
                QFile::remove(workPath);
                m_window->statusBar()->showMessage(QStringLiteral("Импорт отменён"), 5000);
                return false;
            }
            layout = wizard.layout();
            withdrawnMarks = wizard.withdrawnMarks();
            imported = true;
        }
    }

    if (!m_workbook.open(workPath, &error)) {
        QFile::remove(workPath);
        showError(QStringLiteral("Не удалось открыть файл"),
                  QStringLiteral("Файл «%1» не удалось прочитать как Excel-файл.\n\n%2")
                      .arg(QFileInfo(path).fileName(), error));
        return false;
    }

    m_originalPath = path;
    m_workPath = workPath;
    m_window->setWorkFileName(QFileInfo(workPath).fileName());
    m_window->setUnsavedChanges(false);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    core::FundSheet sheet;
    QString readError;
    const bool read = core::FundSheetReader::read(m_workbook, layout, &sheet, &readError);
    if (read) {
        // Цвет шрифта (ветхие / устаревшие) — отдельным чтением, только для показа.
        core::FundSheetReader::applyFontColors(&sheet, io::readFontColors(workPath, sheet.sheetName));
        // Внутренние ID экземпляров: из скрытого листа или новые.
        core::CopyIds::assign(&sheet, core::CopyIds::load(m_workbook));
    }
    QApplication::restoreOverrideCursor();

    m_searchTimer.stop();
    m_proxy->clearAllFilters();
    m_withdrawnProxy->clearAllFilters();
    if (read) {
        const QString sheetName = sheet.sheetName;
        const int headerRow = sheet.headerRow;
        m_model->setSheet(std::move(sheet));
        m_window->booksPage()->showTable(m_proxy);
        m_window->withdrawnPage()->showTable(m_withdrawnProxy);
        updateColumnVisibility();
        fitColumns(m_window->booksTable());
        m_fitWithdrawnPending = true;
        if (m_window->isWithdrawnTabActive())
            emit m_window->withdrawnTabShown();
        updateCounts();
        m_window->statusBar()->showMessage(
            QStringLiteral("Открыт файл «%1», лист «%2», заголовки в строке %3. Рабочая копия: %4")
                .arg(QFileInfo(path).fileName(), sheetName)
                .arg(headerRow)
                .arg(QDir::toNativeSeparators(workPath)));
        if (imported)
            finishImport(withdrawnMarks);
    } else {
        m_model->setSheet({});
        m_window->withdrawnPage()->setMessage(QStringLiteral("В файле не найден список книг."));
        m_window->booksPage()->setMessage(
            QStringLiteral("Файл открыт, но список книг в нём не найден.\n\n%1").arg(readError));
        m_window->statusBar()->showMessage(
            QStringLiteral("Открыт файл «%1». Рабочая копия: %2")
                .arg(QFileInfo(path).fileName(), QDir::toNativeSeparators(workPath)));
    }
    return true;
}

void DocumentController::finishImport(const QSet<QString> &withdrawnMarks)
{
    // Настройки мастера и ID экземпляров сразу пишем в рабочую копию:
    // при следующем открытии мастер уже не нужен.
    QString error;
    if (!core::CopyIds::save(m_workbook, m_model->sheet(), &error) || !m_workbook.saveAs(m_workPath, &error)) {
        m_window->statusBar()->showMessage(
            QStringLiteral("Настройки импорта не записаны в рабочую копию: %1. Они запишутся при «Сохранить».")
                .arg(error));
    }

    // Пометки «нет на месте» → выбытие (отдельное действие для отмены на каждую пометку).
    const QHash<int, QString> marked = core::recordsWithMarks(m_model->sheet(), withdrawnMarks);
    if (marked.isEmpty())
        return;
    QMap<QString, QVector<int>> byMark;
    for (auto it = marked.constBegin(); it != marked.constEnd(); ++it)
        byMark[it.value()].push_back(it.key());
    const QDate today = QDate::currentDate();
    int total = 0;
    for (auto it = byMark.begin(); it != byMark.end(); ++it) {
        std::sort(it->begin(), it->end());
        total += m_model->withdraw(*it, today, core::markWithdrawalReason(),
                                   QStringLiteral("пометка «%1»").arg(it.key()));
    }
    updateCounts();
    m_window->statusBar()->showMessage(
        QStringLiteral("По пометкам в файле отмечено выбывшими %1 экз. Они на вкладке «Выбывшие». "
                       "Нажмите «Сохранить», чтобы записать это в рабочую копию.")
            .arg(total));
}

void DocumentController::fitColumns(QTableView *table)
{
    QHeaderView *header = table->horizontalHeader();
    header->setResizeContentsPrecision(300);  // по первым строкам: быстро на 8000 книг
    table->resizeColumnsToContents();
    const int author = m_model->sheet().columnIndex(core::ColumnRole::AuthorTitle);
    for (int c = 0; c < m_model->columnCount(); ++c) {
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        if (c != author)
            table->setColumnWidth(c, std::min(table->columnWidth(c), 220));
    }
    if (author < 0)
        return;
    // Названию — всё место, что осталось после остальных столбцов,
    // но не меньше 40 % ширины таблицы.
    int others = 0;
    for (int c = 0; c < m_model->columnCount(); ++c) {
        if (c != author && !table->isColumnHidden(c))
            others += table->columnWidth(c);
    }
    const int viewport = table->viewport()->width();
    const int width = std::max({viewport - others - 4, viewport * 2 / 5, 320});
    table->setColumnWidth(author, width);
}

void DocumentController::save()
{
    if (!m_workbook.isOpen())
        return;
    QString error;
    if (!m_model->writeChanges(m_workbook, &error) || !m_workbook.saveAs(m_workPath, &error)) {
        showError(QStringLiteral("Не удалось сохранить"),
                  QStringLiteral("%1\n\nЕсли файл открыт в Excel, закройте его и нажмите «Сохранить» ещё раз.")
                      .arg(error));
        return;
    }
    m_model->markSaved();
    m_window->setUnsavedChanges(false);
    m_window->statusBar()->showMessage(QStringLiteral("Изменения сохранены в рабочую копию"), 5000);
}

void DocumentController::saveAs()
{
    if (!m_workbook.isOpen())
        return;

    QSettings settings;
    const QString startDir = settings.value(kLastDirKey, QDir::homePath()).toString();
    const QString suggested = QDir(startDir).filePath(QFileInfo(m_workPath).fileName());
    QString path = QFileDialog::getSaveFileName(m_window, QStringLiteral("Сохранить как"), suggested,
                                                QStringLiteral("Файлы Excel (*.xlsx)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".xlsx");

    if (samePath(path, m_originalPath)) {
        showError(QStringLiteral("Это исходный файл"),
                  QStringLiteral("Исходный файл программа не перезаписывает, чтобы его можно было "
                                 "открыть заново.\n\nВыберите другое имя или другую папку."));
        return;
    }

    QString error;
    if (!m_model->writeChanges(m_workbook, &error) || !m_workbook.saveAs(path, &error)) {
        showError(QStringLiteral("Не удалось сохранить"), error);
        return;
    }
    m_model->markSaved();
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    // Дальше программа работает с новым файлом.
    m_workPath = path;
    m_window->setWorkFileName(QFileInfo(path).fileName());
    m_window->setUnsavedChanges(false);
    m_window->statusBar()->showMessage(
        QStringLiteral("Файл сохранён: %1").arg(QDir::toNativeSeparators(path)), 8000);
}

bool DocumentController::confirmDiscardChanges()
{
    if (!m_window->hasUnsavedChanges())
        return true;

    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Несохранённые изменения"));
    box.setText(QStringLiteral("В рабочем файле есть несохранённые изменения. Что сделать?"));
    auto *saveButton = box.addButton(QStringLiteral("Сохранить"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("Не сохранять"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();

    if (box.clickedButton() == saveButton) {
        save();
        return !m_window->hasUnsavedChanges();
    }
    return box.clickedButton() == discard;
}

void DocumentController::showError(const QString &title, const QString &text)
{
    QMessageBox::warning(m_window, title, text);
}

} // namespace bh::ui
