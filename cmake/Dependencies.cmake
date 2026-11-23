# Сторонние библиотеки скачиваются при первой сборке и закрепляются
# на конкретных версиях, чтобы сборка была воспроизводимой.
include(FetchContent)

# OpenXLSX: чтение и сохранение рабочих копий XLSX.
# Правит XML листа на месте, остальные части файла не пересобирает.
# Релизные теги у проекта давно не обновлялись, поэтому закреплён коммит.
FetchContent_Declare(OpenXLSX
    GIT_REPOSITORY https://github.com/troldal/OpenXLSX.git
    GIT_TAG        c1220f3e0dc570f96fae456e69ac2eb13aa65f4d
    # Совместимость с GCC 8 (MinGW из Qt 5.15), см. сам скрипт.
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/patches/openxlsx_gcc8.cmake)
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

# libxls: чтение старых файлов Excel 97–2003 (.xls), только значения.
# Своего CMake у библиотеки нет, поэтому цель собирается здесь.
# Перевод строк в UTF-8 заменён на свой (см. скрипт правки).
FetchContent_Declare(libxls
    GIT_REPOSITORY https://github.com/libxls/libxls.git
    GIT_TAG        v1.6.3
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/patches/libxls_portable.cmake)
FetchContent_GetProperties(libxls)
if(NOT libxls_POPULATED)
    FetchContent_Populate(libxls)
endif()
add_library(xls STATIC
    ${libxls_SOURCE_DIR}/src/xls.c
    ${libxls_SOURCE_DIR}/src/xlstool.c
    ${libxls_SOURCE_DIR}/src/ole.c
    ${libxls_SOURCE_DIR}/src/endian.c
    ${libxls_SOURCE_DIR}/src/locale.c)
set_target_properties(xls PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(xls PUBLIC ${libxls_SOURCE_DIR}/include PRIVATE ${libxls_SOURCE_DIR}/src)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xls PRIVATE -w)  # чужой код: предупреждения не наши
endif()
