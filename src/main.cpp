#include "ui/document_controller.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QFile>
#include <QFont>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Чёткое отображение при масштабе Windows 125–150 %.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BiblioHelper"));
    QApplication::setApplicationDisplayName(QStringLiteral("Учёт книжного фонда"));
    QApplication::setApplicationVersion(QStringLiteral(BIBLIOHELPER_VERSION));
    QApplication::setOrganizationName(QStringLiteral("BiblioHelper"));

    // Крупный шрифт по умолчанию (ТЗ: 12–14 pt).
    QFont font = QApplication::font();
    font.setPointSize(13);
    QApplication::setFont(font);

    QFile style(QStringLiteral(":/style.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    bh::ui::MainWindow window;
    auto *documents = new bh::ui::DocumentController(&window);
    window.showMaximized();

    // Файл можно открыть, перетащив его на значок программы.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        documents->openFile(args.at(1));
    return app.exec();
}
