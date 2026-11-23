#include "filter_popup.h"

#include "core/fund_sheet.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace bh::ui {

FilterPopup::FilterPopup(const QString &columnTitle, const QVector<QPair<QString, int>> &values,
                         const QSet<QString> &checked, bool hasFilter, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Фильтр: %1").arg(columnTitle));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    auto *sortRow = new QHBoxLayout;
    auto *asc = new QPushButton(QStringLiteral("Сортировка А → Я, 1 → 9"), this);
    auto *desc = new QPushButton(QStringLiteral("Сортировка Я → А, 9 → 1"), this);
    for (auto *b : {asc, desc}) {
        b->setMinimumHeight(40);
        sortRow->addWidget(b);
    }
    layout->addLayout(sortRow);
    connect(asc, &QPushButton::clicked, this, [this] { finish(Result::SortAscending); });
    connect(desc, &QPushButton::clicked, this, [this] { finish(Result::SortDescending); });

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Найти значение…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(36);
    layout->addWidget(m_search);

    m_selectAll = new QCheckBox(QStringLiteral("Выделить всё"), this);
    m_selectAll->setTristate(true);
    layout->addWidget(m_selectAll);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setMinimumHeight(320);
    const bool all = !hasFilter;
    for (const auto &value : values) {
        auto *item = new QListWidgetItem(QStringLiteral("%1   (%2)").arg(value.first).arg(value.second), m_list);
        item->setData(Qt::UserRole, value.first);
        // Без ItemIsUserCheckable: галочку меняет щелчок по всей строке (ниже),
        // а не только по маленькому квадратику.
        item->setFlags(Qt::ItemIsEnabled);
        item->setCheckState(all || checked.contains(value.first) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(m_list, 1);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setObjectName(QStringLiteral("emptyState"));
    layout->addWidget(m_hint);

    auto *buttons = new QHBoxLayout;
    auto *clear = new QPushButton(QStringLiteral("Снять фильтр"), this);
    clear->setEnabled(hasFilter);
    auto *ok = new QPushButton(QStringLiteral("Применить"), this);
    ok->setDefault(true);
    auto *cancel = new QPushButton(QStringLiteral("Отмена"), this);
    for (auto *b : {clear, ok, cancel})
        b->setMinimumHeight(40);
    buttons->addWidget(clear);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(clear, &QPushButton::clicked, this, [this] { finish(Result::Clear); });
    connect(ok, &QPushButton::clicked, this, [this] { finish(Result::Apply); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, &FilterPopup::applySearch);
    connect(m_selectAll, &QCheckBox::clicked, this, [this] {
        // Как в Excel: «Выделить всё» действует на видимые (найденные) значения.
        const Qt::CheckState state = m_selectAll->checkState() == Qt::Unchecked ? Qt::Unchecked : Qt::Checked;
        m_updating = true;
        for (int i = 0; i < m_list->count(); ++i) {
            if (!m_list->item(i)->isHidden())
                m_list->item(i)->setCheckState(state);
        }
        m_updating = false;
        updateSelectAll();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this] {
        if (!m_updating)
            updateSelectAll();
    });
    connect(m_list, &QListWidget::itemClicked, this, [](QListWidgetItem *item) {
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });

    updateSelectAll();
    m_search->setFocus();
}

void FilterPopup::applySearch(const QString &text)
{
    const QString needle = core::normalizeForSearch(text);
    int shown = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const bool match = needle.isEmpty()
            || core::normalizeForSearch(item->data(Qt::UserRole).toString()).contains(needle);
        item->setHidden(!match);
        shown += match ? 1 : 0;
    }
    m_hint->setText(needle.isEmpty() ? QString()
                                     : QStringLiteral("Найдено значений: %1. «Выделить всё» отметит только их.")
                                           .arg(shown));
    updateSelectAll();
}

void FilterPopup::updateSelectAll()
{
    int checkedCount = 0;
    int visible = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        const auto *item = m_list->item(i);
        if (item->isHidden())
            continue;
        ++visible;
        checkedCount += item->checkState() == Qt::Checked ? 1 : 0;
    }
    m_selectAll->setCheckState(checkedCount == 0 ? Qt::Unchecked
                               : checkedCount == visible ? Qt::Checked
                                                         : Qt::PartiallyChecked);
}

QSet<QString> FilterPopup::checkedValues() const
{
    QSet<QString> result;
    for (int i = 0; i < m_list->count(); ++i) {
        const auto *item = m_list->item(i);
        if (item->checkState() == Qt::Checked && !item->isHidden())
            result.insert(item->data(Qt::UserRole).toString());
    }
    return result;
}

bool FilterPopup::allChecked() const
{
    for (int i = 0; i < m_list->count(); ++i) {
        const auto *item = m_list->item(i);
        if (item->isHidden() || item->checkState() != Qt::Checked)
            return false;
    }
    return true;
}

void FilterPopup::finish(Result result)
{
    m_result = result;
    accept();
}

} // namespace bh::ui
