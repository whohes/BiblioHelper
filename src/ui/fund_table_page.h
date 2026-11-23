#pragma once

#include <QStackedWidget>

class QAbstractItemModel;
class QBoxLayout;
class QLabel;
class QPushButton;
class QTableView;

namespace bh::ui {

class FilterHeaderView;

// Страница со списком экземпляров: текст-заглушка, пока файл не открыт,
// или таблица с фильтрами в заголовке и строкой «Всего экземпляров…».
// Используется на вкладках «Книги» и «Выбывшие».
class FundTablePage : public QStackedWidget
{
    Q_OBJECT

public:
    explicit FundTablePage(const QString &emptyText, QWidget *parent = nullptr);

    QTableView *table() const { return m_table; }
    FilterHeaderView *header() const { return m_header; }

    // Кнопка над таблицей (например, «Вернуть в фонд»).
    void addButton(QPushButton *button);

    void setMessage(const QString &text);
    void showTable(QAbstractItemModel *model);
    bool isTableShown() const { return currentIndex() == 1; }
    // filtered — включены поиск или фильтры (строка выделяется).
    void setCountText(const QString &text, bool filtered);

private:
    QLabel *m_message = nullptr;
    QTableView *m_table = nullptr;
    FilterHeaderView *m_header = nullptr;
    QLabel *m_count = nullptr;
    QBoxLayout *m_buttons = nullptr;
};

} // namespace bh::ui
