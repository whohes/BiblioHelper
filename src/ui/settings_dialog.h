#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace bh::ui {

// «Настройки»: размер интерфейса, папка программы, сколько хранить
// резервных копий, предлагать ли объединение книг после импорта.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // Записать выбранное в настройки. Возвращает true, если нужен перезапуск.
    bool apply();

private:
    QComboBox *m_scale = nullptr;
    QLineEdit *m_folder = nullptr;
    QSpinBox *m_keep = nullptr;
    QCheckBox *m_checkUpdates = nullptr;
    QCheckBox *m_offerBooks = nullptr;
};

} // namespace bh::ui
