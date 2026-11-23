#include "withdraw_dialog.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QStyle>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace bh::ui {

namespace {

QString cellText(const core::FundSheet &sheet, const core::FundRecord &record, core::ColumnRole role)
{
    const int c = sheet.columnIndex(role);
    return c >= 0 && c < record.values.size() ? core::displayText(record.values.at(c)) : QString();
}

} // namespace

QString WithdrawDialog::suggestedReason(const core::FundSheet &sheet, const QVector<int> &records)
{
    QString common;
    for (int i = 0; i < records.size(); ++i) {
        const QString mark = cellText(sheet, sheet.records.at(records.at(i)), core::ColumnRole::ColorMark);
        if (i == 0)
            common = mark;
        else if (mark != common)
            return {};
    }
    if (common == QStringLiteral("Ветхая"))
        return QStringLiteral("ветхость");
    if (common == QStringLiteral("Устаревшая"))
        return QStringLiteral("устаревшая по содержанию");
    return {};
}

WithdrawDialog::WithdrawDialog(const core::FundSheet &sheet, const QVector<int> &records, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Выбытие"));
    setMinimumWidth(760);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *intro = new QLabel(
        QStringLiteral("Выбывают %1 экз. Проверьте список. Экземпляры перейдут на вкладку «Выбывшие», "
                       "из файла они не удаляются.")
            .arg(records.size()),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *list = new QTableWidget(records.size(), 3, this);
    list->setHorizontalHeaderLabels({QStringLiteral("Автор и название"), QStringLiteral("Инв. №"),
                                     QStringLiteral("Строка в Excel")});
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->verticalHeader()->hide();
    list->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (int i = 0; i < records.size(); ++i) {
        const core::FundRecord &record = sheet.records.at(records.at(i));
        list->setItem(i, 0, new QTableWidgetItem(cellText(sheet, record, core::ColumnRole::AuthorTitle)));
        list->setItem(i, 1, new QTableWidgetItem(cellText(sheet, record, core::ColumnRole::Inventory)));
        list->setItem(i, 2, new QTableWidgetItem(record.excelRow > 0 ? QString::number(record.excelRow)
                                                                      : QStringLiteral("новая")));
    }
    list->setMinimumHeight(std::min(records.size(), 8) * (fontMetrics().height() + 12) + 48);
    layout->addWidget(list, 1);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setVerticalSpacing(10);

    m_date = new QDateEdit(QDate::currentDate(), this);
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    m_date->setMinimumHeight(38);
    form->addRow(QStringLiteral("Дата выбытия:"), m_date);

    m_reason = new QComboBox(this);
    m_reason->setEditable(true);  // можно вписать свою причину
    m_reason->addItems(core::withdrawalReasons());
    m_reason->setMinimumHeight(38);
    const QString suggested = suggestedReason(sheet, records);
    m_reason->setCurrentIndex(suggested.isEmpty() ? -1 : m_reason->findText(suggested));
    m_reason->lineEdit()->setPlaceholderText(QStringLiteral("выберите из списка или впишите свою"));
    form->addRow(QStringLiteral("Причина:"), m_reason);

    m_note = new QLineEdit(this);
    m_note->setMinimumHeight(38);
    m_note->setPlaceholderText(QStringLiteral("необязательно, например «акт № 12»"));
    form->addRow(QStringLiteral("Примечание:"), m_note);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(this);
    auto *ok = buttons->addButton(QStringLiteral("Оформить выбытие (%1 экз.)").arg(records.size()),
                                  QDialogButtonBox::AcceptRole);
    auto *cancel = buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    ok->setProperty("primary", true);
    ok->style()->unpolish(ok);
    ok->style()->polish(ok);
    for (auto *b : {ok, cancel})
        b->setMinimumHeight(44);
    ok->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_reason->setFocus();
}

QDate WithdrawDialog::date() const
{
    return m_date->date();
}

QString WithdrawDialog::reason() const
{
    return m_reason->currentText().trimmed();
}

QString WithdrawDialog::note() const
{
    return m_note->text().trimmed();
}

void WithdrawDialog::accept()
{
    if (reason().isEmpty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Укажите причину выбытия."));
        m_reason->setFocus();
        return;
    }
    if (date() > QDate::currentDate()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("Дата выбытия не может быть позже сегодняшней."));
        m_date->setFocus();
        return;
    }
    QDialog::accept();
}

} // namespace bh::ui
