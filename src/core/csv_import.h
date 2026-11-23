#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace bh::core {

// Чтение CSV (выгрузки из других программ и таблиц) и перевод в XLSX,
// чтобы дальше работать с ним как с обычной рабочей копией.
class CsvImport
{
public:
    enum class Encoding { Utf8, Windows1251 };

    struct Table
    {
        Encoding encoding = Encoding::Utf8;
        QChar delimiter = QLatin1Char(';');
        QVector<QStringList> rows;
    };

    // Кодировка: UTF-8 (с BOM или без), иначе Windows-1251.
    static Encoding detectEncoding(const QByteArray &data);
    // Разделитель: «;», «,» или табуляция — тот, что даёт одинаковое число столбцов в строках.
    static QChar detectDelimiter(const QString &text);
    // Разбор с кавычками ("…", "" внутри), переносами строк внутри кавычек.
    static QVector<QStringList> parse(const QString &text, QChar delimiter);

    static bool read(const QString &path, Table *table, QString *error = nullptr);

    // Значение ячейки: целые и дробные числа без ведущих нулей — числом
    // («2018», «131,6»), остальное («033», «2р20к», «б/н») — текстом.
    static QVariant cellValue(const QString &text);

    // Записывает таблицу в новый XLSX (лист «Лист1»).
    static bool writeXlsx(const Table &table, const QString &path, QString *error = nullptr);
};

} // namespace bh::core
