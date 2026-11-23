#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

namespace OpenXLSX {
class XLDocument;
}

namespace bh::io {

// Тонкая обёртка над OpenXLSX для рабочей копии XLSX.
//
// OpenXLSX правит XML листа на месте и не пересобирает остальные части
// файла (стили, автофильтр, параметры печати, другие листы), поэтому
// оформление пользовательского документа сохраняется. Исключения
// библиотеки не выходят наружу: методы возвращают false и текст ошибки.
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
    bool cellHasFormula(const QString &sheet, int row, int column) const;
    QString cellFormula(const QString &sheet, int row, int column) const;

    // Записать значение (заменяет и формулу, если она была).
    bool setCellValue(const QString &sheet, int row, int column, const QVariant &value,
                      QString *error = nullptr);

    // Служебный лист, невидимый в Excel даже через «Показать».
    bool ensureVeryHiddenSheet(const QString &name, QString *error = nullptr);
    bool isVeryHidden(const QString &sheet) const;

    // Сохраняет документ в новый файл; открытый файл не перезаписывается.
    bool saveAs(const QString &path, QString *error = nullptr);

private:
    std::unique_ptr<OpenXLSX::XLDocument> m_doc;
};

} // namespace bh::io
