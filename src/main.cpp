#include "core/app_settings.h"
#include "ui/document_controller.h"
#include "ui/main_window.h"
#include "ui/update_checker.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QSslSocket>
#include <QStyleFactory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
    // Проверка сборки в CI: есть ли OpenSSL для HTTPS (код выхода 0 — есть).
    if (argc > 1 && qstrcmp(argv[1], "--check-https") == 0) {
        QCoreApplication core(argc, argv);
        return QSslSocket::supportsSsl() ? 0 : 3;
    }

#ifdef Q_OS_WIN
    // По этому имени установщик ждёт, пока программа закроется перед обновлением.
    CreateMutexW(nullptr, FALSE, L"BiblioHelperRunning");
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Чёткое отображение при масштабе Windows 125–150 %.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    // Имена нужны до QApplication: по ним читаются настройки (масштаб интерфейса).
    QApplication::setApplicationName(QStringLiteral("BiblioHelper"));
    QApplication::setOrganizationName(QStringLiteral("BiblioHelper"));
    const int scale = bh::core::AppSettings::interfaceScale();
    if (scale != 100 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));

    QApplication app(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Учёт книжного фонда"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    QApplication::setApplicationVersion(QStringLiteral(BIBLIOHELPER_VERSION));
    QApplication::setOrganizationName(QStringLiteral("BiblioHelper"));

    // Fusion — одинаковый современный вид на Windows 7–11 (стиль Windows 7 выглядит устаревшим).
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Крупный шрифт по умолчанию (ТЗ: 12–14 pt), Segoe UI есть во всех Windows начиная с Vista.
    QFont font = QApplication::font();
    font.setFamily(QStringLiteral("Segoe UI"));
    font.setPointSize(13);
    QApplication::setFont(font);

    QFile style(QStringLiteral(":/style.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    bh::ui::MainWindow window;
    auto *documents = new bh::ui::DocumentController(&window);
    new bh::ui::UpdateChecker(&window);
    window.showMaximized();

    // Файл можно открыть, перетащив его на значок программы.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        documents->openFile(args.at(1));
    return app.exec();
}
