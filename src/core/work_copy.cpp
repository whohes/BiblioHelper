#include "work_copy.h"

#include "csv_import.h"
#include "xls_import.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

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

QString WorkCopies::exportDirectory()
{
    return QDir::cleanPath(QDir(defaultDirectory()).filePath(QStringLiteral("../Экспорт")));
}

QString WorkCopies::reportsDirectory()
{
    return QDir::cleanPath(QDir(defaultDirectory()).filePath(QStringLiteral("../Отчёты")));
}

namespace {

const char kBackupIndex[] = "Сведения о копиях.json";
const char kBackupStamp[] = "yyyy-MM-dd_HH-mm-ss";

QJsonObject readIndex(const QDir &dir)
{
    QFile file(dir.filePath(QString::fromUtf8(kBackupIndex)));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeIndex(const QDir &dir, const QJsonObject &index)
{
    QSaveFile file(dir.filePath(QString::fromUtf8(kBackupIndex)));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(index).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

} // namespace

QString WorkCopies::backupDirectoryFor(const QString &workFile)
{
    return QDir(backupDirectory()).filePath(QFileInfo(workFile).completeBaseName());
}

QString WorkCopies::saveBackupReason()
{
    return QStringLiteral("перед сохранением");
}

QString WorkCopies::backup(const QString &file, const QString &reason, QString *error, int books, int withdrawn)
{
    const QFileInfo source(file);
    const QDir dir(backupDirectoryFor(file));
    if (!QDir().mkpath(dir.path())) {
        setError(error, QStringLiteral("Не удалось создать папку резервных копий: %1")
                            .arg(QDir::toNativeSeparators(dir.path())));
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(QString::fromLatin1(kBackupStamp));
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
    if (books >= 0) {
        QJsonObject index = readIndex(dir);
        QJsonObject info;
        info.insert(QStringLiteral("books"), books);
        info.insert(QStringLiteral("withdrawn"), withdrawn);
        index.insert(QFileInfo(target).fileName(), info);
        writeIndex(dir, index);
    }
    return target;
}

QVector<WorkCopies::BackupInfo> WorkCopies::backups(const QString &workFile)
{
    QVector<BackupInfo> result;
    const QDir dir(backupDirectoryFor(workFile));
    if (!dir.exists())
        return result;
    const QJsonObject index = readIndex(dir);
    static const QRegularExpression name(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}(?:-\\d{2})?) (.+?)(?: \\((\\d+)\\))?$"));
    for (const QFileInfo &file : dir.entryInfoList({QStringLiteral("*.xlsx")}, QDir::Files)) {
        BackupInfo info;
        info.path = file.absoluteFilePath();
        info.size = file.size();
        const auto match = name.match(file.completeBaseName());
        if (match.hasMatch()) {
            const QString stamp = match.captured(1);
            info.createdAt = QDateTime::fromString(stamp, stamp.size() > 16 ? QString::fromLatin1(kBackupStamp)
                                                                             : QStringLiteral("yyyy-MM-dd_HH-mm"));
            info.reason = match.captured(2);
            if (!match.captured(3).isEmpty())
                info.sequence = match.captured(3).toInt();
        }
        if (!info.createdAt.isValid())
            info.createdAt = file.lastModified();
        const QJsonObject meta = index.value(file.fileName()).toObject();
        info.books = meta.value(QStringLiteral("books")).toInt(-1);
        info.withdrawn = meta.value(QStringLiteral("withdrawn")).toInt(-1);
        result.push_back(info);
    }
    std::sort(result.begin(), result.end(), [](const BackupInfo &a, const BackupInfo &b) {
        return a.createdAt != b.createdAt ? a.createdAt > b.createdAt : a.sequence > b.sequence;
    });
    return result;
}

int WorkCopies::pruneBackups(const QString &workFile, int keep)
{
    const QDir dir(backupDirectoryFor(workFile));
    QJsonObject index = readIndex(dir);
    int kept = 0;
    int removed = 0;
    for (const BackupInfo &info : backups(workFile)) {
        if (info.reason != saveBackupReason())
            continue;
        if (++kept <= keep)
            continue;
        if (QFile::remove(info.path)) {
            index.remove(QFileInfo(info.path).fileName());
            ++removed;
        }
    }
    if (removed > 0)
        writeIndex(dir, index);
    return removed;
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
    const bool csv = source.suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) == 0
        || source.suffix().compare(QLatin1String("txt"), Qt::CaseInsensitive) == 0;
    const bool xls = source.suffix().compare(QLatin1String("xls"), Qt::CaseInsensitive) == 0;
    // CSV и XLS сразу переводятся в XLSX: дальше рабочая копия — обычный файл Excel.
    const QString suffix = csv || xls ? QStringLiteral("xlsx") : source.suffix().toLower();
    QString target;
    for (int n = 1;; ++n) {
        const QString name = n == 1
            ? QStringLiteral("%1 (рабочая копия).%2").arg(base, suffix)
            : QStringLiteral("%1 (рабочая копия %2).%3").arg(base).arg(n).arg(suffix);
        target = QDir(m_dir).filePath(name);
        if (!QFileInfo::exists(target))
            break;
    }

    if (csv) {
        CsvImport::Table table;
        if (!CsvImport::read(original, &table, error) || !CsvImport::writeXlsx(table, target, error)) {
            QFile::remove(target);
            return {};
        }
        return target;
    }
    if (xls) {
        QVector<XlsImport::Sheet> sheets;
        if (!XlsImport::read(original, &sheets, error) || !XlsImport::writeXlsx(sheets, target, error)) {
            QFile::remove(target);
            return {};
        }
        return target;
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
