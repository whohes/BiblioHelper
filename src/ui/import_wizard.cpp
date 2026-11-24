#include "import_wizard.h"

#include "io/xlsx_workbook.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWizardPage>

#include <algorithm>

namespace bh::ui {

namespace {

enum PageId { SheetPageId, ColumnsPageId, MarksPageId, SummaryPageId };

constexpr int kPreviewRows = 60;
constexpr int kPreviewColumns = 30;

QString columnLetter(int column)
{
    QString letters;
    while (column > 0) {
        const int rem = (column - 1) % 26;
        letters.prepend(QChar('A' + rem));
        column = (column - 1) / 26;
    }
    return letters;
}

QString copiesText(int n)
{
    return QStringLiteral("%1 экз.").arg(QLocale(QLocale::Russian).toString(n));
}

QLabel *introLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

QTableWidget *plainTable(QWidget *parent)
{
    auto *table = new QTableWidget(parent);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 12);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    return table;
}

bool isSingleRole(core::ColumnRole role)
{
    return role != core::ColumnRole::Mark && role != core::ColumnRole::Other && role != core::ColumnRole::Status;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Лист и строка заголовков

class SheetPage : public QWizardPage
{
public:
    explicit SheetPage(ImportWizard *wizard)
        : m_wizard(wizard)
    {
        setTitle(QStringLiteral("Шаг 1 из 3. Лист и строка с названиями столбцов"));
        auto *layout = new QVBoxLayout(this);
        layout->setSpacing(10);
        layout->addWidget(introLabel(
            QStringLiteral("Программа сама нашла лист со списком книг и строку, где записаны названия столбцов "
                           "(она выделена синим). Если ошиблась, выберите другой лист или щёлкните нужную строку "
                           "в таблице."),
            this));

        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_sheetCombo = new QComboBox(this);
        m_sheetCombo->setMinimumHeight(38);
        form->addRow(QStringLiteral("Лист:"), m_sheetCombo);
        m_headerSpin = new QSpinBox(this);
        m_headerSpin->setMinimumHeight(38);
        m_headerSpin->setMaximumWidth(140);
        form->addRow(QStringLiteral("Строка с названиями:"), m_headerSpin);
        layout->addLayout(form);

        m_preview = plainTable(this);
        layout->addWidget(m_preview, 1);

        for (const auto &info : qAsConst(wizard->m_sheets)) {
            const QString text = info.records > 0
                ? QStringLiteral("%1 — найдено книг: %2").arg(info.name).arg(info.records)
                : QStringLiteral("%1 — список книг не найден").arg(info.name);
            m_sheetCombo->addItem(text, info.name);
        }
        int best = 0;
        for (int i = 1; i < wizard->m_sheets.size(); ++i) {
            if (wizard->m_sheets.at(i).records > wizard->m_sheets.at(best).records)
                best = i;
        }
        connect(m_sheetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int index) { loadSheet(index); });
        connect(m_headerSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { highlight(); });
        connect(m_preview, &QTableWidget::cellClicked, this, [this](int row, int) { m_headerSpin->setValue(row + 1); });
        for (int i = 0; i < wizard->m_sheets.size(); ++i) {
            if (wizard->m_sheets.at(i).name == wizard->m_preferredSheet)
                best = i;
        }
        m_sheetCombo->setCurrentIndex(best);
        loadSheet(best);
    }

    bool validatePage() override
    {
        const int row = m_headerSpin->value() - 1;
        bool filled = false;
        for (int c = 0; c < m_preview->columnCount() && !filled; ++c) {
            const QTableWidgetItem *item = m_preview->item(row, c);
            filled = item && !item->text().trimmed().isEmpty();
        }
        if (!filled) {
            QMessageBox::warning(this, QStringLiteral("Пустая строка"),
                                 QStringLiteral("В строке %1 ничего не записано. Выберите строку, где записаны "
                                                "названия столбцов («Автор и название», «Инв. №»…).")
                                     .arg(row + 1));
            return false;
        }
        m_wizard->m_layout.sheet = m_sheetCombo->currentData().toString();
        m_wizard->m_layout.headerRow = m_headerSpin->value();
        return true;
    }

private:
    void loadSheet(int index)
    {
        if (index < 0 || index >= m_wizard->m_sheets.size())
            return;
        const auto &info = m_wizard->m_sheets.at(index);
        const io::SheetSize size = m_wizard->m_workbook.sheetSize(info.name);
        const int rows = std::max(1, std::min(size.rows, kPreviewRows));
        const int columns = std::max(1, std::min(size.columns, kPreviewColumns));

        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto data = m_wizard->m_workbook.readRows(info.name, 1, rows, columns);
        QApplication::restoreOverrideCursor();

        m_preview->clear();
        m_preview->setRowCount(rows);
        m_preview->setColumnCount(columns);
        QStringList letters;
        for (int c = 1; c <= columns; ++c)
            letters << columnLetter(c);
        m_preview->setHorizontalHeaderLabels(letters);
        for (const auto &row : data) {
            if (row.row < 1 || row.row > rows)
                continue;
            for (int c = 0; c < columns && c < row.values.size(); ++c) {
                const QString text = core::displayText(row.values.at(c));
                if (!text.isEmpty())
                    m_preview->setItem(row.row - 1, c, new QTableWidgetItem(text));
            }
        }
        for (int c = 0; c < columns; ++c)
            m_preview->setColumnWidth(c, c == 1 ? 320 : 90);

        m_headerSpin->blockSignals(true);
        m_headerSpin->setRange(1, rows);
        m_headerSpin->setValue(info.detectedHeaderRow > 0 ? std::min(info.detectedHeaderRow, rows) : 1);
        m_headerSpin->blockSignals(false);
        highlight();
    }

    void highlight()
    {
        const int header = m_headerSpin->value() - 1;
        QFont bold = m_preview->font();
        bold.setBold(true);
        for (int r = 0; r < m_preview->rowCount(); ++r) {
            for (int c = 0; c < m_preview->columnCount(); ++c) {
                QTableWidgetItem *item = m_preview->item(r, c);
                if (!item) {
                    if (r != header)
                        continue;
                    item = new QTableWidgetItem;
                    m_preview->setItem(r, c, item);
                }
                if (r == header) {
                    item->setBackground(QColor(0xd6, 0xe6, 0xfa));
                    item->setFont(bold);
                    item->setForeground(QColor(0x1f, 0x29, 0x33));
                } else {
                    item->setBackground(Qt::NoBrush);
                    item->setFont(m_preview->font());
                    // Строки над заголовком (название списка) — серым: они не книги.
                    item->setForeground(r < header ? QColor(0x9a, 0xa3, 0xae) : QColor(0x1f, 0x29, 0x33));
                }
            }
        }
        m_preview->scrollToItem(m_preview->item(header, 0) ? m_preview->item(header, 0) : nullptr,
                                QAbstractItemView::PositionAtCenter);
    }

    ImportWizard *m_wizard;
    QComboBox *m_sheetCombo;
    QSpinBox *m_headerSpin;
    QTableWidget *m_preview;
};

// ---------------------------------------------------------------------------
// 2. Назначение столбцов

class ColumnsPage : public QWizardPage
{
public:
    explicit ColumnsPage(ImportWizard *wizard)
        : m_wizard(wizard)
    {
        setTitle(QStringLiteral("Шаг 2 из 3. Что записано в столбцах"));
        auto *layout = new QVBoxLayout(this);
        layout->setSpacing(10);
        layout->addWidget(introLabel(
            QStringLiteral("Программа подобрала назначение столбцов по их названиям. Проверьте и при "
                           "необходимости поправьте. Обязателен столбец «Автор и название». Столбцы без названия, "
                           "куда ставили отметки при проверке («*», «сп», «инв»), — это «Пометка о наличии»."),
            this));
        m_table = plainTable(this);
        m_table->setColumnCount(4);
        m_table->setHorizontalHeaderLabels({QStringLiteral("Столбец"), QStringLiteral("Название в файле"),
                                            QStringLiteral("Примеры значений"), QStringLiteral("Что это")});
        m_table->setSelectionMode(QAbstractItemView::NoSelection);
        m_table->verticalHeader()->hide();
        m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 22);
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        m_table->setColumnWidth(1, 240);
        m_table->setColumnWidth(3, 280);
        layout->addWidget(m_table, 1);
    }

