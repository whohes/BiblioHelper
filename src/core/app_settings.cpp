#include "app_settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace bh::core::AppSettings {

namespace {

const char kDataRoot[] = "settings/dataRoot";
const char kScale[] = "settings/interfaceScale";
const char kKeepBackups[] = "settings/keepSaveBackups";
const char kOfferBooks[] = "settings/offerBookGroups";

} // namespace

QString defaultDataRoot()
{
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty())
        documents = QDir::homePath();
    return QDir(documents).filePath(QStringLiteral("BiblioHelper"));
}

QString dataRoot()
{
    const QString custom = QSettings().value(QLatin1String(kDataRoot)).toString();
    return custom.isEmpty() ? defaultDataRoot() : QDir::cleanPath(custom);
}

void setDataRoot(const QString &path)
{
    QSettings settings;
    if (path.isEmpty() || QDir::cleanPath(path) == QDir::cleanPath(defaultDataRoot()))
        settings.remove(QLatin1String(kDataRoot));
    else
        settings.setValue(QLatin1String(kDataRoot), QDir::cleanPath(path));
}

int interfaceScale()
{
    const int value = QSettings().value(QLatin1String(kScale), 100).toInt();
    return value == 125 || value == 150 ? value : 100;
}

void setInterfaceScale(int percent)
{
    QSettings().setValue(QLatin1String(kScale), percent == 125 || percent == 150 ? percent : 100);
}

int keepSaveBackups()
{
    return std::clamp(QSettings().value(QLatin1String(kKeepBackups), 20).toInt(), 1, 500);
}

void setKeepSaveBackups(int count)
{
    QSettings().setValue(QLatin1String(kKeepBackups), std::clamp(count, 1, 500));
}

bool offerBookGroupsAfterImport()
{
    return QSettings().value(QLatin1String(kOfferBooks), true).toBool();
}

void setOfferBookGroupsAfterImport(bool offer)
{
    QSettings().setValue(QLatin1String(kOfferBooks), offer);
}

QString supportEmail()
{
    return QStringLiteral("bibliohelpers@gmail.com");
}

} // namespace bh::core::AppSettings
