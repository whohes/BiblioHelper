cmake_minimum_required(VERSION 3.16)

project(BiblioHelper
    VERSION 0.1.0
    DESCRIPTION "Учёт книжного фонда поверх Excel-файлов"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

option(BIBLIOHELPER_BUILD_TESTS "Собирать автотесты" ON)

# Qt 5.15 — последняя версия Qt с поддержкой Windows 7.
# Код пишется так, чтобы собираться и на Qt 6 (QT_VERSION_MAJOR=6).
find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
if(QT_VERSION_MAJOR EQUAL 5 AND QT_VERSION VERSION_LESS 5.15)
    message(FATAL_ERROR "Нужен Qt 5.15 или новее, найден ${QT_VERSION}")
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Concurrent)
message(STATUS "Qt ${QT_VERSION}")

if(MINGW)
    # Программа и тесты не зависят от libgcc/libstdc++ DLL: иначе на
    # компьютере может подхватиться чужая DLL другой версии из PATH.
    add_link_options(-static-libgcc -static-libstdc++)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(BIBLIOHELPER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
