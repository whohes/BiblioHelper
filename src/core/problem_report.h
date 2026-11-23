#pragma once

#include "action_log.h"

#include <QString>
#include <QUrl>
#include <QVector>

namespace bh::core {

// Письмо «Сообщить о проблеме»: описание от пользователя и сведения
// для разбора (версия программы и Windows, открытый фонд, последние записи журнала).
struct ProblemReport
{
    QString description;
    QString appVersion;
    QString system;      // «Windows 7 Service Pack 1 (6.1)»
    QString fund;        // имя открытого файла фонда
    QVector<LogEntry> log;  // новые первыми
    bool includeDetails = true;

    QString subject() const;
    QString body() const;
    // mailto: с темой и текстом. Почтовые программы Windows не принимают
    // слишком длинные ссылки, поэтому журнал укорачивается до maxBodyChars.
    QUrl mailto(const QString &email, int maxBodyChars = 1800) const;
};

} // namespace bh::core
