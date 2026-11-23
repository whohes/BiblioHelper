# Сторонние библиотеки скачиваются при первой сборке и закрепляются
# на конкретных версиях, чтобы сборка была воспроизводимой.
include(FetchContent)

# OpenXLSX: чтение и сохранение рабочих копий XLSX.
# Правит XML листа на месте, остальные части файла не пересобирает.
# Релизные теги у проекта давно не обновлялись, поэтому закреплён коммит.
FetchContent_Declare(OpenXLSX
    GIT_REPOSITORY https://github.com/troldal/OpenXLSX.git
    GIT_TAG        c1220f3e0dc570f96fae456e69ac2eb13aa65f4d)
set(OPENXLSX_CREATE_DOCS OFF CACHE BOOL "" FORCE)
set(OPENXLSX_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(OPENXLSX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENXLSX_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# QXlsx: создание новых отчётов и экспорт (этап 5 и 7).
FetchContent_Declare(QXlsx
    GIT_REPOSITORY https://github.com/QtExcel/QXlsx.git
    GIT_TAG        v1.5.0
    SOURCE_SUBDIR  QXlsx)
set(QT_VERSION_MAJOR ${QT_VERSION_MAJOR} CACHE STRING "" FORCE)

FetchContent_MakeAvailable(OpenXLSX QXlsx)
