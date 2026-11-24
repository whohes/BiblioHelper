#include "ui/update_checker.h"

#include "core/action_log.h"
#include "core/app_settings.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStandardPaths>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace bh::ui {

namespace {

// Корневые сертификаты GitHub на случай, если в Windows 7 они устарели
// (компьютеры давно без обновлений). Добавляются к системным.
QList<QSslCertificate> bundledRoots()
{
    static const QList<QSslCertificate> roots = [] {
        QFile file(QStringLiteral(":/certs/roots.pem"));
        return file.open(QIODevice::ReadOnly) ? QSslCertificate::fromData(file.readAll(), QSsl::Pem)
                                              : QList<QSslCertificate>();
    }();
    return roots;
}

QString safeFileName(const QString &name)
{
    QString result = name;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (!result.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        result += QStringLiteral(".exe");
    return result;
}

} // namespace

UpdateChecker::UpdateChecker(MainWindow *window)
    : QObject(window)
    , m_window(window)
    , m_network(new QNetworkAccessManager(this))
    , m_dailyTimer(new QTimer(this))
{
    connect(window, &MainWindow::updateNotesRequested, this, &UpdateChecker::showNotes);
    connect(window, &MainWindow::updateRequested, this, &UpdateChecker::download);

    // Программу могут не закрывать неделями: раз в час смотрим, не наступил ли новый день.
    m_dailyTimer->setInterval(60 * 60 * 1000);
    connect(m_dailyTimer, &QTimer::timeout, this, &UpdateChecker::checkIfDue);
    m_dailyTimer->start();
    // Первая проверка — чуть позже запуска, чтобы не мешать открытию файла.
    QTimer::singleShot(20 * 1000, this, &UpdateChecker::checkIfDue);
}

void UpdateChecker::checkIfDue()
{
    if (!core::AppSettings::checkUpdates() || m_checkReply)
        return;
    if (core::AppSettings::lastUpdateCheck() == QDate::currentDate())
        return;
    check();
}

QNetworkRequest UpdateChecker::request(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent",
                         QByteArray("BiblioHelper/") + QApplication::applicationVersion().toLatin1());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.addCaCertificates(bundledRoots());
    request.setSslConfiguration(ssl);
    return request;
}

void UpdateChecker::check()
{
    // Без OpenSSL (не та сборка) HTTPS невозможен — молчим.
    if (!QSslSocket::supportsSsl())
        return;
    QNetworkRequest req = request(core::Updates::latestReleaseUrl());
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(30 * 1000);
    m_checkReply = m_network->get(req);
    QNetworkReply *reply = m_checkReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onCheckFinished(reply); });
}

void UpdateChecker::onCheckFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    // Нет интернета, GitHub недоступен, релизов ещё нет (404) — тихо, попробуем завтра или через час.
    if (reply->error() != QNetworkReply::NoError)
        return;
    core::AppSettings::setLastUpdateCheck(QDate::currentDate());
    const auto release = core::Updates::parseLatestRelease(reply->readAll());
    if (!release || core::Updates::compareVersions(release->version, QApplication::applicationVersion()) <= 0)
        return;
    m_release = release;
    m_window->setUpdateBanner(release->version);
}

void UpdateChecker::showNotes()
{
    if (!m_release)
        return;
    QDialog dialog(m_window);
    dialog.setWindowTitle(QStringLiteral("Что нового в версии %1").arg(m_release->version));
    dialog.resize(720, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QTextBrowser(&dialog);
    text->setOpenExternalLinks(true);
    if (m_release->notes.isEmpty())
        text->setPlainText(QStringLiteral("Описание изменений не приложено."));
    else
        text->setMarkdown(m_release->notes);
    layout->addWidget(text, 1);
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *update = buttons->addButton(QStringLiteral("Обновить"), QDialogButtonBox::AcceptRole);
    update->setProperty("primary", true);
    update->setDefault(true);
    update->style()->unpolish(update);
    update->style()->polish(update);
    buttons->addButton(QStringLiteral("Закрыть"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        download();
}

void UpdateChecker::download()
{
    if (!m_release)
        return;
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("BiblioHelper-update"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(safeFileName(m_release->setupName));
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(m_window, QStringLiteral("Обновление"),
                             QStringLiteral("Не удалось сохранить установщик:\n%1").arg(file->errorString()));
        return;
    }

    auto *progress = new QProgressDialog(QStringLiteral("Загрузка новой версии %1…").arg(m_release->version),
                                         QStringLiteral("Отмена"), 0, 100, m_window);
    progress->setWindowTitle(QStringLiteral("Обновление"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    QNetworkReply *reply = m_network->get(request(m_release->setupUrl));
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 done, qint64 total) {
        if (total > 0)
            progress->setValue(int(done * 100 / total));
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, progress, path] {
        reply->deleteLater();
        progress->deleteLater();
        file->write(reply->readAll());
        file->close();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            QFile::remove(path);
            return;
        }
        bool ok = reply->error() == QNetworkReply::NoError
                  && (m_release->setupSize < 0 || file->size() == m_release->setupSize);
        if (ok) {
            QFile check(path);
            ok = check.open(QIODevice::ReadOnly) && check.read(2) == "MZ";
        }
        if (!ok) {
            QFile::remove(path);
            QMessageBox::warning(m_window, QStringLiteral("Обновление"),
                                 QStringLiteral("Не удалось скачать новую версию. Проверьте интернет и "
                                                "попробуйте ещё раз позже.\n\n%1")
                                     .arg(reply->errorString()));
            return;
        }
        install(path);
    });
}

void UpdateChecker::install(const QString &setupPath)
{
    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Обновление"));
    box.setText(QStringLiteral("Новая версия скачана. Программа закроется, установится новая версия "
                               "и программа откроется снова. Рабочие файлы, резервные копии и "
                               "настройки сохранятся."));
    auto *now = box.addButton(QStringLiteral("Установить сейчас"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Позже"), QMessageBox::RejectRole);
    box.setDefaultButton(now);
    box.exec();
    if (box.clickedButton() != now)
        return;

    // Закрытие окна спросит о несохранённых изменениях; «Отмена» — остаёмся.
    if (!m_window->close())
        return;
    core::ActionLog().append(QStringLiteral("update"), {},
                             QStringLiteral("Установка версии %1").arg(m_release->version));
    // /SILENT — без вопросов, только окно с полосой хода установки.
    const QStringList args{QStringLiteral("/SILENT"), QStringLiteral("/SP-"), QStringLiteral("/SUPPRESSMSGBOXES"),
                           QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS")};
    if (!QProcess::startDetached(setupPath, args)) {
        QMessageBox::warning(nullptr, QStringLiteral("Обновление"),
                             QStringLiteral("Не удалось запустить установщик. Запустите его вручную:\n%1")
                                 .arg(QDir::toNativeSeparators(setupPath)));
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(setupPath).absolutePath()));
    }
    QApplication::quit();
}

} // namespace bh::ui
