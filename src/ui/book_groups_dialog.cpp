#include "book_groups_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace bh::ui {

namespace {

QString booksWord(int n)
{
    const int mod100 = n % 100;
    const int mod10 = n % 10;
    if (mod100 >= 11 && mod100 <= 14)
        return QStringLiteral("книг");
    if (mod10 == 1)
        return QStringLiteral("книга");
    if (mod10 >= 2 && mod10 <= 4)
        return QStringLiteral("книги");
    return QStringLiteral("книг");
}

} // namespace

BookGroupsDialog::BookGroupsDialog(const core::FundSheet &sheet, const QVector<QVector<int>> &groups,
                                   QWidget *parent)
    : QDialog(parent)
    , m_groups(groups)
{
    setWindowTitle(QStringLiteral("Одинаковые книги"));
    resize(900, 600);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    int copies = 0;
    for (const auto &group : groups)
        copies += group.size();
    const QLocale ru(QLocale::Russian);
    auto *intro = new QLabel(
        QStringLiteral("Найдено %1 %3, записанных несколькими строками (всего %2 экз.): одинаковые автор и "
                       "название (без учёта точек, пробелов и регистра) и одинаковый год.\n"
                       "Отмеченные группы станут книгами с экземплярами: на вкладке «Книги» такая книга "
                       "показана одной строкой, её экземпляры раскрываются синей стрелкой слева. Строки в Excel не "
                       "меняются, объединение можно отменить кнопкой «Отменить» или «Разделить».")
            .arg(ru.toString(groups.size()), ru.toString(copies),
                 groups.size() % 10 == 1 && groups.size() % 100 != 11 ? QStringLiteral("книга") : QStringLiteral("книг")),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    const int author = sheet.columnIndex(core::ColumnRole::AuthorTitle);
    const int year = sheet.columnIndex(core::ColumnRole::Year);
    const int inventory = sheet.columnIndex(core::ColumnRole::Inventory);
    auto text = [&sheet](int record, int column) {
        return column >= 0 ? core::displayText(sheet.records.at(record).values.value(column)) : QString();
    };

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("Автор и название"), QStringLiteral("Год"), QStringLiteral("Экз.")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setUniformRowHeights(true);
    for (int g = 0; g < groups.size(); ++g) {
        const QVector<int> &group = groups.at(g);
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, text(group.first(), author));
        item->setText(1, text(group.first(), year));
        item->setText(2, QString::number(group.size()));
        item->setTextAlignment(1, Qt::AlignCenter);
        item->setTextAlignment(2, Qt::AlignCenter);
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, g);
        for (int r : group) {
            auto *child = new QTreeWidgetItem(item);
            const QString inv = text(r, inventory);
            const int row = sheet.records.at(r).excelRow;
            child->setText(0, QStringLiteral("%1 — %2   %3")
                                  .arg(inv.isEmpty() ? QStringLiteral("без инв. №") : QStringLiteral("инв. № %1").arg(inv),
                                       row > 0 ? QStringLiteral("строка %1").arg(row) : QStringLiteral("новая"),
                                       sheet.isWithdrawn(r) ? QStringLiteral("(выбыл)") : QString())
                                  .trimmed());
            child->setToolTip(0, text(r, author));
            child->setFlags(Qt::ItemIsEnabled);
        }
    }
    layout->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    auto *all = new QPushButton(QStringLiteral("Отметить все"), this);
    auto *none = new QPushButton(QStringLiteral("Снять все"), this);
    row->addWidget(all);
    row->addWidget(none);
    row->addStretch(1);
    auto *buttons = new QDialogButtonBox(this);
    m_merge = buttons->addButton(QStringLiteral("Объединить"), QDialogButtonBox::AcceptRole);
    m_merge->setProperty("primary", true);
    m_merge->style()->unpolish(m_merge);
    m_merge->style()->polish(m_merge);
    m_merge->setDefault(true);
    buttons->addButton(QStringLiteral("Не сейчас"), QDialogButtonBox::RejectRole);
    row->addWidget(buttons);
    layout->addLayout(row);

    auto setAll = [this](Qt::CheckState state) {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            m_tree->topLevelItem(i)->setCheckState(0, state);
    };
    connect(all, &QPushButton::clicked, this, [setAll] { setAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [setAll] { setAll(Qt::Unchecked); });
    connect(m_tree, &QTreeWidget::itemChanged, this, &BookGroupsDialog::updateButton);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateButton();
}

QVector<QVector<int>> BookGroupsDialog::checkedGroups() const
{
    QVector<QVector<int>> result;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            result.push_back(m_groups.at(item->data(0, Qt::UserRole).toInt()));
    }
    return result;
}

void BookGroupsDialog::updateButton()
{
    const int n = checkedGroups().size();
    m_merge->setText(QStringLiteral("Объединить (%1 %2)").arg(n).arg(booksWord(n)));
    m_merge->setEnabled(n > 0);
}

} // namespace bh::ui
