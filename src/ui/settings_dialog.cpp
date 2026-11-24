#include "settings_dialog.h"

#include "core/app_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

namespace bh::ui {

namespace {

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("emptyState"));
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    using namespace core;
    setWindowTitle(QStringLiteral("Настройки"));
    constexpr int kWidth = 720;
    setFixedWidth(kWidth);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(4);
    const int textWidth = kWidth - 40;
    auto section = [&](const QString &title, QLayout *control, const QString &text) {
        auto *label = new QLabel(title, this);
        label->setObjectName(QStringLiteral("sectionTitle"));
        layout->addWidget(label);
        layout->addLayout(control);
        if (!text.isEmpty()) {
            QLabel *h = hint(text, this);
            // Перенос строк учитывается в высоте окна сразу (иначе текст обрезается).
            h->setMinimumHeight(h->heightForWidth(textWidth));
            layout->addWidget(h);
        }
        layout->addSpacing(10);
    };

    m_scale = new QComboBox(this);
    for (int percent : {100, 125, 150})
        m_scale->addItem(QStringLiteral("%1 %").arg(percent), percent);
    m_scale->setCurrentIndex(m_scale->findData(AppSettings::interfaceScale()));
    m_scale->setMinimumWidth(140);
    auto *scaleRow = new QHBoxLayout;
    scaleRow->addWidget(m_scale);
    scaleRow->addStretch(1);
    section(QStringLiteral("Размер интерфейса"), scaleRow,
            QStringLiteral("Крупнее — удобнее читать. Изменится после перезапуска программы."));

    auto *folderRow = new QHBoxLayout;
    m_folder = new QLineEdit(QDir::toNativeSeparators(AppSettings::dataRoot()), this);
    m_folder->setCursorPosition(0);
    auto *browse = new QPushButton(QStringLiteral("Выбрать…"), this);
    auto *reset = new QPushButton(QStringLiteral("По умолчанию"), this);
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    folderRow->addWidget(reset);
    section(QStringLiteral("Папка программы"), folderRow,
            QStringLiteral("Здесь хранятся рабочие файлы, резервные копии, отчёты, экспорт и журнал. "
                           "Уже созданные файлы остаются на прежнем месте."));

    m_keep = new QSpinBox(this);
    m_keep->setRange(1, 500);
    m_keep->setValue(AppSettings::keepSaveBackups());
    m_keep->setSuffix(QStringLiteral(" копий"));
    m_keep->setMinimumWidth(140);
    auto *keepRow = new QHBoxLayout;
    keepRow->addWidget(m_keep);
    keepRow->addStretch(1);
    section(QStringLiteral("Резервные копии"), keepRow,
            QStringLiteral("Сколько последних копий «перед сохранением» хранить для каждого файла. "
                           "Копии перед очисткой и восстановлением не удаляются."));

    m_offerBooks = new QCheckBox(QStringLiteral("После импорта предлагать объединить одинаковые книги"), this);
    m_offerBooks->setChecked(AppSettings::offerBookGroupsAfterImport());
    auto *importRow = new QHBoxLayout;
    importRow->addWidget(m_offerBooks);
    section(QStringLiteral("Импорт"), importRow, {});

    m_checkUpdates = new QCheckBox(QStringLiteral("Проверять раз в день, не вышла ли новая версия"), this);
    m_checkUpdates->setChecked(AppSettings::checkUpdates());
    auto *updatesRow = new QHBoxLayout;
    updatesRow->addWidget(m_checkUpdates);
    section(QStringLiteral("Обновления"), updatesRow,
            QStringLiteral("Нужен интернет. Если вышла новая версия, вверху окна появится жёлтая полоса. "
                           "Без интернета программа работает как обычно."));

    auto *buttons = new QDialogButtonBox(this);
    auto *ok = buttons->addButton(QStringLiteral("Сохранить"), QDialogButtonBox::AcceptRole);
    ok->setProperty("primary", true);
    ok->style()->unpolish(ok);
    ok->style()->polish(ok);
    ok->setDefault(true);
    buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка программы"),
                                                              QDir::fromNativeSeparators(m_folder->text()));
        if (!dir.isEmpty())
            m_folder->setText(QDir::toNativeSeparators(dir));
    });
    connect(reset, &QPushButton::clicked, this,
            [this] { m_folder->setText(QDir::toNativeSeparators(core::AppSettings::defaultDataRoot())); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool SettingsDialog::apply()
{
    using namespace core;
    const int scale = m_scale->currentData().toInt();
    const bool restart = scale != AppSettings::interfaceScale();
    AppSettings::setInterfaceScale(scale);
    const QString folder = QDir::fromNativeSeparators(m_folder->text().trimmed());
    AppSettings::setDataRoot(folder);
    if (!folder.isEmpty())
        QDir().mkpath(folder);
    AppSettings::setKeepSaveBackups(m_keep->value());
    AppSettings::setOfferBookGroupsAfterImport(m_offerBooks->isChecked());
    AppSettings::setCheckUpdates(m_checkUpdates->isChecked());
    return restart;
}

} // namespace bh::ui
