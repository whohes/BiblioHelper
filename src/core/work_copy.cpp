#include "work_copy.h"

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