    void initializePage() override
    {
        const core::ImportLayout &chosen = m_wizard->m_layout;
        const QString key = chosen.sheet + QLatin1Char('\n') + QString::number(chosen.headerRow);
        if (key == m_loadedFor)
            return;  // вернулись назад и снова вперёд — выбор пользователя сохраняем
        m_loadedFor = key;

        core::ImportLayout probeLayout;
        probeLayout.sheet = chosen.sheet;
        probeLayout.headerRow = chosen.headerRow;
        core::FundSheet probe;
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        core::FundSheetReader::read(m_wizard->m_workbook, probeLayout, &probe, &error);
        QApplication::restoreOverrideCursor();

        m_columns.clear();
        m_combos.clear();
        m_table->setRowCount(0);
        QVector<int> probeIndex;  // индекс столбца в probe для каждой строки таблицы
        for (int c = 0; c < probe.columns.size(); ++c) {
            const core::FundColumn &column = probe.columns.at(c);
            if (column.excelColumn > 0 && column.inFile) {
                m_columns.push_back(column);
                probeIndex.push_back(c);
            }
        }
        m_table->setRowCount(m_columns.size());
        const int probeRows = std::min<int>(probe.records.size(), 300);
        for (int i = 0; i < m_columns.size(); ++i) {
            const core::FundColumn &column = m_columns.at(i);
            const int c = probeIndex.at(i);
            auto *letter = new QTableWidgetItem(columnLetter(column.excelColumn));
            letter->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 0, letter);
            m_table->setItem(i, 1, new QTableWidgetItem(column.title));

            QStringList examples;
            for (int r = 0; r < probeRows && examples.size() < 3; ++r) {
                const QString text = core::displayText(probe.records.at(r).values.value(c));
                if (!text.isEmpty() && !examples.contains(text))
                    examples << text;
            }
            m_table->setItem(i, 2, new QTableWidgetItem(examples.join(QStringLiteral(";  "))));

            auto *combo = new QComboBox(m_table);
            if (column.role == core::ColumnRole::Status) {
                combo->addItem(core::roleTitle(core::ColumnRole::Status), static_cast<int>(core::ColumnRole::Status));
                combo->setEnabled(false);
            } else {
                for (core::ColumnRole role : core::importableRoles())
                    combo->addItem(core::roleTitle(role), static_cast<int>(role));
                combo->setCurrentIndex(std::max(0, combo->findData(static_cast<int>(column.role))));
            }
            m_table->setCellWidget(i, 3, combo);
            m_combos.push_back(combo);
        }
    }

    bool validatePage() override
    {
        QHash<int, core::ColumnRole> roles;
        QMap<core::ColumnRole, QString> firstColumn;
        int authors = 0;
        for (int i = 0; i < m_columns.size(); ++i) {
            const auto role = static_cast<core::ColumnRole>(m_combos.at(i)->currentData().toInt());
            const QString letter = columnLetter(m_columns.at(i).excelColumn);
            if (role == core::ColumnRole::AuthorTitle)
                ++authors;
            if (isSingleRole(role)) {
                if (firstColumn.contains(role)) {
                    QMessageBox::warning(this, QStringLiteral("Повтор"),
                                         QStringLiteral("Столбцы %1 и %2 оба отмечены как «%3». Такой столбец "
                                                        "может быть только один, у второго выберите "
                                                        "«Другое (показывать как есть)».")
                                             .arg(firstColumn.value(role), letter, core::roleTitle(role)));
                    return false;
                }
                firstColumn.insert(role, letter);
            }
            roles.insert(m_columns.at(i).excelColumn, role);
        }
        if (authors == 0) {
            QMessageBox::warning(this, QStringLiteral("Нет столбца с названиями книг"),
                                 QStringLiteral("Отметьте столбец, где записаны автор и название книги. "
                                                "По нему программа отличает строки с книгами от итогов и пояснений."));
            return false;
        }

        core::ImportLayout layout = m_wizard->m_layout;
        layout.roles = roles;
        core::FundSheet sheet;
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = core::FundSheetReader::read(m_wizard->m_workbook, layout, &sheet, &error);
        QApplication::restoreOverrideCursor();
        if (!ok || sheet.records.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Книги не найдены"),
                                 ok ? QStringLiteral("По этим настройкам на листе не найдено ни одной книги. "
                                                     "Проверьте строку с названиями на шаге 1 и столбец "
                                                     "«Автор и название».")
                                    : error);
            return false;
        }
        m_wizard->m_layout = layout;
        m_wizard->m_sheet = std::move(sheet);
        m_wizard->m_withdrawnMarks.clear();
        return true;
    }

    int nextId() const override
    {
        return core::countMarks(m_wizard->m_sheet).isEmpty() ? SummaryPageId : MarksPageId;
    }

