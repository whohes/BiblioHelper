# Библиотека с логикой и работой с файлами: её же подключают автотесты.
add_library(bh_core STATIC
    io/xlsx_workbook.h
    io/xlsx_workbook.cpp
)
target_include_directories(bh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bh_core
    PUBLIC  Qt${QT_VERSION_MAJOR}::Core
    PRIVATE OpenXLSX::OpenXLSX)

add_executable(BiblioHelper WIN32
    main.cpp
    ui/main_window.h
    ui/main_window.cpp
    resources/resources.qrc
)
target_compile_definitions(BiblioHelper PRIVATE BIBLIOHELPER_VERSION="${PROJECT_VERSION}")
target_link_libraries(BiblioHelper PRIVATE
    bh_core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    QXlsx::QXlsx)

if(MINGW)
    # Без зависимости от libgcc/libstdc++ DLL рядом с программой.
    target_link_options(BiblioHelper PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS BiblioHelper RUNTIME DESTINATION .)
