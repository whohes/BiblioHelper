#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace bh::core {

// Журнал основных действий: «Документы\BiblioHelper\Журнал\журнал.jsonl».
// Одна строка — одно действие в формате JSON (JSON Lines), так файл можно
// дописывать без перечитывания и открыть в Блокноте. Когда файл вырастает
// больше maxBytes, он переименовывается в «журнал до <дата>.jsonl»
// и начинается новый.
struct LogEntry
{
    QDateTime time;
    QString action;   // ключ: open, import, add, edit, withdraw, return, undo,
                      // save, report, cleanup, export, restore, error
    QString fund;     // имя исходного файла фонда («Фонд 2025.xlsx»)
    QString details;  // понятное описание для человека
    int count = -1;   // сколько экземпляров затронуто, -1 — неважно

    // «Выбытие», «Изменение»… для показа в таблице.
    QString actionTitle() const;
};

class ActionLog
{
public:
    // path — файл журнала; пустая строка = по умолчанию.
    explicit ActionLog(const QString &path = {});

    static QString defaultPath();
    QString path() const { return m_path; }

    // Дописывает запись (время = сейчас, если не задано). Ошибки записи журнала
    // не должны мешать работе, поэтому false лишь сообщает о них.
    bool append(LogEntry entry, QString *error = nullptr) const;
    bool append(const QString &action, const QString &fund, const QString &details, int count = -1) const;

    // Последние записи, новые первыми; fund — только этого фонда (пусто — все).
    // Повреждённые строки пропускаются.
    QVector<LogEntry> read(int limit = 500, const QString &fund = {}) const;

    static QString actionTitle(const QString &action);

    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }

private:
    void rotateIfNeeded() const;

    QString m_path;
    qint64 m_maxBytes = 2 * 1024 * 1024;
};

} // namespace bh::core
