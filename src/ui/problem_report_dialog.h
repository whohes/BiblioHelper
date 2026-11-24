#pragma once

#include "core/problem_report.h"

#include <QDialog>

class QCheckBox;
class QPlainTextEdit;

namespace bh::ui {

// «Сообщить о проблеме»: описание проблемы и письмо на адрес поддержки
// через почтовую программу (mailto) или копированием текста.
class ProblemReportDialog : public QDialog
{
    Q_OBJECT

public:
    ProblemReportDialog(core::ProblemReport report, QWidget *parent = nullptr);

private:
    core::ProblemReport current() const;
    void sendMail();
    void copyText();

    core::ProblemReport m_report;
    QPlainTextEdit *m_text = nullptr;
    QCheckBox *m_details = nullptr;
};

} // namespace bh::ui
