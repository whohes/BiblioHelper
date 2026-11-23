# Правка OpenXLSX для GCC 8 (MinGW 8.1 из Qt 5.15 — сборка под Windows 7).
#
# OpenXLSX объявляет перемещающие конструкторы и операторы как noexcept,
# а определяет их в .cpp через «= default». GCC до версии 9 считает это
# ошибкой, если неявная спецификация не noexcept (старое правило C++,
# исправленное в P1286R2). Убираем noexcept у таких пар
# «объявление в .hpp + = default в .cpp». Поведение библиотеки не меняется.
#
# Скрипт запускается из папки исходников OpenXLSX и безопасен при повторе.

file(GLOB_RECURSE _headers "OpenXLSX/headers/*.hpp")
file(GLOB_RECURSE _sources "OpenXLSX/sources/*.cpp")

set(_classes "")
foreach(_file IN LISTS _sources)
    file(READ "${_file}" _text)
    string(REGEX MATCHALL "\\(([A-Za-z_:]+)&& *[A-Za-z_]*\\) *noexcept *= *default" _hits "${_text}")
    foreach(_hit IN LISTS _hits)
        string(REGEX REPLACE "^\\(([A-Za-z_:]+)&&.*" "\\1" _cls "${_hit}")
        string(REGEX REPLACE "^.*::" "" _cls "${_cls}")
        list(APPEND _classes "${_cls}")
    endforeach()
    string(REGEX REPLACE "(&& *[A-Za-z_]*\\)) *noexcept *(= *default)" "\\1 \\2" _patched "${_text}")
    if(NOT _patched STREQUAL _text)
        file(WRITE "${_file}" "${_patched}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _classes)

foreach(_file IN LISTS _headers)
    file(READ "${_file}" _text)
    set(_patched "${_text}")
    foreach(_cls IN LISTS _classes)
        string(REGEX REPLACE "\\(((OpenXLSX::)?${_cls}&& *[A-Za-z_]*\\)) *noexcept *;" "(\\1;" _patched "${_patched}")
    endforeach()
    if(NOT _patched STREQUAL _text)
        file(WRITE "${_file}" "${_patched}")
    endif()
endforeach()

list(LENGTH _classes _count)
message(STATUS "OpenXLSX: убран noexcept у перемещения в ${_count} классах (совместимость с GCC 8)")
