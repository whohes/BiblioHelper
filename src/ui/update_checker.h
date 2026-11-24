#pragma once

#include "core/update_info.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QTimer;

namespace bh::ui {

class MainWindow;

// Обновления программы. Раз в день тихо спрашивает GitHub Releases, есть ли
// версия новее; без интернета ничего не сообщает. Если есть — показывает
// жёлтую полосу; «Обновить» скачивает setup.exe, закрывает программу
// (с вопросом о несохранённых изменениях) и запускает установку поверх.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(MainWindow *window);

    // Проверить, если сегодня ещё не проверяли и проверка включена.
    void checkIfDue();

private:
    QNetworkRequest request(const QUrl &url) const;
    void check();
    void onCheckFinished(QNetworkReply *reply);
    void showNotes();
    void download();
    void install(const QString &setupPath);

    MainWindow *m_window;
    QNetworkAccessManager *m_network;
    QTimer *m_dailyTimer;
    QPointer<QNetworkReply> m_checkReply;
    std::optional<core::ReleaseInfo> m_release;
};

} // namespace bh::ui
