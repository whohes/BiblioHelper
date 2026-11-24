#include "core/update_info.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace bh::core::Updates {

QUrl latestReleaseUrl()
{
    // Для проверки на тестовом сервере.
    const QString custom = qEnvironmentVariable("BIBLIOHELPER_UPDATE_URL");
    if (!custom.isEmpty())
        return QUrl(custom);
    return QUrl(QStringLiteral("https://api.github.com/repos/whohes/BiblioHelper/releases/latest"));
}

namespace {

QString cleanVersion(QString v)
{
    v = v.trimmed();
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        v.remove(0, 1);
    return v;
}

QVector<int> parts(const QString &version)
{
    QVector<int> result;
    // «1.2.0-beta» → 1, 2, 0: хвост после цифр не учитывается.
    static const QRegularExpression number(QStringLiteral("^(\\d+)"));
    for (const QString &part : cleanVersion(version).split(QLatin1Char('.'))) {
        const auto match = number.match(part);
        if (!match.hasMatch())
            break;
        result.push_back(match.captured(1).toInt());
    }
    return result;
}

} // namespace

int compareVersions(const QString &a, const QString &b)
{
    const QVector<int> x = parts(a);
    const QVector<int> y = parts(b);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int l = i < x.size() ? x.at(i) : 0;
        const int r = i < y.size() ? y.at(i) : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

std::optional<ReleaseInfo> parseLatestRelease(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject release = doc.object();
    if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool())
        return std::nullopt;

    ReleaseInfo info;
    info.version = cleanVersion(release.value(QStringLiteral("tag_name")).toString());
    if (parts(info.version).isEmpty())
        return std::nullopt;
    info.notes = release.value(QStringLiteral("body")).toString().trimmed();
    info.pageUrl = QUrl(release.value(QStringLiteral("html_url")).toString());

    for (const QJsonValue &value : release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            || !name.contains(QStringLiteral("setup"), Qt::CaseInsensitive))
            continue;
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!url.isValid() || url.scheme() != QStringLiteral("https"))
            continue;
        info.setupUrl = url;
        info.setupName = name;
        info.setupSize = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble(-1));
        return info;
    }
    return std::nullopt;
}

} // namespace bh::core::Updates
