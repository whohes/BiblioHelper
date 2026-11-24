#include "problem_report.h"

#include <algorithm>

namespace bh::core {

QString ProblemReport::subject() const
{
    return QStringLiteral("BiblioHelper %1: сообщение о проблеме").arg(appVersion);
}

namespace {

QString logLine(const LogEntry &e)
{
    QString line = QStringLiteral("%1  %2").arg(e.time.toString(QStringLiteral("dd.MM.yyyy HH:mm")), e.actionTitle());
    if (e.count >= 0)
        line += QStringLiteral(" (%1 экз.)").arg(e.count);
    if (!e.details.isEmpty())
        line += QStringLiteral(": ") + (e.details.size() > 160 ? e.details.left(157) + QStringLiteral("…") : e.details);
    return line;
}

QString buildBody(const ProblemReport &r, int logLines)
{
    QString text = r.description.trimmed();
    if (text.isEmpty())
        text = QStringLiteral("(опишите, что случилось)");
    if (!r.includeDetails)
        return text;
    text += QStringLiteral("\n\n— Сведения для разбора —\n");
    text += QStringLiteral("Программа: BiblioHelper %1\n").arg(r.appVersion);
    if (!r.system.isEmpty())
        text += QStringLiteral("Система: %1\n").arg(r.system);
    if (!r.fund.isEmpty())
        text += QStringLiteral("Фонд: %1\n").arg(r.fund);
    const int n = std::min(logLines, int(r.log.size()));
    if (n > 0) {
        text += QStringLiteral("\nПоследние действия:\n");
        for (int i = 0; i < n; ++i)
            text += logLine(r.log.at(i)) + QLatin1Char('\n');
    }
    return text;
}

} // namespace

QString ProblemReport::body() const
{
    return buildBody(*this, 20);
}

QUrl ProblemReport::mailto(const QString &email, int maxBodyChars) const
{
    int lines = 20;
    QString text = buildBody(*this, lines);
    while (text.size() > maxBodyChars && lines > 0)
        text = buildBody(*this, --lines);
    if (text.size() > maxBodyChars)
        text = text.left(maxBodyChars - 1) + QStringLiteral("…");
    auto encode = [](const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); };
    QUrl url;
    url.setScheme(QStringLiteral("mailto"));
    url.setPath(email);
    url.setQuery(QStringLiteral("subject=%1&body=%2").arg(encode(subject()), encode(text)), QUrl::StrictMode);
    return url;
}

} // namespace bh::core