private:
    ImportWizard *m_wizard;
    QTableWidget *m_table;
    QVector<core::FundColumn> m_columns;
    QVector<QComboBox *> m_combos;
    QString m_loadedFor;
};

// ---------------------------------------------------------------------------
// 3. Пометки «нет на месте»

class MarksPage : public QWizardPage
{
public:
    explicit MarksPage(ImportWizard *wizard)
        : m_wizard(wizard)
    {
        setTitle(QStringLiteral("Шаг 3 из 3. Пометки о наличии"));
        auto *layout = new QVBoxLayout(this);
        layout->setSpacing(10);
        layout->addWidget(introLabel(
            QStringLiteral("В столбцах пометок нашлись такие отметки. Галочкой отмечены те, что значат "
                           "«книги нет на месте». Такие экземпляры попадут на вкладку «Выбывшие» с причиной "
                           "«по пометке в исходном файле». Строки в Excel никуда не денутся, их можно вернуть "
                           "в фонд кнопкой «Вернуть в фонд»."),
            this));
        m_list = new QListWidget(this);
        m_list->setSpacing(2);
        layout->addWidget(m_list, 1);
        m_total = new QLabel(this);
        layout->addWidget(m_total);
        connect(m_list, &QListWidget::itemChanged, this, [this] { updateTotal(); });
    }

