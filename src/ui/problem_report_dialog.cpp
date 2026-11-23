#include "problem_report_dialog.h"

#include "core/app_settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace bh::ui {

ProblemReportDialog::ProblemReportDialog(core::ProblemReport report, QWidget *parent)
    : QDialog(parent)
    , m_report(std::move(report))
{
    setWindowTitle(QStringLiteral("Сообщить о проблеме"));
    resize(680, 520);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(8);

    auto *intro = new QLabel(QStringLiteral("Опишите, что случилось: что вы делали, что ожидали и что получилось. "
                                            "Письмо уйдёт на адрес <b>%1</b>.")
                                 .arg(core::AppSettings::supportEmail()),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(QStringLiteral("Например: нажимаю «Сохранить» — появляется сообщение «файл занят», "
                                              "хотя Excel закрыт."));
    layout->addWidget(m_text, 1);

    m_details = new QCheckBox(QStringLiteral("Добавить версию программы и Windows, имя файла и последние действия "
                                             "из журнала"),
                              this);
    m_details->setChecked(true);
    layout->addWidget(m_details);
    auto *note = new QLabel(QStringLiteral("Содержимое фонда в письмо не попадает. Если нужен сам файл, приложите "
                                           "его к письму вручную."),
                            this);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(this);
    auto *send = buttons->addButton(QStringLiteral("Написать письмо"), QDialogButtonBox::AcceptRole);
    send->setProperty("primary", true);
    send->style()->unpolish(send);
    send->style()->polish(send);
    send->setDefault(true);
    auto *copy = buttons->addButton(QStringLiteral("Скопировать текст"), QDialogButtonBox::ActionRole);
    buttons->addButton(QStringLiteral("Закрыть"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(send, &QPushButton::clicked, this, &ProblemReportDialog::sendMail);
    connect(copy, &QPushButton::clicked, this, &ProblemReportDialog::copyText);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

core::ProblemReport ProblemReportDialog::current() const
{
    core::ProblemReport report = m_report;
    report.description = m_text->toPlainText();
    report.includeDetails = m_details->isChecked();
    return report;
}

void ProblemReportDialog::sendMail()
{
    const core::ProblemReport report = current();
    if (QDesktopServices::openUrl(report.mailto(core::AppSettings::supportEmail()))) {
        accept();
        return;
    }
    // Почтовой программы нет: текст — в буфер обмена, адрес — на экран.
    QApplication::clipboard()->setText(report.body());
    QMessageBox::information(
        this, windowTitle(),
        QStringLiteral("Почтовая программа не открылась. Текст письма скопирован.\n\n"
                       "Откройте почту в браузере, создайте письмо на адрес %1 с темой «%2» и вставьте текст "
                       "(Ctrl+V).")
            .arg(core::AppSettings::supportEmail(), report.subject()));
}

void ProblemReportDialog::copyText()
{
    const core::ProblemReport report = current();
    QApplication::clipboard()->setText(report.body());
    QMessageBox::information(this, windowTitle(),
                             QStringLiteral("Текст письма скопирован. Вставьте его (Ctrl+V) в письмо на адрес %1.")
                                 .arg(core::AppSettings::supportEmail()));
}

} // namespace bh::ui
