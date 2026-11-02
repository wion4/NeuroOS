#include "../ports/musl/include/shgetc.h"
#include "libc/stdlib.h"
#define hidden
#include "../vendor/musl-1.2.5/floatscan.h"
static long double convert(const char *text, char **end, int precision) {
    FILE stream;
    sh_fromstring(&stream, text);
    shlim(&stream, 0);
    long double value = __floatscan(&stream, precision, 1);
    ptrdiff_t count = shcnt(&stream);
    if (end)
        *end = count ? (char *)text + count : (char *)text;
    return value;
}
float strtof(const char *text, char **end) { return (float)convert(text, end, 0); }
double strtod(const char *text, char **end) { return (double)convert(text, end, 1); }
long double strtold(const char *text, char **end) { return convert(text, end, 2); }
