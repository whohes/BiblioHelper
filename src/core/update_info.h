#pragma once

#include <QString>
#include <QUrl>

#include <optional>

namespace bh::core {

// Сведения о последней версии программы на GitHub Releases
// (https://github.com/whohes/BiblioHelper/releases).
struct ReleaseInfo
{
    QString version;   // «1.2.0» (без «v»)
    QString notes;     // «Что нового» — текст релиза (Markdown)
    QUrl setupUrl;     // ссылка на установщик …setup.exe
    QString setupName; // имя файла установщика
    qint64 setupSize = -1;
    QUrl pageUrl;      // страница релиза
};

namespace Updates {

// Адрес запроса последней версии (без входа в GitHub, только публичные релизы).
QUrl latestReleaseUrl();

// Разбирает ответ GitHub API /releases/latest. nullopt — нет установщика
// или ответ непонятный (черновики и предварительные версии GitHub сюда не отдаёт).
std::optional<ReleaseInfo> parseLatestRelease(const QByteArray &json);

// Сравнение версий «1.10.2» и «v1.9»: <0, 0, >0. Недостающие части = 0.
int compareVersions(const QString &a, const QString &b);

} // namespace Updates

} // namespace bh::core