    void initializePage() override
    {
        m_list->blockSignals(true);
        m_list->clear();
        for (const core::MarkCount &mark : core::countMarks(m_wizard->m_sheet)) {
            auto *item = new QListWidgetItem(
                QStringLiteral("«%1» — %2").arg(mark.text, copiesText(mark.count)), m_list);
            item->setData(Qt::UserRole, mark.key);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            const bool checked = m_wizard->m_withdrawnMarks.isEmpty() ? core::isDefaultWithdrawnMark(mark.key)
                                                                      : m_wizard->m_withdrawnMarks.contains(mark.key);
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
        m_list->blockSignals(false);
        updateTotal();
    }

    bool validatePage() override
    {
        m_wizard->m_withdrawnMarks = checkedKeys();
        return true;
    }

private:
    QSet<QString> checkedKeys() const
    {
        QSet<QString> keys;
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->checkState() == Qt::Checked)
                keys.insert(m_list->item(i)->data(Qt::UserRole).toString());
        }
        return keys;
    }

    void updateTotal()
    {
        const int n = core::recordsWithMarks(m_wizard->m_sheet, checkedKeys()).size();
        m_total->setText(n > 0 ? QStringLiteral("Будут отмечены выбывшими: %1").arg(copiesText(n))
                               : QStringLiteral("Выбывшими никто отмечен не будет."));
    }

    ImportWizard *m_wizard;
    QListWidget *m_list;
    QLabel *m_total;
};

// ---------------------------------------------------------------------------
// 4. Итог

