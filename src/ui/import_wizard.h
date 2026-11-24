#pragma once

#include "core/fund_sheet.h"
#include "core/import_layout.h"

#include <QSet>
#include <QWizard>

namespace bh::io {
class XlsxWorkbook;
}

namespace bh::ui {

// Мастер импорта: открывается для файла, который программа видит впервые.
// 1. Лист и строка заголовков (с предпросмотром листа).
// 2. Что хранится в каждом столбце (подставлено по словарю названий).
// 3. Какие пометки значат «книги нет на месте» (если есть столбцы пометок).
// 4. Итог: сколько книг найдено и сколько будет отмечено выбывшими.
// Мастер ничего не меняет в файле: результат забирает вызывающий код.
class ImportWizard : public QWizard
{
    Q_OBJECT

public:
    // sheet — с какого листа начать (иначе — с листа, где больше всего книг).
    ImportWizard(const io::XlsxWorkbook &workbook, const QString &fileName, QWidget *parent = nullptr,
                 const QString &sheet = {});

    // Настройки чтения листа и прочитанный по ним фонд.
    core::ImportLayout layout() const { return m_layout; }
    const core::FundSheet &sheet() const { return m_sheet; }
    // Пометки (ключи normalizeMark), которые значат «нет на месте».
    QSet<QString> withdrawnMarks() const { return m_withdrawnMarks; }
    // Сколько экземпляров будет отмечено выбывшими по пометкам.
    int withdrawnCount() const;

private:
    friend class SheetPage;
    friend class ColumnsPage;
    friend class MarksPage;
    friend class SummaryPage;

    struct SheetInfo
    {
        QString name;
        int detectedHeaderRow = 0;  // 0 — не найдена
        int records = 0;
    };

    const io::XlsxWorkbook &m_workbook;
    QString m_fileName;
    QString m_preferredSheet;
    QVector<SheetInfo> m_sheets;
    core::ImportLayout m_layout;
    core::FundSheet m_sheet;
    QSet<QString> m_withdrawnMarks;
};

} // namespace bh::ui
