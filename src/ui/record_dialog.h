#pragma once

#include "core/fund_sheet.h"

#include <QDialog>

#include <functional>
#include <QVector>

class QLabel;
class QLineEdit;

namespace bh::ui {

// Форма книги (экземпляра): по полю на каждый столбец листа, крупно.
// Получает копию данных и ничего не меняет сама: результат забирает
// вызывающий код через values() только после «Сохранить».
class RecordDialog : public QDialog
{
    Q_OBJECT

public:
    // excelRow == 0 — новая книга.
    RecordDialog(const core::FundSheet &sheet, const QVector<QVariant> &values, int excelRow,
                 QWidget *parent = nullptr);

    QVector<QVariant> values() const;

    // Подсказка о другом экземпляре с тем же инвентарным номером.
    using DuplicateCheck = std::function<QString(const QString &inventory)>;
    void setDuplicateCheck(DuplicateCheck check);

protected:
    void accept() override;

private:
    void updateWarning();

    const core::FundSheet &m_sheet;
    QVector<QVariant> m_original;
    QVector<QLineEdit *> m_edits;  // nullptr для вычисляемых столбцов
    QLabel *m_warning = nullptr;
    DuplicateCheck m_duplicateCheck;
};

} // namespace bh::ui
