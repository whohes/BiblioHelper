#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

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
    // «Документы\BiblioHelper\Экспорт».
    static QString exportDirectory();

    // Резервная копия файла перед опасным действием:
    // «Резервные копии\<имя файла>\2026-11-23_10-15 перед очисткой.xlsx».
    // books / withdrawn — сколько в копии действующих и выбывших экземпляров
    // (для окна «Прошлые версии»), -1 — неизвестно.
    // Возвращает путь к копии или пустую строку и текст ошибки.
    static QString backup(const QString &file, const QString &reason, QString *error = nullptr,
                          int books = -1, int withdrawn = -1);

    struct BackupInfo
    {
        QString path;
        QDateTime createdAt;
        QString reason;      // «перед сохранением», «перед очисткой»…
        int books = -1;      // -1 — неизвестно
        int withdrawn = -1;
        qint64 size = 0;
        int sequence = 1;    // «(2)» в имени — вторая копия в ту же секунду
    };
    // Резервные копии рабочего файла, новые первыми.
    static QVector<BackupInfo> backups(const QString &workFile);
    // Папка резервных копий рабочего файла.
    static QString backupDirectoryFor(const QString &workFile);
    // Причина автоматической копии при каждом «Сохранить».
    static QString saveBackupReason();
    // Оставляет keep последних автоматических копий («перед сохранением»);
    // копии перед очисткой, восстановлением и т. п. не удаляет. Возвращает число удалённых.
    static int pruneBackups(const QString &workFile, int keep = 20);

private:
    QString m_dir;
};

} // namespace bh::core
