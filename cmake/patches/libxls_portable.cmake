# Правка libxls для сборки без iconv и без UTF-8 локали.
#
# libxls переводит строки XLS (UTF-16LE) в UTF-8 через iconv или через
# локаль «UTF-8» (_create_locale(".65001") в Windows). На Windows 7 такой
# локали нет, iconv в MinGW тоже нет — строки получались бы пустыми.
# Заменяем перевод на свой, без зависимостей (с суррогатными парами),
# а функции локали — на заглушки. Добавляем config.h, который обычно
# создаёт ./configure.
#
# Скрипт запускается из папки исходников libxls и безопасен при повторе.

file(WRITE "src/config.h" "#define PACKAGE_VERSION \"1.6.3\"\n")

file(WRITE "src/locale.c" [=[
/* Заглушки BiblioHelper: строки переводятся в UTF-8 без локали (см. xlstool.c). */
#include "config.h"
#include <stdlib.h>
#include "../include/libxls/locale.h"

xls_locale_t xls_createlocale(void) { return (xls_locale_t)0; }
void xls_freelocale(xls_locale_t locale) { (void)locale; }
size_t xls_wcstombs_l(char *restrict s, const wchar_t *restrict pwcs, size_t n, xls_locale_t loc)
{
    (void)s; (void)pwcs; (void)n; (void)loc;
    return (size_t)-1;
}
]=])

file(READ "src/xlstool.c" _text)
string(FIND "${_text}" "bh_utf16le_to_utf8" _done)
if(_done EQUAL -1)
    set(_converter [=[
/* BiblioHelper: UTF-16LE -> UTF-8 без iconv и локали. */
static char *bh_utf16le_to_utf8(const char *s, size_t len) {
    size_t n = len / 2, i;
    char *out = malloc(n * 3 + 1);
    char *p = out;
    if (!out)
        return NULL;
    for (i = 0; i < n; ++i) {
        unsigned long c = (BYTE)s[2*i] | ((unsigned long)(BYTE)s[2*i+1] << 8);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n) {
            unsigned long c2 = (BYTE)s[2*i+2] | ((unsigned long)(BYTE)s[2*i+3] << 8);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) {
            *p++ = (char)c;
        } else if (c < 0x800) {
            *p++ = (char)(0xC0 | (c >> 6));
            *p++ = (char)(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            *p++ = (char)(0xE0 | (c >> 12));
            *p++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *p++ = (char)(0x80 | (c & 0x3F));
        } else {
            *p++ = (char)(0xF0 | (c >> 18));
            *p++ = (char)(0x80 | ((c >> 12) & 0x3F));
            *p++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *p++ = (char)(0x80 | (c & 0x3F));
        }
    }
    *p = 0;
    return out;
}

static char *unicode_decode_wcstombs(const char *s, size_t len, xls_locale_t locale) {
    (void)locale;
    return bh_utf16le_to_utf8(s, len);
}

static char *unicode_decode_wcstombs_unused(const char *s, size_t len, xls_locale_t locale) {]=])
    string(REPLACE "static char *unicode_decode_wcstombs(const char *s, size_t len, xls_locale_t locale) {"
                   "${_converter}" _text "${_text}")
    string(REPLACE "        if (locale == NULL) {\n            printf(\"creation of UTF-8 locale failed\\n\");\n            return NULL;\n        }\n"
                   "" _text "${_text}")
    # Неиспользуемая старая функция не должна давать предупреждение.
    string(REPLACE "static char *unicode_decode_wcstombs_unused(" "char *bh_unicode_decode_wcstombs_unused(" _text "${_text}")
    file(WRITE "src/xlstool.c" "${_text}")
endif()