class SummaryPage : public QWizardPage
{
public:
    explicit SummaryPage(ImportWizard *wizard)
        : m_wizard(wizard)
    {
        setTitle(QStringLiteral("Всё готово"));
        auto *layout = new QVBoxLayout(this);
        m_text = new QLabel(this);
        m_text->setWordWrap(true);
        m_text->setTextFormat(Qt::RichText);
        m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        layout->addWidget(m_text);
        layout->addStretch(1);
    }

    void initializePage() override
    {
        const core::FundSheet &sheet = m_wizard->m_sheet;
        const int withdrawn = m_wizard->withdrawnCount();
        QString html;
        html += QStringLiteral("<p>Файл «%1», лист «%2», названия столбцов в строке %3.</p>")
                    .arg(m_wizard->m_fileName.toHtmlEscaped(), sheet.sheetName.toHtmlEscaped())
                    .arg(sheet.headerRow);
        html += QStringLiteral("<p style='font-size:15pt'><b>Найдено книг: %1</b></p>")
                    .arg(QLocale(QLocale::Russian).toString(sheet.records.size()));
        if (withdrawn > 0) {
            html += QStringLiteral("<p>Отмечены выбывшими по пометкам: <b>%1</b> Они будут на вкладке "
                                   "«Выбывшие».</p>")
                        .arg(copiesText(withdrawn));
        }
        html += QStringLiteral("<p>Строки итогов и пояснений под таблицей книгами не считаются. Исходный файл "
                               "не изменится: программа работает с рабочей копией.</p>");
        if (withdrawn > 0) {
            html += QStringLiteral("<p>Чтобы записать выбытие в рабочую копию, после открытия нажмите "
                                   "«Сохранить».</p>");
        }
        m_text->setText(html);
    }

private:
    ImportWizard *m_wizard;
    QLabel *m_text;
};

// ---------------------------------------------------------------------------

ImportWizard::ImportWizard(const io::XlsxWorkbook &workbook, const QString &fileName, QWidget *parent,
                           const QString &sheet)
    : QWizard(parent)
    , m_workbook(workbook)
    , m_fileName(fileName)
    , m_preferredSheet(sheet)
{
    setWindowTitle(QStringLiteral("Импорт фонда — %1").arg(fileName));
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setMinimumSize(980, 640);

    // Листы, на которых может быть фонд, и что на них нашлось само.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &name : workbook.sheetNames()) {
        if (workbook.isVeryHidden(name) || name.startsWith(QLatin1Char('_')) || name == core::archiveSheetName())
            continue;
        SheetInfo info;
        info.name = name;
        core::FundSheet detected;
        if (core::FundSheetReader::read(workbook, name, &detected)) {
            info.detectedHeaderRow = detected.headerRow;
            info.records = detected.records.size();
        }
        m_sheets.push_back(info);
    }
    QApplication::restoreOverrideCursor();

    setPage(SheetPageId, new SheetPage(this));
    setPage(ColumnsPageId, new ColumnsPage(this));
    setPage(MarksPageId, new MarksPage(this));
    setPage(SummaryPageId, new SummaryPage(this));
    setStartId(SheetPageId);

    setButtonText(QWizard::BackButton, QStringLiteral("Назад"));
    setButtonText(QWizard::NextButton, QStringLiteral("Далее"));
    setButtonText(QWizard::FinishButton, QStringLiteral("Открыть фонд"));
    setButtonText(QWizard::CancelButton, QStringLiteral("Отмена"));
    for (QWizard::WizardButton which : {QWizard::NextButton, QWizard::FinishButton}) {
        QAbstractButton *b = button(which);
        b->setProperty("primary", true);
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    for (QWizard::WizardButton which : {QWizard::BackButton, QWizard::NextButton, QWizard::FinishButton,
                                        QWizard::CancelButton})
        button(which)->setMinimumHeight(42);
}

int ImportWizard::withdrawnCount() const
{
    return core::recordsWithMarks(m_sheet, m_withdrawnMarks).size();
}

} // namespace bh::ui
