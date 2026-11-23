#pragma once

#include <QString>

namespace bh::core {

// Рабочие копии: программа никогда не меняет исходный файл пользователя.
// При открытии файл копируется в папку рабочих файлов, и дальше
// программа читает и сохраняет только эту копию.
class WorkCopies
{
public:
    // dir — папка рабочих файлов; пустая строка = папка по умолчанию
    // («Документы\BiblioHelper\Рабочие файлы»).
    explicit WorkCopies(const QString &dir = {});

    QString directory() const { return m_dir; }

    // Копирует original в папку рабочих файлов под понятным именем
    // («Фонд 2025 (рабочая копия).xlsx», при совпадении — с номером).
    // Возвращает путь к копии или пустую строку и текст ошибки.
    QString create(const QString &original, QString *error = nullptr) const;

    static QString defaultDirectory();
    // «Документы\BiblioHelper\Резервные копии» и «…\Отчёты».
    static QString backupDirectory();
    static QString reportsDirectory();

    // Резервная копия файла перед опасным действием:
    // «Резервные копии\<имя файла>\2026-11-23_10-15 перед очисткой.xlsx».
    // Возвращает путь к копии или пустую строку и текст ошибки.
    static QString backup(const QString &file, const QString &reason, QString *error = nullptr);

private:
    QString m_dir;
};

} // namespace bh::core
