# Библиотека с логикой и работой с файлами: её же подключают автотесты.
add_library(bh_core STATIC
    core/fund_sheet.h
    core/fund_sheet.cpp
    core/work_copy.h
    core/work_copy.cpp
    io/xlsx_workbook.h
    io/xlsx_workbook.cpp
    models/fund_table_model.h
    models/fund_table_model.cpp
    models/fund_filter_proxy.h
    models/fund_filter_proxy.cpp
)
target_include_directories(bh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bh_core
    PUBLIC  Qt${QT_VERSION_MAJOR}::Core
    PRIVATE OpenXLSX::OpenXLSX)

add_executable(BiblioHelper WIN32
    main.cpp
    ui/document_controller.h
    ui/document_controller.cpp
    ui/filter_header_view.h
    ui/filter_header_view.cpp
    ui/filter_popup.h
    ui/filter_popup.cpp
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

install(TARGETS BiblioHelper RUNTIME DESTINATION .)
