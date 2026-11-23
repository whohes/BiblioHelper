# Библиотека с логикой и работой с файлами: её же подключают автотесты.
add_library(bh_core STATIC
    core/copy_ids.h
    core/copy_ids.cpp
    core/fund_sheet.h
    core/fund_sheet.cpp
    core/withdrawal_report.h
    core/withdrawal_report.cpp
    core/work_copy.h
    core/work_copy.cpp
    io/xlsx_font_colors.h
    io/xlsx_font_colors.cpp
    io/sheet_xml_rows.h
    io/sheet_xml_rows.cpp
    io/xlsx_workbook.h
    io/xlsx_workbook.cpp
    models/fund_table_model.h
    models/fund_table_model.cpp
    models/fund_filter_proxy.h
    models/fund_filter_proxy.cpp
)
target_include_directories(bh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bh_core
    PUBLIC  Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui
    PRIVATE OpenXLSX::OpenXLSX QXlsx::QXlsx)
# pugixml (внутри OpenXLSX) — для правки XML листа при добавлении строк.
if(TARGET pugixml-static)
    target_link_libraries(bh_core PRIVATE pugixml-static)
elseif(TARGET pugixml::pugixml)
    target_link_libraries(bh_core PRIVATE pugixml::pugixml)
endif()

add_executable(BiblioHelper WIN32
    main.cpp
    ui/document_controller.h
    ui/document_controller.cpp
    ui/filter_header_view.h
    ui/filter_header_view.cpp
    ui/filter_popup.h
    ui/fund_table_page.h
    ui/fund_table_page.cpp
    ui/filter_popup.cpp
    ui/record_dialog.h
    ui/record_dialog.cpp
    ui/withdraw_dialog.h
    ui/withdraw_dialog.cpp
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
