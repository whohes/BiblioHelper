#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <memory>

class QTemporaryDir;

namespace OpenXLSX {
class XLDocument;
}

namespace bh::io {

// Одна непустая строка листа: номер строки Excel и значения столбцов 1..N.
struct SheetRow
{
    int row = 0;
    QVector<QVariant> values;
};

struct SheetSize
{
    int rows = 0;
    int columns = 0;
};

// Тонкая обёртка над OpenXLSX для рабочей копии XLSX.
//
// OpenXLSX правит XML листа на месте и не пересобирает остальные части
// файла (стили, автофильтр, параметры печати, другие листы), поэтому
// оформление пользовательского документа сохраняется. Исключения
// библиотеки не выходят наружу: методы возвращают false и текст ошибки.
//
// OpenXLSX на Windows открывает файлы через «узкие» функции (fopen),
// которые не понимают русские буквы в пути. Поэтому библиотека всегда
// работает с копией файла в служебной папке с латинским путём, а при
// сохранении результат переносится в нужное место средствами Qt.
//
// Номера строк и столбцов здесь — координаты ячеек Excel (с 1), а не
// идентификаторы записей. Записи адресуются по внутренним ID в core/.
class XlsxWorkbook
{
public:
    XlsxWorkbook();
    ~XlsxWorkbook();

    XlsxWorkbook(const XlsxWorkbook &) = delete;
    XlsxWorkbook &operator=(const XlsxWorkbook &) = delete;

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const;

    QStringList sheetNames() const;

    // Значение ячейки: пусто, bool, qint64, double или QString.
    // Для формулы возвращается последнее вычисленное значение.
    QVariant cellValue(const QString &sheet, int row, int column) const;
    // Размер занятой области листа.
    SheetSize sheetSize(const QString &sheet) const;

    // Непустые строки firstRow..lastRow, столбцы 1..columnCount.
    // Ничего не меняет в файле (пустые строки и ячейки не создаются).
    QVector<SheetRow> readRows(const QString &sheet, int firstRow, int lastRow, int columnCount,
                               QString *error = nullptr) const;

    // Вставляет count пустых строк после afterRow с оформлением строки
    // templateRow. Строки ниже (итоги) сдвигаются, итоговые формулы и
    // автофильтр растягиваются на новые строки, Excel пересчитает формулы
    // при открытии. Подробности — io/sheet_xml_rows.h.
    bool appendRowsAfter(const QString &sheet, int afterRow, int count, int templateRow,
                         QString *error = nullptr);

    // Полностью заменить содержимое листа текстовой таблицей (служебный лист ID).
    bool replaceSheetText(const QString &sheet, const QVector<QStringList> &rows, QString *error = nullptr);

    bool cellHasFormula(const QString &sheet, int row, int column) const;
    QString cellFormula(const QString &sheet, int row, int column) const;

    // Записать значение (заменяет и формулу, если она была).
    // Оформление ячейки (шрифт, границы, формат) — как у соседней ячейки той же строки.
    bool copyCellStyle(const QString &sheet, int row, int fromColumn, int toColumn, QString *error = nullptr);
    // Перенести строки листа на лист archiveSheet (создаётся при первом
    // переносе: строка заголовков и ширины столбцов — как у листа) и удалить
    // их с листа. Итоги, автофильтр и прочие диапазоны сжимаются.
    bool archiveRows(const QString &sheet, const QVector<int> &rows, int headerRow, const QString &archiveSheet,
                     QString *error = nullptr);

    // Альбомная печать по ширине, закреплённая шапка и автофильтр (для отчётов).
    bool setReportLayout(const QString &sheet, int headerRow, const QString &filterRef, QString *error = nullptr);
    // Ширина столбца в символах (как в Excel).
    bool setColumnWidth(const QString &sheet, int column, double width, QString *error = nullptr);
    // Расширить автофильтр (начинается в headerRow) вправо до lastColumn.
    bool extendAutoFilter(const QString &sheet, int headerRow, int lastColumn, QString *error = nullptr);
    bool setCellValue(const QString &sheet, int row, int column, const QVariant &value,
                      QString *error = nullptr);

    // Служебный лист, невидимый в Excel даже через «Показать».
    bool ensureVeryHiddenSheet(const QString &name, QString *error = nullptr);
    bool isVeryHidden(const QString &sheet) const;

    // Сохраняет документ в новый файл; открытый файл не перезаписывается.
    bool saveAs(const QString &path, QString *error = nullptr);

    // Папка с латинским путём, где лежат копии для OpenXLSX
    // (пустая строка, если подходящей папки нет).
    static QString stagingRoot();

private:
    QString nextStagedPath();

    std::unique_ptr<OpenXLSX::XLDocument> m_doc;
    std::unique_ptr<QTemporaryDir> m_staging;
    int m_stagedCount = 0;
};

} // namespace bh::io
