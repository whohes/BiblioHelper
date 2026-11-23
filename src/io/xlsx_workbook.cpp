#include "xlsx_workbook.h"

#include <OpenXLSX.hpp>

#include <exception>

namespace bh::io {

namespace {

std::string toUtf8(const QString &s)
{
    const QByteArray bytes = s.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QString fromUtf8(const std::string &s)
{
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool validCoordinates(int row, int column)
{
    // Ограничения формата XLSX.
    return row >= 1 && row <= 1048576 && column >= 1 && column <= 16384;
}

} // namespace

XlsxWorkbook::XlsxWorkbook() = default;

XlsxWorkbook::~XlsxWorkbook()
{
    close();
}

bool XlsxWorkbook::open(const QString &path, QString *error)
{
    close();
    try {
        auto doc = std::make_unique<OpenXLSX::XLDocument>();
        doc->open(toUtf8(path));
        m_doc = std::move(doc);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось открыть файл: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

void XlsxWorkbook::close()
{
    if (!m_doc)
        return;
    try {
        if (m_doc->isOpen())
            m_doc->close();
    } catch (const std::exception &) {
        // Закрытие без сохранения: ошибки здесь не влияют на данные.
    }
    m_doc.reset();
}

bool XlsxWorkbook::isOpen() const
{
    return m_doc && m_doc->isOpen();
}

QStringList XlsxWorkbook::sheetNames() const
{
    QStringList result;
    if (!isOpen())
        return result;
    for (const auto &name : m_doc->workbook().worksheetNames())
        result << fromUtf8(name);
    return result;
}

QVariant XlsxWorkbook::cellValue(const QString &sheet, int row, int column) const
{
    if (!isOpen() || !validCoordinates(row, column))
        return {};
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        OpenXLSX::XLCellValue value =
            ws.cell(static_cast<uint32_t>(row), static_cast<uint16_t>(column)).value();
        switch (value.type()) {
        case OpenXLSX::XLValueType::Empty:
            return {};
        case OpenXLSX::XLValueType::Boolean:
            return value.get<bool>();
        case OpenXLSX::XLValueType::Integer:
            return static_cast<qint64>(value.get<int64_t>());
        case OpenXLSX::XLValueType::Float:
            return value.get<double>();
        case OpenXLSX::XLValueType::String:
        case OpenXLSX::XLValueType::Error:
            return fromUtf8(value.getString());
        }
    } catch (const std::exception &) {
    }
    return {};
}

bool XlsxWorkbook::cellHasFormula(const QString &sheet, int row, int column) const
{
    if (!isOpen() || !validCoordinates(row, column))
        return false;
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        return ws.cell(static_cast<uint32_t>(row), static_cast<uint16_t>(column)).hasFormula();
    } catch (const std::exception &) {
        return false;
    }
}

QString XlsxWorkbook::cellFormula(const QString &sheet, int row, int column) const
{
    if (!cellHasFormula(sheet, row, column))
        return {};
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        return fromUtf8(
            ws.cell(static_cast<uint32_t>(row), static_cast<uint16_t>(column)).formula().get());
    } catch (const std::exception &) {
        return {};
    }
}

bool XlsxWorkbook::setCellValue(const QString &sheet, int row, int column, const QVariant &value,
                                QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    if (!validCoordinates(row, column)) {
        setError(error, QStringLiteral("Неверная ячейка: строка %1, столбец %2").arg(row).arg(column));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        auto cell = ws.cell(static_cast<uint32_t>(row), static_cast<uint16_t>(column));
        if (cell.hasFormula())
            cell.formula().clear();
        auto &proxy = cell.value();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const int type = value.typeId();
#else
        const int type = static_cast<int>(value.type());
#endif
        if (!value.isValid() || value.isNull())
            proxy.clear();
        else if (type == QMetaType::Bool)
            proxy = value.toBool();
        else if (type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt
                 || type == QMetaType::ULongLong)
            proxy = static_cast<int64_t>(value.toLongLong());
        else if (type == QMetaType::Double || type == QMetaType::Float)
            proxy = value.toDouble();
        else
            proxy = toUtf8(value.toString());
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось записать ячейку: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::ensureVeryHiddenSheet(const QString &name, QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    try {
        auto wb = m_doc->workbook();
        const std::string utf8 = toUtf8(name);
        if (!wb.sheetExists(utf8))
            wb.addWorksheet(utf8);
        wb.worksheet(utf8).setVisibility(OpenXLSX::XLSheetState::VeryHidden);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось создать служебный лист: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::isVeryHidden(const QString &sheet) const
{
    if (!isOpen())
        return false;
    try {
        return m_doc->workbook().worksheet(toUtf8(sheet)).visibility()
               == OpenXLSX::XLSheetState::VeryHidden;
    } catch (const std::exception &) {
        return false;
    }
}

bool XlsxWorkbook::saveAs(const QString &path, QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    try {
        m_doc->saveAs(toUtf8(path), true);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось сохранить файл: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

} // namespace bh::io
