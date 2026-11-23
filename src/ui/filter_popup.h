#pragma once

#include "models/fund_filter_proxy.h"

#include <QDialog>
#include <QSet>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;

namespace bh::ui {

// Окно фильтра столбца, как в Excel: сортировка, поиск по значениям,
// список значений с галочками и количеством строк.
class FilterPopup : public QDialog
{
    Q_OBJECT

public:
    enum class Result { Cancelled, Apply, ApplyCondition, Clear, SortAscending, SortDescending };

    FilterPopup(const QString &columnTitle, const QVector<QPair<QString, int>> &values,
                const QSet<QString> &checked, bool hasFilter,
                const models::ColumnCondition *condition = nullptr, QWidget *parent = nullptr);

    Result result() const { return m_result; }
    QSet<QString> checkedValues() const;
    bool allChecked() const;
    models::ColumnCondition condition() const { return m_condition; }

private:
    void applySearch(const QString &text);
    void updateSelectAll();
    void finish(Result result);
    void updateConditionInputs();
    bool readCondition();

    QLineEdit *m_search = nullptr;
    QCheckBox *m_selectAll = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QComboBox *m_op = nullptr;
    QLineEdit *m_value1 = nullptr;
    QLineEdit *m_value2 = nullptr;
    QLabel *m_and = nullptr;
    QWidget *m_valuesBox = nullptr;
    models::ColumnCondition m_condition;
    Result m_result = Result::Cancelled;
    bool m_updating = false;
};

} // namespace bh::ui
