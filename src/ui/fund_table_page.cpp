#include "fund_table_page.h"

#include "filter_header_view.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>

namespace bh::ui {

FundTablePage::FundTablePage(const QString &emptyText, QWidget *parent)
    : QStackedWidget(parent)
{
    m_message = new QLabel(emptyText, this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setObjectName(QStringLiteral("emptyState"));
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addWidget(m_message);

    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_buttons = new QHBoxLayout;
    m_buttons->setSpacing(8);
    m_buttons->addStretch(1);
    layout->addLayout(m_buttons);

    m_table = new QTableView(page);
    m_header = new FilterHeaderView(m_table);
    m_table->setHorizontalHeader(m_header);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 14);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setToolTip(QStringLiteral("Номер строки в Excel"));
    layout->addWidget(m_table, 1);

    m_count = new QLabel(page);
    m_count->setObjectName(QStringLiteral("countLabel"));
    m_count->setWordWrap(true);
    layout->addWidget(m_count);

    addWidget(page);
}

void FundTablePage::addButton(QPushButton *button)
{
    // Кнопки — слева, в порядке добавления.
    m_buttons->insertWidget(m_buttons->count() - 1, button);
}

void FundTablePage::setMessage(const QString &text)
{
    m_message->setText(text);
    setCurrentIndex(0);
}

void FundTablePage::showTable(QAbstractItemModel *model)
{
    m_table->setModel(model);
    m_table->sortByColumn(-1, Qt::AscendingOrder);  // порядок как в файле
    m_header->setSortIndicator(-1, Qt::AscendingOrder);
    setCurrentIndex(1);
}

void FundTablePage::setCountText(const QString &text, bool filtered)
{
    m_count->setText(text);
    m_count->setProperty("filtered", filtered);
    m_count->style()->unpolish(m_count);
    m_count->style()->polish(m_count);
}

} // namespace bh::ui
