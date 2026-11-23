#pragma once

#include <QString>

namespace bh::core {

// Настройки программы (окно «Настройки»). Хранятся в QSettings
// (на Windows — в реестре, HKCU\Software\BiblioHelper).
namespace AppSettings {

// Папка программы: рабочие файлы, резервные копии, отчёты, экспорт, журнал.
// По умолчанию «Документы\BiblioHelper».
QString dataRoot();
QString defaultDataRoot();
void setDataRoot(const QString &path);  // пусто — по умолчанию

// Масштаб интерфейса в процентах: 100, 125 или 150. Действует после перезапуска.
int interfaceScale();
void setInterfaceScale(int percent);

// Сколько автоматических резервных копий («перед сохранением») хранить.
int keepSaveBackups();
void setKeepSaveBackups(int count);

// Предлагать объединить одинаковые книги сразу после импорта.
bool offerBookGroupsAfterImport();
void setOfferBookGroupsAfterImport(bool offer);

// Адрес для «Сообщить о проблеме».
QString supportEmail();

} // namespace AppSettings

} // namespace bh::core
