#include "abi.h"
#include "heap.h"
#include "libc/errno.h"
#include "libc/setjmp.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include <limits.h>
extern int test_jump_registers(void);
static int compare_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
static volatile unsigned visits;
static _Noreturn __attribute__((noinline, disable_tail_calls)) void nested(jmp_buf environment,
                                                                           unsigned depth) {
    volatile uint64_t scratch[12];
    scratch[0] = depth;
    scratch[11] = depth + 1;
    ++visits;
    yield();
    if (depth)
        nested(environment, depth - 1);
    (void)scratch[0];
    (void)scratch[11];
    longjmp(environment, 0);
}
int test_c_library(void) {
    char *end;
    errno = 0;
    if (strtoll("-9223372036854775808!", &end, 10) != LLONG_MIN || *end != '!' || errno)
        return 76;
    if (strtoull("18446744073709551616end", &end, 10) != ULLONG_MAX || errno != ERANGE ||
        strcmp(end, "end"))
        return 77;
    if (strtol("  -0x2a?", &end, 0) != -42 || *end != '?' || strtoul("-1", 0, 10) != ULONG_MAX)
        return 78;
    const char *empty = "  +nothing";
    if (strtol(empty, &end, 10) || end != empty || strtol("0x", &end, 16) || *end != 'x')
        return 79;
    int numbers[] = {INT_MAX, 0, -8, 0, INT_MIN, 17};
    qsort(numbers, 6, sizeof(int), compare_int);
    for (unsigned i = 1; i < 6; ++i)
        if (numbers[i] < numbers[i - 1])
            return 80;
    if (test_jump_registers())
        return 60;
    jmp_buf environment;
    switch (setjmp(environment)) {
    case 0:
        nested(environment, 3);
    case 1:
        break;
    default:
        return 61;
    }
    if (visits != 4)
        return 62;
    char s[128];
    const char expected[] = "-0000042|0x2a|abc|70000";
    if (snprintf(s, sizeof(s), "%+08d|%#x|%.*s|%zu", -42, 42u, 3, "abcdef", (size_t)70000) !=
            (int)strlen(expected) ||
        strcmp(s, expected))
        return 63;
    if (snprintf(s, 5, "abcdef") != 6 || strcmp(s, "abcd"))
        return 64;
    if (snprintf(0, 0, "%*s", INT_MAX, "") != INT_MAX)
        return 65;
    if (snprintf(s, sizeof(s), "x%*s", INT_MAX, "") != -1 || errno != EOVERFLOW)
        return 66;
    if (snprintf(s, sizeof(s), "%lld %llu", LLONG_MIN, ULLONG_MAX) != 41 ||
        strcmp(s, "-9223372036854775808 18446744073709551615"))
        return 67;
    if (snprintf(s, sizeof(s), "%#.0o:%#.0x:%hhd:%hu", 0u, 0u, 257, 65535) != 10 ||
        strcmp(s, "0::1:65535"))
        return 68;
    if (snprintf(s, sizeof(s), "%f", 1.0) != -1 || errno != EINVAL)
        return 69;
    strcpy(s, "abcd");
    memmove(s + 1, s, 4);
    if (memcmp(s, "aabcd", 5) || !strstr("abcde", "bcd") || strstr("abc", "z") ||
        strchr("abc", 0) == 0 || strcmp("\xff", "\x7f") <= 0)
        return 70;
    char *copy = strdup("copied");
    if (!copy || strcmp(copy, "copied"))
        return 71;
    free(copy);
    memset(s, 0xff, sizeof(s));
    strncpy(s, "xy", 5);
    if (s[0] != 'x' || s[1] != 'y' || s[2] || s[3] || s[4] || (unsigned char)s[5] != 255)
        return 72;
    FILE *f = fopen("format.txt", "wb");
    if (!f || fprintf(f, "%0300u", 7u) != 300 || fclose(f))
        return 73;
    f = fopen("format.txt", "rb");
    if (!f || fseek(f, 299, SEEK_SET) || fgetc(f) != '7' || fclose(f))
        return 74;
    if (printf("C LIBRARY TEST %s\n", "PASS") < 0)
        return 75;
    return 0;
}
