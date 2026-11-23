#include "export_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QVBoxLayout>

namespace bh::ui {

namespace {

QString number(int n)
{
    return QLocale(QLocale::Russian).toString(n);
}

} // namespace

QString ExportDialog::scopeTitle(Scope scope)
{
    switch (scope) {
    case Scope::ActiveAll: return QStringLiteral("Книжный фонд");
    case Scope::ActiveShown: return QStringLiteral("Книжный фонд (выборка)");
    case Scope::WithdrawnAll: return QStringLiteral("Выбывшие экземпляры");
    case Scope::WithdrawnShown: return QStringLiteral("Выбывшие экземпляры (выборка)");
    case Scope::Everything: return QStringLiteral("Книжный фонд вместе с выбывшими");
    }
    return {};
}

ExportDialog::ExportDialog(const core::FundSheet &sheet, const Counts &counts, Scope initial, QWidget *parent)
    : QDialog(parent)
    , m_sheet(sheet)
{
    setWindowTitle(QStringLiteral("Экспорт"));
    setMinimumWidth(760);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *intro = new QLabel(QStringLiteral("Программа сохранит выбранные строки и столбцы в отдельный файл. "
                                            "Рабочая копия фонда не меняется."),
                             this);
    intro->setWordWrap(true);
    intro->setMinimumHeight(intro->heightForWidth(minimumWidth() - 24));
    layout->addWidget(intro);

    auto *scopeBox = new QGroupBox(QStringLiteral("Что выгрузить"), this);
    auto *scopeLayout = new QVBoxLayout(scopeBox);
    m_scopes = new QButtonGroup(this);
    auto addScope = [&](Scope scope, const QString &text, bool enabled) {
        auto *radio = new QRadioButton(text, scopeBox);
        radio->setEnabled(enabled);
        scopeLayout->addWidget(radio);
        m_scopes->addButton(radio, static_cast<int>(scope));
    };
    const bool activeFiltered = counts.activeShown != counts.activeAll;
    const bool withdrawnFiltered = counts.withdrawnShown != counts.withdrawnAll;
    addScope(Scope::ActiveAll, QStringLiteral("Весь фонд (%1 экз.)").arg(number(counts.activeAll)),
             counts.activeAll > 0);
    addScope(Scope::ActiveShown,
             QStringLiteral("Найденные сейчас: поиск и фильтры на вкладке «Книги» (%1 экз.)")
                 .arg(number(counts.activeShown)),
             activeFiltered && counts.activeShown > 0);
    addScope(Scope::WithdrawnAll, QStringLiteral("Выбывшие (%1 экз.)").arg(number(counts.withdrawnAll)),
             counts.withdrawnAll > 0);
    addScope(Scope::WithdrawnShown,
             QStringLiteral("Найденные среди выбывших (%1 экз.)").arg(number(counts.withdrawnShown)),
             withdrawnFiltered && counts.withdrawnShown > 0);
    addScope(Scope::Everything,
             QStringLiteral("Фонд вместе с выбывшими (%1 экз.)").arg(number(counts.activeAll + counts.withdrawnAll)),
             counts.withdrawnAll > 0 && counts.activeAll > 0);
    QAbstractButton *start = m_scopes->button(static_cast<int>(initial));
    if (!start || !start->isEnabled())
        start = m_scopes->button(static_cast<int>(counts.activeAll > 0 ? Scope::ActiveAll : Scope::WithdrawnAll));
    start->setChecked(true);
    layout->addWidget(scopeBox);

    auto *columnsBox = new QGroupBox(QStringLiteral("Столбцы"), this);
    auto *columnsLayout = new QVBoxLayout(columnsBox);
    m_columns = new QListWidget(columnsBox);
    m_columns->setMinimumHeight(180);
    columnsLayout->addWidget(m_columns);
    auto *columnButtons = new QHBoxLayout;
    auto *all = new QPushButton(QStringLiteral("Выбрать все"), columnsBox);
    auto *defaults = new QPushButton(QStringLiteral("Как в таблице"), columnsBox);
    columnButtons->addWidget(all);
    columnButtons->addWidget(defaults);
    columnButtons->addStretch(1);
    columnsLayout->addLayout(columnButtons);
    layout->addWidget(columnsBox, 1);
    for (int c = 0; c < sheet.columns.size(); ++c) {
        auto *item = new QListWidgetItem(sheet.columns.at(c).title, m_columns);
        item->setData(Qt::UserRole, c);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    }
    resetColumns();
    connect(all, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_columns->count(); ++i)
            m_columns->item(i)->setCheckState(Qt::Checked);
    });
    connect(defaults, &QPushButton::clicked, this, [this] { resetColumns(); });
    connect(m_scopes, QOverload<QAbstractButton *, bool>::of(&QButtonGroup::buttonToggled), this,
            [this](QAbstractButton *, bool checked) {
                if (checked)
                    resetColumns();
            });

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_format = new QComboBox(this);
    m_format->setMinimumHeight(38);
    m_format->addItem(QStringLiteral("Excel (.xlsx) — с оформлением, итогом и фильтрами"),
                      static_cast<int>(core::FundExport::Format::Xlsx));
    m_format->addItem(QStringLiteral("CSV (.csv) — для других программ"), static_cast<int>(core::FundExport::Format::Csv));
    form->addRow(QStringLiteral("Формат:"), m_format);
    layout->addLayout(form);
    m_open = new QCheckBox(QStringLiteral("Открыть файл после сохранения"), this);
    m_open->setChecked(true);
    layout->addWidget(m_open);

    auto *buttons = new QDialogButtonBox(this);
    auto *ok = buttons->addButton(QStringLiteral("Выгрузить…"), QDialogButtonBox::AcceptRole);
    auto *cancel = buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    ok->setProperty("primary", true);
    ok->style()->unpolish(ok);
    ok->style()->polish(ok);
    for (auto *b : {ok, cancel})
        b->setMinimumHeight(44);
    ok->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (columns().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Экспорт"), QStringLiteral("Отметьте хотя бы один столбец."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool ExportDialog::scopeHasWithdrawn() const
{
    const Scope s = scope();
    return s == Scope::WithdrawnAll || s == Scope::WithdrawnShown || s == Scope::Everything;
}

void ExportDialog::resetColumns()
{
    const QVector<int> defaults = core::FundExport::defaultColumns(m_sheet, scopeHasWithdrawn());
    for (int i = 0; i < m_columns->count(); ++i) {
        QListWidgetItem *item = m_columns->item(i);
        item->setCheckState(defaults.contains(item->data(Qt::UserRole).toInt()) ? Qt::Checked : Qt::Unchecked);
    }
}

ExportDialog::Scope ExportDialog::scope() const
{
    return static_cast<Scope>(m_scopes->checkedId());
}

QVector<int> ExportDialog::columns() const
{
    QVector<int> result;
    for (int i = 0; i < m_columns->count(); ++i) {
        if (m_columns->item(i)->checkState() == Qt::Checked)
            result.push_back(m_columns->item(i)->data(Qt::UserRole).toInt());
    }
    return result;
}

core::FundExport::Format ExportDialog::format() const
{
    return static_cast<core::FundExport::Format>(m_format->currentData().toInt());
}

bool ExportDialog::openAfterSave() const
{
    return m_open->isChecked();
}

} // namespace bh::ui
