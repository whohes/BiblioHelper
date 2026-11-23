#include "xlsx_workbook.h"

#include "sheet_xml_rows.h"

#include <OpenXLSX.hpp>
#include <XLXmlParser.hpp>

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace bh::io {

namespace {

// Доступ к XML листа и книги: в OpenXLSX нет вставки строк и правки
// автофильтра, а xmlDocument() защищён. Указатель на защищённый член,
// взятый через наследника, разрешён стандартом и ничего не копирует.
struct XmlAccess : OpenXLSX::XLXmlFile
{
    static OpenXLSX::XMLDocument &of(OpenXLSX::XLXmlFile &file)
    {
        using Getter = OpenXLSX::XMLDocument &(OpenXLSX::XLXmlFile::*)();
        constexpr Getter getter = &XmlAccess::xmlDocument;
        return (file.*getter)();
    }
};

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

bool isAsciiPath(const QString &path)
{
    for (const QChar c : path) {
        if (c.unicode() > 0x7e)
            return false;
    }
    return !path.isEmpty();
}

// Короткое имя Windows (C:\Users\D8F3~1\...) не содержит русских букв.
QString shortPath(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring wide = QDir::toNativeSeparators(path).toStdWString();
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetShortPathNameW(wide.c_str(), buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        return QDir::fromNativeSeparators(QString::fromWCharArray(buffer, static_cast<int>(length)));
#else
    Q_UNUSED(path);
#endif
    return {};
}

bool copyFile(const QString &from, const QString &to, QString *error)
{
    QFile source(from);
    if (!source.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Не удалось прочитать файл «%1»: %2")
                            .arg(QDir::toNativeSeparators(from), source.errorString()));
        return false;
    }
    // QSaveFile пишет во временный файл и подменяет целевой только после
    // успешной записи: при сбое прежний файл остаётся целым.
    QSaveFile target(to);
    if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) != source.size()
        || !target.commit()) {
        setError(error, QStringLiteral("Не удалось записать файл «%1»: %2")
                            .arg(QDir::toNativeSeparators(to), target.errorString()));
        return false;
    }
    return true;
}

QVariant toVariant(OpenXLSX::XLCellValue value)
{
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
    return {};
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

QString XlsxWorkbook::stagingRoot()
{
    QStringList candidates{QDir::tempPath()};
    const QString temp = shortPath(QDir::tempPath());
    if (!temp.isEmpty())
        candidates << temp;
    const QString programData = qEnvironmentVariable("ProgramData");
    if (!programData.isEmpty())
        candidates << programData + QStringLiteral("/BiblioHelper/tmp");

    for (const QString &dir : candidates) {
        if (isAsciiPath(dir) && QDir().mkpath(dir))
            return dir;
    }
    return {};
}

QString XlsxWorkbook::nextStagedPath()
{
    return m_staging->filePath(QStringLiteral("work%1.xlsx").arg(++m_stagedCount));
}

bool XlsxWorkbook::open(const QString &path, QString *error)
{
    close();
    const QString root = stagingRoot();
    if (root.isEmpty()) {
        setError(error, QStringLiteral("Не найдена служебная папка для работы с файлом "
                                       "(во временной папке Windows есть русские буквы)."));
        return false;
    }
    auto staging = std::make_unique<QTemporaryDir>(root + QStringLiteral("/bh-XXXXXX"));
    if (!staging->isValid()) {
        setError(error, QStringLiteral("Не удалось создать служебную папку в «%1»")
                            .arg(QDir::toNativeSeparators(root)));
        return false;
    }
    m_staging = std::move(staging);
    m_stagedCount = 0;

    const QString staged = nextStagedPath();
    if (!copyFile(path, staged, error)) {
        m_staging.reset();
        return false;
    }
    try {
        auto doc = std::make_unique<OpenXLSX::XLDocument>();
        doc->open(toUtf8(QDir::toNativeSeparators(staged)));
        m_doc = std::move(doc);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось открыть файл: %1").arg(fromUtf8(e.what())));
        m_staging.reset();
        return false;
    }
}

void XlsxWorkbook::close()
{
    if (m_doc) {
        try {
            if (m_doc->isOpen())
                m_doc->close();
        } catch (const std::exception &) {
            // Закрытие без сохранения: ошибки здесь не влияют на данные.
        }
        m_doc.reset();
    }
    // Служебная папка удаляется только после закрытия документа.
    m_staging.reset();
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
        // findCell не создаёт строку/ячейку в XML, если её нет: чтение
        // не должно менять файл.
        auto cell = ws.findCell(static_cast<uint32_t>(row), static_cast<uint16_t>(column));
        if (cell.empty())
            return {};
        return toVariant(cell.value());
    } catch (const std::exception &) {
    }
    return {};
}

SheetSize XlsxWorkbook::sheetSize(const QString &sheet) const
{
    if (!isOpen())
        return {};
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        return {static_cast<int>(ws.rowCount()), static_cast<int>(ws.columnCount())};
    } catch (const std::exception &) {
        return {};
    }
}

