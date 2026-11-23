#pragma once

#include "core/fund_sheet.h"

#include <QDate>
#include <QDialog>
#include <QVector>

class QComboBox;
class QDateEdit;
class QLineEdit;
class QPushButton;

namespace bh::ui {

// Окно «Выбытие»: список выбранных экземпляров (название и инв. номер),
// дата (сегодня), причина из списка или своя, примечание.
// Ничего не меняет само: результат забирает вызывающий код.
class WithdrawDialog : public QDialog
{
    Q_OBJECT

public:
    // records — индексы записей листа, которые выбывают.
    WithdrawDialog(const core::FundSheet &sheet, const QVector<int> &records, QWidget *parent = nullptr);

    QDate date() const;
    QString reason() const;
    QString note() const;

    // Причина по цветовой пометке, если она одна у всех: «Ветхая» → «ветхость».
    static QString suggestedReason(const core::FundSheet &sheet, const QVector<int> &records);

protected:
    void accept() override;

private:
    QDateEdit *m_date = nullptr;
    QComboBox *m_reason = nullptr;
    QLineEdit *m_note = nullptr;
};

} // namespace bh::ui
