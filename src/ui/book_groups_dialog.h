#pragma once

#include "core/fund_sheet.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace bh::ui {

// «Одинаковые книги»: группы строк с одинаковым названием и годом, которые
// можно объединить в книги с экземплярами. По умолчанию отмечены все;
// раскрыв группу, видно её экземпляры (инв. №, строка Excel).
class BookGroupsDialog : public QDialog
{
    Q_OBJECT

public:
    BookGroupsDialog(const core::FundSheet &sheet, const QVector<QVector<int>> &groups, QWidget *parent = nullptr);

    // Отмеченные группы (индексы записей).
    QVector<QVector<int>> checkedGroups() const;

private:
    void updateButton();

    QVector<QVector<int>> m_groups;
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_merge = nullptr;
};

} // namespace bh::ui