QVector<SheetRow> XlsxWorkbook::readRows(const QString &sheet, int firstRow, int lastRow,
                                         int columnCount, QString *error) const
{
    QVector<SheetRow> result;
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return result;
    }
    if (firstRow < 1 || lastRow < firstRow || columnCount < 1)
        return result;
    lastRow = std::min(lastRow, 1048576);
    columnCount = std::min(columnCount, 16384);
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        auto range = ws.rows(static_cast<uint32_t>(firstRow), static_cast<uint32_t>(lastRow));
        for (auto it = range.begin(); it != range.end(); ++it) {
            // Проверка до разыменования: иначе OpenXLSX создаст пустую строку.
            if (!it.rowExists())
                continue;
            auto &row = *it;
            SheetRow out;
            out.row = static_cast<int>(row.rowNumber());
            out.values.resize(columnCount);
            bool any = false;
            for (int c = 1; c <= columnCount; ++c) {
                auto cell = row.findCell(static_cast<uint16_t>(c));
                if (cell.empty())
                    continue;
                out.values[c - 1] = toVariant(cell.value());
                any = any || out.values[c - 1].isValid();
            }
            if (any)
                result.push_back(std::move(out));
        }
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось прочитать лист «%1»: %2").arg(sheet, fromUtf8(e.what())));
        result.clear();
    }
    return result;
}

bool XlsxWorkbook::appendRowsAfter(const QString &sheet, int afterRow, int count, int templateRow,
                                   QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    if (afterRow < 1 || count < 1 || afterRow + count > 1048576) {
        setError(error, QStringLiteral("Неверный номер строки"));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        sheetxml::insertRowsAfter(XmlAccess::of(ws).document_element(), afterRow, count, templateRow);
        auto wb = m_doc->workbook();
        sheetxml::requestFullCalcOnLoad(XmlAccess::of(wb).document_element());
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось добавить строки: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::replaceSheetText(const QString &sheet, const QVector<QStringList> &rows, QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        sheetxml::replaceSheetData(XmlAccess::of(ws).document_element(), rows);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось записать служебный лист: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::cellHasFormula(const QString &sheet, int row, int column) const
{
    if (!isOpen() || !validCoordinates(row, column))
        return false;
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        auto cell = ws.findCell(static_cast<uint32_t>(row), static_cast<uint16_t>(column));
        return !cell.empty() && cell.hasFormula();
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
        auto cell = ws.findCell(static_cast<uint32_t>(row), static_cast<uint16_t>(column));
        return cell.empty() ? QString() : fromUtf8(cell.formula().get());
    } catch (const std::exception &) {
        return {};
    }
}

bool XlsxWorkbook::copyCellStyle(const QString &sheet, int row, int fromColumn, int toColumn, QString *error)
{
    if (!isOpen() || !validCoordinates(row, fromColumn) || !validCoordinates(row, toColumn)) {
        setError(error, QStringLiteral("Неверная ячейка: строка %1").arg(row));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        auto source = ws.findCell(static_cast<uint32_t>(row), static_cast<uint16_t>(fromColumn));
        if (source.empty())
            return true;  // у соседней ячейки нет оформления — оставляем обычное
        auto target = ws.cell(static_cast<uint32_t>(row), static_cast<uint16_t>(toColumn));
        target.setCellFormat(source.cellFormat());
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось оформить ячейку: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::setColumnWidth(const QString &sheet, int column, double width, QString *error)
{
    if (!isOpen() || !validCoordinates(1, column)) {
        setError(error, QStringLiteral("Неверный столбец %1").arg(column));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        ws.column(static_cast<uint16_t>(column)).setWidth(static_cast<float>(width));
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось задать ширину столбца: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::archiveRows(const QString &sheet, const QVector<int> &rows, int headerRow,
                               const QString &archiveSheet, QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    if (rows.isEmpty())
        return true;
    try {
        auto wb = m_doc->workbook();
        const std::string archiveName = toUtf8(archiveSheet);
        const bool created = !wb.sheetExists(archiveName);
        if (created)
            wb.addWorksheet(archiveName);
        auto source = wb.worksheet(toUtf8(sheet));
        auto archive = wb.worksheet(archiveName);
        pugi::xml_node from = XmlAccess::of(source).document_element();
        pugi::xml_node to = XmlAccess::of(archive).document_element();
        int next = sheetxml::lastRowNumber(to) + 1;
        if (created) {
            sheetxml::copyColumnWidths(from, to);
            sheetxml::copyRows(from, to, {headerRow}, 1);
            next = 2;
        }
        QVector<int> sorted = rows;
        std::sort(sorted.begin(), sorted.end());
        sheetxml::copyRows(from, to, sorted, next);
        sheetxml::deleteRows(from, sorted);
        auto wbDoc = XmlAccess::of(wb).document_element();
        sheetxml::deleteRowsInDefinedNames(wbDoc, sheet, sorted);
        sheetxml::requestFullCalcOnLoad(wbDoc);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось перенести строки в архив: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::setReportLayout(const QString &sheet, int headerRow, const QString &filterRef, QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        sheetxml::setReportLayout(XmlAccess::of(ws).document_element(), headerRow, filterRef);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось настроить печать: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

bool XlsxWorkbook::extendAutoFilter(const QString &sheet, int headerRow, int lastColumn, QString *error)
{
    if (!isOpen()) {
        setError(error, QStringLiteral("Файл не открыт"));
        return false;
    }
    try {
        auto ws = m_doc->workbook().worksheet(toUtf8(sheet));
        sheetxml::extendAutoFilter(XmlAccess::of(ws).document_element(), headerRow, lastColumn);
        return true;
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось изменить автофильтр: %1").arg(fromUtf8(e.what())));
        return false;
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
    // Сначала OpenXLSX сохраняет в служебную папку (латинский путь),
    // затем готовый файл копируется на место через Qt.
    const QString staged = nextStagedPath();
    try {
        m_doc->saveAs(toUtf8(QDir::toNativeSeparators(staged)), true);
        return copyFile(staged, path, error);
    } catch (const std::exception &e) {
        setError(error, QStringLiteral("Не удалось сохранить файл: %1").arg(fromUtf8(e.what())));
        return false;
    }
}

} // namespace bh::io
