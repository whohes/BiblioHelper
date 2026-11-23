#pragma once

#include "core/fund_export.h"
#include "core/fund_sheet.h"

#include <QDialog>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QListWidget;

namespace bh::ui {

// Окно «Экспорт»: что выгрузить (весь фонд, показанное сейчас, выбывшие),
// какие столбцы и в каком формате. Само ничего не пишет.
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Scope { ActiveAll, ActiveShown, WithdrawnAll, WithdrawnShown, Everything };

    struct Counts
    {
        int activeAll = 0;
        int activeShown = 0;
        int withdrawnAll = 0;
        int withdrawnShown = 0;
    };

    ExportDialog(const core::FundSheet &sheet, const Counts &counts, Scope initial, QWidget *parent = nullptr);

    Scope scope() const;
    QVector<int> columns() const;
    core::FundExport::Format format() const;
    bool openAfterSave() const;

    static QString scopeTitle(Scope scope);

private:
    void resetColumns();
    bool scopeHasWithdrawn() const;

    const core::FundSheet &m_sheet;
    QButtonGroup *m_scopes;
    QListWidget *m_columns;
    QComboBox *m_format;
    QCheckBox *m_open;
};

} // namespace bh::ui
