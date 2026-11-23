#pragma once

#include <QString>
#include <QVariant>
#include <QVector>

namespace bh::core {

// Чтение старых файлов Excel 97–2003 (.xls) и перевод в XLSX.
// Переносятся только значения: оформление XLS не сохраняется
// (программа предупреждает об этом при открытии).
class XlsImport
{
public:
    struct Sheet
    {
        QString name;
        QVector<QVector<QVariant>> rows;  // строки с 1-й, столбцы с A
    };

    static bool read(const QString &path, QVector<Sheet> *sheets, QString *error = nullptr);
    static bool writeXlsx(const QVector<Sheet> &sheets, const QString &path, QString *error = nullptr);
};

} // namespace bh::core
