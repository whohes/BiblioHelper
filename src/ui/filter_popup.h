#pragma once

#include <QDialog>
#include <QSet>
#include <QVector>

class QCheckBox;
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
    enum class Result { Cancelled, Apply, Clear, SortAscending, SortDescending };

    FilterPopup(const QString &columnTitle, const QVector<QPair<QString, int>> &values,
                const QSet<QString> &checked, bool hasFilter, QWidget *parent = nullptr);

    Result result() const { return m_result; }
    QSet<QString> checkedValues() const;
    bool allChecked() const;

private:
    void applySearch(const QString &text);
    void updateSelectAll();
    void finish(Result result);

    QLineEdit *m_search = nullptr;
    QCheckBox *m_selectAll = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    Result m_result = Result::Cancelled;
    bool m_updating = false;
};

} // namespace bh::ui
