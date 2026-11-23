#include "document_controller.h"

#include "main_window.h"

#include "core/copy_ids.h"
#include "core/fund_sheet.h"
#include "record_dialog.h"
#include "io/xlsx_font_colors.h"
#include "filter_header_view.h"
#include "filter_popup.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QApplication>
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
{
    m_proxy->setSourceModel(m_model);

    connect(window, &MainWindow::openRequested, this, &DocumentController::open);
    connect(window, &MainWindow::saveRequested, this, &DocumentController::save);
    connect(window, &MainWindow::saveAsRequested, this, &DocumentController::saveAs);

    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(250);
    connect(window, &MainWindow::searchTextChanged, this, [this](const QString &text) {
        m_pendingSearch = text;
        m_searchTimer.start();
    });
    connect(&m_searchTimer, &QTimer::timeout, this, [this] { m_proxy->setSearchText(m_pendingSearch); });
    connect(window, &MainWindow::resetFiltersRequested, this, [this] {
        m_searchTimer.stop();
        m_proxy->clearAllFilters();
        m_window->booksTable()->sortByColumn(-1, Qt::AscendingOrder);
        m_window->booksHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    });
    connect(window->booksHeader(), &FilterHeaderView::filterRequested, this,
            &DocumentController::showColumnFilter);
    connect(m_proxy, &models::FundFilterProxy::filtersChanged, this, &DocumentController::updateCounts);
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, &DocumentController::updateCounts);
    connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, &DocumentController::updateCounts);
    connect(window, &MainWindow::addBookRequested, this, &DocumentController::addBook);
    connect(window, &MainWindow::editRequested, this, &DocumentController::editSelected);
    connect(window, &MainWindow::undoRequested, this, &DocumentController::undo);
    connect(window->booksTable(), &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) {
        editRow(m_proxy->mapToSource(index).row());
    });
    connect(m_model, &models::FundTableModel::modifiedChanged, this, [this](bool modified) {
        m_window->setUnsavedChanges(modified);
        m_window->setUndoText(m_model->undoText());
    });
    // Изменённые данные могут выпасть из фильтра: прокси пересчитывает строку сама.
    m_proxy->setDynamicSortFilter(true);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &DocumentController::updateCounts);
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
    m_window->setUndoText(m_model->undoText());
    selectRecord(m_model->index(row, 0).data(models::FundTableModel::IdRole).toString());
    m_window->statusBar()->showMessage(QStringLiteral("Книга добавлена. Нажмите «Сохранить», чтобы записать её в Excel."), 10000);
}

void DocumentController::editSelected()
{
    QTableView *table = m_window->booksTable();
    const QModelIndexList rows = table->selectionModel() ? table->selectionModel()->selectedRows()
                                                         : QModelIndexList();
    if (rows.size() != 1) {
        QMessageBox::information(m_window, QStringLiteral("Редактировать"),
                                 rows.isEmpty() ? QStringLiteral("Сначала щёлкните по строке с книгой, которую нужно изменить.")
                                                : QStringLiteral("Выбрано несколько строк. Выберите одну книгу."));
        return;
    }
    editRow(m_proxy->mapToSource(rows.first()).row());
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
        m_window->setUndoText(m_model->undoText());
        selectRecord(id);
        m_window->statusBar()->showMessage(QStringLiteral("Изменения внесены. Нажмите «Сохранить», чтобы записать их в Excel."), 10000);
    }
}

void DocumentController::undo()
{
    if (!m_model->canUndo())
        return;
    m_model->undo();
    m_window->setUndoText(m_model->undoText());
    m_window->statusBar()->showMessage(QStringLiteral("Последнее изменение отменено"), 5000);
}

void DocumentController::updateCounts()
{
    QList<int> filtered;
    for (int c = 0; c < m_model->columnCount(); ++c) {
        if (m_proxy->isColumnFiltered(c))
            filtered << c;
    }
    m_window->booksHeader()->setFilteredColumns(filtered);
    m_window->setRecordCount(m_proxy->rowCount(), m_model->rowCount(), m_proxy->hasAnyFilter());
}

void DocumentController::showColumnFilter(int column, const QPoint &globalPos)
{
    if (column < 0 || column >= m_model->columnCount())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto values = m_proxy->columnValues(column);
    QApplication::restoreOverrideCursor();

    const models::ColumnCondition condition = m_proxy->columnCondition(column);
    FilterPopup popup(m_model->sheet().columns.at(column).title, values, m_proxy->columnFilter(column),
                      m_proxy->hasColumnFilter(column),
                      m_proxy->hasColumnCondition(column) ? &condition : nullptr, m_window);
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

    QTableView *table = m_window->booksTable();
    switch (popup.result()) {
    case FilterPopup::Result::Apply:
        if (popup.allChecked())
            m_proxy->clearColumnFilter(column);
        else
            m_proxy->setColumnFilter(column, popup.checkedValues());
        break;
    case FilterPopup::Result::ApplyCondition:
        m_proxy->setColumnCondition(column, popup.condition());
        break;
    case FilterPopup::Result::Clear:
        m_proxy->clearColumnFilter(column);
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
        QStringLiteral("Файлы Excel (*.xlsx *.xls);;Все файлы (*)"));
    if (path.isEmpty())
        return;
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    openFile(path);
}

bool DocumentController::openFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xls")) {
        showError(QStringLiteral("Старый формат Excel"),
                  QStringLiteral("Файлы XLS (Excel 97–2003) программа научится открывать позже.\n\n"
                                 "Пока откройте файл в Excel и сохраните его как «Книга Excel (*.xlsx)»."));
        return false;
    }

    QString error;
    const QString workPath = m_workCopies.create(path, &error);
    if (workPath.isEmpty()) {
        showError(QStringLiteral("Не удалось открыть файл"), error);
        return false;
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
    const bool read = core::FundSheetReader::readBestSheet(m_workbook, &sheet, &readError);
    if (read) {
        // Цвет шрифта (ветхие / устаревшие) — отдельным чтением, только для показа.
        core::FundSheetReader::applyFontColors(&sheet, io::readFontColors(workPath, sheet.sheetName));
        // Внутренние ID экземпляров: из скрытого листа или новые.
        core::CopyIds::assign(&sheet, core::CopyIds::load(m_workbook));
    }
    QApplication::restoreOverrideCursor();

    m_searchTimer.stop();
    m_proxy->clearAllFilters();
    if (read) {
        const QString sheetName = sheet.sheetName;
        const int headerRow = sheet.headerRow;
        m_model->setSheet(std::move(sheet));
        m_window->showBooksTable(m_proxy);
        fitColumns();
        updateCounts();
        m_window->statusBar()->showMessage(
            QStringLiteral("Открыт файл «%1», лист «%2», заголовки в строке %3. Рабочая копия: %4")
                .arg(QFileInfo(path).fileName(), sheetName)
                .arg(headerRow)
                .arg(QDir::toNativeSeparators(workPath)));
    } else {
        m_model->setSheet({});
        m_window->setBooksMessage(
            QStringLiteral("Файл открыт, но список книг в нём не найден.\n\n%1\n\n"
                           "Мастер импорта для файлов другого вида появится в следующих версиях.")
                .arg(readError));
        m_window->statusBar()->showMessage(
            QStringLiteral("Открыт файл «%1». Рабочая копия: %2")
                .arg(QFileInfo(path).fileName(), QDir::toNativeSeparators(workPath)));
    }
    return true;
}

void DocumentController::fitColumns()
{
    QTableView *table = m_window->booksTable();
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
        if (c != author)
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
    m_window->setUndoText({});
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
    m_window->setUndoText({});
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
