#include "filter_popup.h"

#include "core/fund_sheet.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLocale>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace bh::ui {

FilterPopup::FilterPopup(const QString &columnTitle, const QVector<QPair<QString, int>> &values,
                         const QSet<QString> &checked, bool hasFilter,
                         const models::ColumnCondition *condition, QWidget *parent)
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

    // Условие: «содержит», «больше», «между»… (как «Текстовые фильтры» в Excel).
    using Op = models::ColumnCondition::Op;
    auto *conditionBox = new QGroupBox(QStringLiteral("Условие"), this);
    auto *conditionLayout = new QVBoxLayout(conditionBox);
    m_op = new QComboBox(conditionBox);
    m_op->setMinimumHeight(36);
    m_op->addItem(QStringLiteral("— без условия, по списку ниже —"), -1);
    for (Op op : {Op::Contains, Op::NotContains, Op::StartsWith, Op::Equals, Op::Greater, Op::GreaterEqual,
                  Op::Less, Op::LessEqual, Op::Between, Op::Empty, Op::NotEmpty}) {
        m_op->addItem(models::ColumnCondition::label(op), static_cast<int>(op));
    }
    conditionLayout->addWidget(m_op);
    m_valuesBox = new QWidget(conditionBox);
    auto *valuesRow = new QHBoxLayout(m_valuesBox);
    valuesRow->setContentsMargins(0, 0, 0, 0);
    m_value1 = new QLineEdit(m_valuesBox);
    m_value2 = new QLineEdit(m_valuesBox);
    m_and = new QLabel(QStringLiteral("и"), m_valuesBox);
    for (auto *edit : {m_value1, m_value2})
        edit->setMinimumHeight(36);
    valuesRow->addWidget(m_value1, 1);
    valuesRow->addWidget(m_and);
    valuesRow->addWidget(m_value2, 1);
    conditionLayout->addWidget(m_valuesBox);
    layout->addWidget(conditionBox);
    if (condition) {
        m_op->setCurrentIndex(m_op->findData(static_cast<int>(condition->op)));
        QLocale ru(QLocale::Russian);
        ru.setNumberOptions(QLocale::OmitGroupSeparator);
        if (models::ColumnCondition::isNumeric(condition->op)) {
            m_value1->setText(ru.toString(condition->a));
            m_value2->setText(ru.toString(condition->b));
        } else {
            m_value1->setText(condition->text);
        }
    }

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
    clear->setEnabled(hasFilter || condition);
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
    connect(ok, &QPushButton::clicked, this, [this] {
        if (m_op->currentData().toInt() < 0) {
            finish(Result::Apply);
        } else if (readCondition()) {
            finish(Result::ApplyCondition);
        }
    });
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
    // Подключается после создания всех полей: setCurrentIndex выше уже
    // вызывал сигнал, а список значений тогда ещё не существовал.
    connect(m_op, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &FilterPopup::updateConditionInputs);
    updateConditionInputs();
    if (condition)
        m_value1->setFocus();
    else
        m_search->setFocus();
}

void FilterPopup::updateConditionInputs()
{
    const int data = m_op->currentData().toInt();
    const bool none = data < 0;
    const auto op = static_cast<models::ColumnCondition::Op>(data);
    m_valuesBox->setVisible(!none && models::ColumnCondition::needsValue(op));
    const bool between = !none && op == models::ColumnCondition::Op::Between;
    m_and->setVisible(between);
    m_value2->setVisible(between);
    m_value1->setPlaceholderText(!none && models::ColumnCondition::isNumeric(op) ? QStringLiteral("число")
                                                                                 : QStringLiteral("текст"));
    m_value2->setPlaceholderText(QStringLiteral("число"));
    // Список значений не действует, когда выбрано условие.
    m_list->setEnabled(none);
    m_selectAll->setEnabled(none);
    m_search->setEnabled(none);
}

bool FilterPopup::readCondition()
{
    using models::ColumnCondition;
    m_condition = {};
    m_condition.op = static_cast<ColumnCondition::Op>(m_op->currentData().toInt());
    if (!ColumnCondition::needsValue(m_condition.op))
        return true;
    if (!ColumnCondition::isNumeric(m_condition.op)) {
        m_condition.text = m_value1->text().trimmed();
        if (m_condition.text.isEmpty()) {
            m_hint->setText(QStringLiteral("Введите текст для условия."));
            m_value1->setFocus();
            return false;
        }
        return true;
    }
    auto parse = [](const QString &text, double *out) {
        QString t = text.trimmed();
        t.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        *out = t.toDouble(&ok);
        return ok;
    };
    if (!parse(m_value1->text(), &m_condition.a)) {
        m_hint->setText(QStringLiteral("Введите число, например 2000 или 12,50."));
        m_value1->setFocus();
        return false;
    }
    if (m_condition.op == ColumnCondition::Op::Between && !parse(m_value2->text(), &m_condition.b)) {
        m_hint->setText(QStringLiteral("Введите второе число для условия «между»."));
        m_value2->setFocus();
        return false;
    }
    return true;
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
