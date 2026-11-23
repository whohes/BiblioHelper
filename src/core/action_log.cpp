#include "core/action_log.h"

#include "core/work_copy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

namespace bh::core {

namespace {

const char kTimeFormat[] = "yyyy-MM-ddTHH:mm:ss";

} // namespace

QString LogEntry::actionTitle() const
{
    return ActionLog::actionTitle(action);
}

QString ActionLog::actionTitle(const QString &action)
{
    static const QHash<QString, QString> titles = {
        {QStringLiteral("open"), QStringLiteral("Открытие файла")},
        {QStringLiteral("import"), QStringLiteral("Импорт")},
        {QStringLiteral("add"), QStringLiteral("Добавление книги")},
        {QStringLiteral("edit"), QStringLiteral("Изменение")},
        {QStringLiteral("withdraw"), QStringLiteral("Выбытие")},
        {QStringLiteral("return"), QStringLiteral("Возврат в фонд")},
        {QStringLiteral("merge"), QStringLiteral("Объединение в книгу")},
        {QStringLiteral("split"), QStringLiteral("Разделение книги")},
        {QStringLiteral("undo"), QStringLiteral("Отмена действия")},
        {QStringLiteral("save"), QStringLiteral("Сохранение")},
        {QStringLiteral("report"), QStringLiteral("Отчёт по выбывшим")},
        {QStringLiteral("cleanup"), QStringLiteral("Очистка выбывших")},
        {QStringLiteral("export"), QStringLiteral("Экспорт")},
        {QStringLiteral("restore"), QStringLiteral("Восстановление версии")},
        {QStringLiteral("error"), QStringLiteral("Ошибка")},
    };
    return titles.value(action, action);
}

ActionLog::ActionLog(const QString &path)
    : m_path(path.isEmpty() ? defaultPath() : path)
{
}

QString ActionLog::defaultPath()
{
    return QDir::cleanPath(QDir(WorkCopies::defaultDirectory()).filePath(QStringLiteral("../Журнал/журнал.jsonl")));
}

void ActionLog::rotateIfNeeded() const
{
    const QFileInfo info(m_path);
    if (!info.exists() || info.size() < m_maxBytes)
        return;
    const QDir dir = info.dir();
    const QString stamp = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QString target = dir.filePath(QStringLiteral("%1 до %2.jsonl").arg(info.completeBaseName(), stamp));
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = dir.filePath(QStringLiteral("%1 до %2 (%3).jsonl").arg(info.completeBaseName(), stamp).arg(n));
    QFile::rename(m_path, target);
}

bool ActionLog::append(LogEntry entry, QString *error) const
{
    if (!entry.time.isValid())
        entry.time = QDateTime::currentDateTime();
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    rotateIfNeeded();

    QJsonObject object;
    object.insert(QStringLiteral("time"), entry.time.toString(QString::fromLatin1(kTimeFormat)));
    object.insert(QStringLiteral("action"), entry.action);
    if (!entry.fund.isEmpty())
        object.insert(QStringLiteral("fund"), entry.fund);
    if (!entry.details.isEmpty())
        object.insert(QStringLiteral("details"), entry.details);
    if (entry.count >= 0)
        object.insert(QStringLiteral("count"), entry.count);

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    line.append('\n');
    if (file.write(line) != line.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool ActionLog::append(const QString &action, const QString &fund, const QString &details, int count) const
{
    LogEntry entry;
    entry.action = action;
    entry.fund = fund;
    entry.details = details;
    entry.count = count;
    return append(entry);
}

QVector<LogEntry> ActionLog::read(int limit, const QString &fund) const
{
    QVector<LogEntry> result;
    QFile file(m_path);
    if (limit <= 0 || !file.open(QIODevice::ReadOnly))
        return result;
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (int i = lines.size() - 1; i >= 0 && result.size() < limit; --i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        const QJsonObject object = doc.object();
        LogEntry entry;
        entry.time = QDateTime::fromString(object.value(QStringLiteral("time")).toString(),
                                           QString::fromLatin1(kTimeFormat));
        entry.action = object.value(QStringLiteral("action")).toString();
        entry.fund = object.value(QStringLiteral("fund")).toString();
        entry.details = object.value(QStringLiteral("details")).toString();
        entry.count = object.value(QStringLiteral("count")).toInt(-1);
        if (!entry.time.isValid() || entry.action.isEmpty())
            continue;
        if (!fund.isEmpty() && entry.fund.compare(fund, Qt::CaseInsensitive) != 0)
            continue;
        result.push_back(entry);
    }
    return result;
}

} // namespace bh::core
