find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Gui)

add_executable(test_xlsx_roundtrip test_xlsx_roundtrip.cpp)
target_compile_definitions(test_xlsx_roundtrip PRIVATE
    BH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# QZipReader — внутренний класс Qt, нужен только тестам для сравнения частей XLSX.
target_link_libraries(test_xlsx_roundtrip PRIVATE
    bh_core
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::GuiPrivate)
if(MINGW)
    target_link_options(test_xlsx_roundtrip PRIVATE -static-libgcc -static-libstdc++)
endif()
add_test(NAME xlsx_roundtrip COMMAND test_xlsx_roundtrip)

add_executable(test_fund_table test_fund_table.cpp)
target_compile_definitions(test_fund_table PRIVATE
    BH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_fund_table PRIVATE bh_core Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME fund_table COMMAND test_fund_table)

add_executable(test_editing test_editing.cpp)
target_compile_definitions(test_editing PRIVATE
    BH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_editing PRIVATE bh_core Qt${QT_VERSION_MAJOR}::Test)
if(MINGW)
    target_link_options(test_editing PRIVATE -static-libgcc -static-libstdc++)
endif()
add_test(NAME editing COMMAND test_editing)

add_executable(test_import test_import.cpp)
target_compile_definitions(test_import PRIVATE
    BH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(test_import PRIVATE bh_core Qt${QT_VERSION_MAJOR}::Test)
if(MINGW)
    target_link_options(test_import PRIVATE -static-libgcc -static-libstdc++)
endif()
add_test(NAME import COMMAND test_import)
