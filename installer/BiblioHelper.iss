; Установщик BiblioHelper (Inno Setup 6).
; Сборка: iscc /DAppVersion=1.0.0 /DSourceDir=..\dist installer\BiblioHelper.iss
;
; Устанавливает для текущего пользователя, без прав администратора, в
; %LOCALAPPDATA%\Programs\BiblioHelper. Вопросов не задаёт: «Далее» → «Завершить».
; Установка поверх старой версии заменяет только программу: рабочие файлы,
; резервные копии и журнал (Документы\BiblioHelper) и настройки
; (HKCU\Software\BiblioHelper) не трогаются ни при обновлении, ни при удалении.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\installer-out"
#endif

[Setup]
; Не менять AppId: по нему новая версия ставится поверх старой.
AppId={{6E1B7C2A-4F3D-4B8E-9C61-2A9D5E0B7F41}
AppName=BiblioHelper
AppVersion={#AppVersion}
AppVerName=BiblioHelper {#AppVersion}
AppPublisher=BiblioHelper
AppSupportURL=mailto:bibliohelpers@gmail.com
AppUpdatesURL=https://github.com/whohes/BiblioHelper/releases
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Установка BiblioHelper
; Windows 7 SP1 и новее, 32 и 64 бита (программа 32-битная).
MinVersion=6.1sp1
ArchitecturesAllowed=x86compatible
PrivilegesRequired=lowest
DefaultDirName={autopf}\BiblioHelper
DefaultGroupName=BiblioHelper
UsePreviousAppDir=yes
DisableWelcomePage=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=no
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
SetupIconFile=..\src\resources\app.ico
UninstallDisplayIcon={app}\BiblioHelper.exe
UninstallDisplayName=BiblioHelper
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=BiblioHelper-{#AppVersion}-setup

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[Messages]
ru.WelcomeLabel2=Будет установлена программа [name/ver] для учёта книжного фонда.%n%nПрава администратора не нужны. Если программа уже установлена, она обновится, а ваши файлы и настройки сохранятся.%n%nНажмите «Далее».

[Files]
; Старые файлы программы убираем, чтобы не осталось лишних DLL от прошлых версий.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: filesandordirs; Name: "{app}\*"

[Icons]
Name: "{autoprograms}\BiblioHelper"; Filename: "{app}\BiblioHelper.exe"; Comment: "Учёт книжного фонда"
Name: "{autodesktop}\BiblioHelper"; Filename: "{app}\BiblioHelper.exe"; Comment: "Учёт книжного фонда"

[Registry]
; «Открыть с помощью → BiblioHelper» для .xlsx, .xls и .csv (только для этого пользователя).
; Программа по умолчанию для Excel-файлов не меняется.
Root: HKCU; Subkey: "Software\Classes\BiblioHelper.Fund"; ValueType: string; ValueData: "Книжный фонд"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\BiblioHelper.Fund\DefaultIcon"; ValueType: string; ValueData: """{app}\BiblioHelper.exe"",0"
Root: HKCU; Subkey: "Software\Classes\BiblioHelper.Fund\shell\open\command"; ValueType: string; ValueData: """{app}\BiblioHelper.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\.xlsx\OpenWithProgids"; ValueType: string; ValueName: "BiblioHelper.Fund"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.xls\OpenWithProgids"; ValueType: string; ValueName: "BiblioHelper.Fund"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.csv\OpenWithProgids"; ValueType: string; ValueName: "BiblioHelper.Fund"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\Applications\BiblioHelper.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "BiblioHelper"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\BiblioHelper.exe\shell\open\command"; ValueType: string; ValueData: """{app}\BiblioHelper.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\BiblioHelper.exe\SupportedTypes"; ValueType: string; ValueName: ".xlsx"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\BiblioHelper.exe\SupportedTypes"; ValueType: string; ValueName: ".xls"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\BiblioHelper.exe\SupportedTypes"; ValueType: string; ValueName: ".csv"; ValueData: ""

[Run]
; Обычная установка: галочка «Запустить» на последней странице.
; Обновление из программы (/SILENT): программа открывается сама.
Filename: "{app}\BiblioHelper.exe"; Description: "Запустить BiblioHelper"; Flags: nowait postinstall

[Code]
// При обновлении из программы установщик стартует, пока она ещё закрывается:
// ждём до 15 секунд, остальное закроет CloseApplications.
function InitializeSetup(): Boolean;
var
  I: Integer;
begin
  I := 0;
  while CheckForMutexes('BiblioHelperRunning') and (I < 50) do
  begin
    Sleep(300);
    I := I + 1;
  end;
  Result := True;
end;
