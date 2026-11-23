#include "work_copy.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace bh::core {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

WorkCopies::WorkCopies(const QString &dir)
    : m_dir(dir.isEmpty() ? defaultDirectory() : dir)
{
}

QString WorkCopies::defaultDirectory()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        documents = QDir::homePath();
    return QDir(documents).filePath(QStringLiteral("BiblioHelper/Рабочие файлы"));
}

QString WorkCopies::backupDirectory()
{
    return QDir::cleanPath(QDir(defaultDirectory()).filePath(QStringLiteral("../Резервные копии")));
}

QString WorkCopies::reportsDirectory()
{
    return QDir::cleanPath(QDir(defaultDirectory()).filePath(QStringLiteral("../Отчёты")));
}

QString WorkCopies::backup(const QString &file, const QString &reason, QString *error)
{
    const QFileInfo source(file);
    const QDir dir(QDir(backupDirectory()).filePath(source.completeBaseName()));
    if (!QDir().mkpath(dir.path())) {
        setError(error, QStringLiteral("Не удалось создать папку резервных копий: %1")
                            .arg(QDir::toNativeSeparators(dir.path())));
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    QString target;
    for (int n = 1;; ++n) {
        const QString name = n == 1 ? QStringLiteral("%1 %2.%3").arg(stamp, reason, source.suffix())
                                    : QStringLiteral("%1 %2 (%3).%4").arg(stamp, reason).arg(n).arg(source.suffix());
        target = dir.filePath(name);
        if (!QFileInfo::exists(target))
            break;
    }
    if (!QFile::copy(file, target)) {
        setError(error, QStringLiteral("Не удалось сделать резервную копию в %1").arg(QDir::toNativeSeparators(dir.path())));
        return {};
    }
    return target;
}

QString WorkCopies::create(const QString &original, QString *error) const
{
    const QFileInfo source(original);
    if (!source.isFile()) {
        setError(error, QStringLiteral("Файл не найден: %1").arg(QDir::toNativeSeparators(original)));
        return {};
    }
    if (!QDir().mkpath(m_dir)) {
        setError(error, QStringLiteral("Не удалось создать папку рабочих файлов: %1")
                            .arg(QDir::toNativeSeparators(m_dir)));
        return {};
    }

    const QString base = source.completeBaseName();
    const QString suffix = source.suffix().toLower();
    QString target;
    for (int n = 1;; ++n) {
        const QString name = n == 1
            ? QStringLiteral("%1 (рабочая копия).%2").arg(base, suffix)
            : QStringLiteral("%1 (рабочая копия %2).%3").arg(base).arg(n).arg(suffix);
        target = QDir(m_dir).filePath(name);
        if (!QFileInfo::exists(target))
            break;
    }

    if (!QFile::copy(original, target)) {
        setError(error, QStringLiteral("Не удалось скопировать файл в папку рабочих файлов: %1")
                            .arg(QDir::toNativeSeparators(m_dir)));
        return {};
    }
    // Копия исходника могла быть «только для чтения» — рабочую копию
    // программа должна уметь сохранять.
    QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner
                                      | QFileDevice::WriteUser);
    return target;
}

} // namespace bh::core
