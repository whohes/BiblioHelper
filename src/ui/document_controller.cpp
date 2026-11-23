#include "document_controller.h"

#include "main_window.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>

namespace bh::ui {

namespace {

const QString kLastDirKey = QStringLiteral("files/lastDirectory");

bool samePath(const QString &a, const QString &b)
{
    const QString ca = QFileInfo(a).canonicalFilePath();
    const QString cb = QFileInfo(b).canonicalFilePath();
    if (ca.isEmpty() || cb.isEmpty())
        return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
    return ca.compare(cb, Qt::CaseInsensitive) == 0;
}

} // namespace

DocumentController::DocumentController(MainWindow *window)
    : QObject(window)
    , m_window(window)
{
    connect(window, &MainWindow::openRequested, this, &DocumentController::open);
    connect(window, &MainWindow::saveRequested, this, &DocumentController::save);
    connect(window, &MainWindow::saveAsRequested, this, &DocumentController::saveAs);
}

void DocumentController::open()
{
    if (!confirmDiscardChanges())
        return;

    QSettings settings;
    const QString startDir = settings.value(kLastDirKey,
                                            QDir::homePath()).toString();
    const QString path = QFileDialog::getOpenFileName(
        m_window, QStringLiteral("Выберите Excel-файл с книжным фондом"), startDir,
        QStringLiteral("Файлы Excel (*.xlsx *.xls);;Все файлы (*)"));
    if (path.isEmpty())
        return;
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    openFile(path);
}

bool DocumentController::openFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xls")) {
        showError(QStringLiteral("Старый формат Excel"),
                  QStringLiteral("Файлы XLS (Excel 97–2003) программа научится открывать позже.\n\n"
                                 "Пока откройте файл в Excel и сохраните его как «Книга Excel (*.xlsx)»."));
        return false;
    }

    QString error;
    const QString workPath = m_workCopies.create(path, &error);
    if (workPath.isEmpty()) {
        showError(QStringLiteral("Не удалось открыть файл"), error);
        return false;
    }
    if (!m_workbook.open(workPath, &error)) {
        QFile::remove(workPath);
        showError(QStringLiteral("Не удалось открыть файл"),
                  QStringLiteral("Файл «%1» не удалось прочитать как Excel-файл.\n\n%2")
                      .arg(QFileInfo(path).fileName(), error));
        return false;
    }

    m_originalPath = path;
    m_workPath = workPath;
    m_window->setWorkFileName(QFileInfo(workPath).fileName());
    m_window->setUnsavedChanges(false);

    const QStringList sheets = m_workbook.sheetNames();
    m_window->setBooksMessage(
        QStringLiteral("Файл открыт: «%1».\nЛистов в файле: %2 (%3).\n\n"
                       "Исходный файл не изменяется: программа работает с рабочей копией\n%4\n\n"
                       "Таблица книг появится в следующей версии программы.")
            .arg(QFileInfo(path).fileName())
            .arg(sheets.size())
            .arg(sheets.join(QStringLiteral(", ")),
                 QDir::toNativeSeparators(workPath)));
    m_window->statusBar()->showMessage(
        QStringLiteral("Открыт файл «%1». Рабочая копия создана.").arg(QFileInfo(path).fileName()));
    return true;
}

void DocumentController::save()
{
    if (!m_workbook.isOpen())
        return;
    QString error;
    if (!m_workbook.saveAs(m_workPath, &error)) {
        showError(QStringLiteral("Не удалось сохранить"), error);
        return;
    }
    m_window->setUnsavedChanges(false);
    m_window->statusBar()->showMessage(QStringLiteral("Изменения сохранены в рабочую копию"), 5000);
}

void DocumentController::saveAs()
{
    if (!m_workbook.isOpen())
        return;

    QSettings settings;
    const QString startDir = settings.value(kLastDirKey, QDir::homePath()).toString();
    const QString suggested = QDir(startDir).filePath(QFileInfo(m_workPath).fileName());
    QString path = QFileDialog::getSaveFileName(m_window, QStringLiteral("Сохранить как"), suggested,
                                                QStringLiteral("Файлы Excel (*.xlsx)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) != 0)
        path += QStringLiteral(".xlsx");

    if (samePath(path, m_originalPath)) {
        showError(QStringLiteral("Это исходный файл"),
                  QStringLiteral("Исходный файл программа не перезаписывает, чтобы его можно было "
                                 "открыть заново.\n\nВыберите другое имя или другую папку."));
        return;
    }

    QString error;
    if (!m_workbook.saveAs(path, &error)) {
        showError(QStringLiteral("Не удалось сохранить"), error);
        return;
    }
    settings.setValue(kLastDirKey, QFileInfo(path).absolutePath());
    // Дальше программа работает с новым файлом.
    m_workPath = path;
    m_window->setWorkFileName(QFileInfo(path).fileName());
    m_window->setUnsavedChanges(false);
    m_window->statusBar()->showMessage(
        QStringLiteral("Файл сохранён: %1").arg(QDir::toNativeSeparators(path)), 8000);
}

bool DocumentController::confirmDiscardChanges()
{
    if (!m_window->hasUnsavedChanges())
        return true;

    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Несохранённые изменения"));
    box.setText(QStringLiteral("В рабочем файле есть несохранённые изменения. Что сделать?"));
    auto *saveButton = box.addButton(QStringLiteral("Сохранить"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(QStringLiteral("Не сохранять"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.exec();

    if (box.clickedButton() == saveButton) {
        save();
        return !m_window->hasUnsavedChanges();
    }
    return box.clickedButton() == discard;
}

void DocumentController::showError(const QString &title, const QString &text)
{
    QMessageBox::warning(m_window, title, text);
}

} // namespace bh::ui
