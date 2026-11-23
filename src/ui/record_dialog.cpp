#include "record_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace bh::ui {

namespace {

QString placeholderFor(core::ColumnRole role)
{
    switch (role) {
    case core::ColumnRole::AuthorTitle: return QStringLiteral("Например: Абрамов Ф. Братья и сестры");
    case core::ColumnRole::Inventory: return QStringLiteral("Номер, «б/н» или «бр»");
    case core::ColumnRole::Price: return QStringLiteral("Например: 350 или 2р20к, «б/ц» — без цены");
    case core::ColumnRole::Year: return QStringLiteral("Например: 2018");
    case core::ColumnRole::Department: return QStringLiteral("Шифр отдела, например 84");
    case core::ColumnRole::Mark: return QStringLiteral("Пометка, например *");
    default: return {};
    }
}

} // namespace

RecordDialog::RecordDialog(const core::FundSheet &sheet, const QVector<QVariant> &values, int excelRow,
                           QWidget *parent)
    : QDialog(parent)
    , m_sheet(sheet)
    , m_original(values)
{
    setWindowTitle(excelRow > 0 ? QStringLiteral("Редактировать книгу") : QStringLiteral("Новая книга"));
    setMinimumWidth(720);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *intro = new QLabel(excelRow > 0
                                 ? QStringLiteral("Строка %1 в Excel. Изменения попадут в рабочую копию после «Сохранить» в главном окне.")
                                       .arg(excelRow)
                                 : QStringLiteral("Книга добавится в конец списка. В Excel она попадёт после «Сохранить» в главном окне."),
                             this);
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setVerticalSpacing(10);
    m_edits.resize(sheet.columns.size());
    for (int c = 0; c < sheet.columns.size(); ++c) {
        const core::FundColumn &column = sheet.columns.at(c);
        if (column.excelColumn <= 0)
            continue;
        auto *edit = new QLineEdit(core::displayText(values.value(c)), this);
        edit->setMinimumHeight(38);
        edit->setPlaceholderText(placeholderFor(column.role));
        if (column.role == core::ColumnRole::AuthorTitle)
            edit->setMinimumWidth(520);
        m_edits[c] = edit;
        form->addRow(column.title + QLatin1Char(':'), edit);
        if (column.role == core::ColumnRole::Inventory)
            connect(edit, &QLineEdit::textChanged, this, &RecordDialog::updateWarning);
    }
    layout->addLayout(form);

    m_warning = new QLabel(this);
    m_warning->setWordWrap(true);
    m_warning->setObjectName(QStringLiteral("warningLabel"));
    m_warning->hide();
    layout->addWidget(m_warning);

    auto *buttons = new QDialogButtonBox(this);
    auto *save = buttons->addButton(QStringLiteral("Сохранить"), QDialogButtonBox::AcceptRole);
    auto *cancel = buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    for (auto *b : {save, cancel})
        b->setMinimumHeight(44);
    save->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const int author = sheet.columnIndex(core::ColumnRole::AuthorTitle);
    if (author >= 0 && m_edits.at(author))
        m_edits.at(author)->setFocus();
}

void RecordDialog::setDuplicateCheck(DuplicateCheck check)
{
    m_duplicateCheck = std::move(check);
    updateWarning();
}

void RecordDialog::updateWarning()
{
    const int inv = m_sheet.columnIndex(core::ColumnRole::Inventory);
    if (!m_duplicateCheck || inv < 0 || !m_edits.at(inv)) {
        m_warning->hide();
        return;
    }
    const QString text = m_duplicateCheck(m_edits.at(inv)->text().trimmed());
    m_warning->setText(text);
    m_warning->setVisible(!text.isEmpty());
}

QVector<QVariant> RecordDialog::values() const
{
    QVector<QVariant> result = m_original;
    result.resize(m_sheet.columns.size());
    for (int c = 0; c < m_sheet.columns.size(); ++c) {
        if (!m_edits.at(c))
            continue;
        const QString text = m_edits.at(c)->text();
        // Не трогаем значение, если текст не менялся: число остаётся числом,
        // формула-ссылка — формулой.
        if (text == core::displayText(m_original.value(c)))
            continue;
        result[c] = core::valueFromInput(text, m_sheet.columns.at(c).role, m_original.value(c));
    }
    return result;
}

void RecordDialog::accept()
{
    const int author = m_sheet.columnIndex(core::ColumnRole::AuthorTitle);
    if (author >= 0 && m_edits.at(author) && m_edits.at(author)->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Заполните поле «%1».")
                                                      .arg(m_sheet.columns.at(author).title));
        m_edits.at(author)->setFocus();
        return;
    }
    QDialog::accept();
}

} // namespace bh::ui
