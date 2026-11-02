#include "libc/stdlib.h"
#include "abi.h"
#include "libc/errno.h"
#include "libc/stdio.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
static bool whitespace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static unsigned digit(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    return 36;
}
struct integer {
    unsigned long long value;
    bool negative, overflow;
};
static struct integer scan(const char *s, char **end, int base, unsigned long long positive,
                           unsigned long long negative) {
    struct integer r = {0};
    const char *start = s;
    if (end)
        *end = (char *)start;
    if (base && (base < 2 || base > 36)) {
        errno = EINVAL;
        return r;
    }
    while (whitespace((unsigned char)*s))
        ++s;
    if (*s == '+' || *s == '-')
        r.negative = *s++ == '-';
    if ((!base || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        digit((unsigned char)s[2]) < 16) {
        base = 16;
        s += 2;
    }
    if (!base)
        base = *s == '0' ? 8 : 10;
    unsigned long long limit = r.negative ? negative : positive;
    unsigned long long cutoff = limit / (unsigned)base;
    unsigned cutlim = (unsigned)(limit % (unsigned)base);
    const char *digits = s;
    for (unsigned d; (d = digit((unsigned char)*s)) < (unsigned)base; ++s) {
        if (r.value > cutoff || (r.value == cutoff && d > cutlim))
            r.overflow = true;
        if (!r.overflow)
            r.value = r.value * (unsigned)base + d;
    }
    if (s != digits && end)
        *end = (char *)s;
    if (r.overflow) {
        errno = ERANGE;
        r.value = limit;
    }
    return r;
}
unsigned long long strtoull(const char *s, char **end, int base) {
    struct integer r = scan(s, end, base, ULLONG_MAX, ULLONG_MAX);
    return r.negative && !r.overflow ? 0 - r.value : r.value;
}
unsigned long strtoul(const char *s, char **end, int base) {
    struct integer r = scan(s, end, base, ULONG_MAX, ULONG_MAX);
    return (unsigned long)(r.negative && !r.overflow ? 0 - r.value : r.value);
}
long long strtoll(const char *s, char **end, int base) {
    struct integer r = scan(s, end, base, LLONG_MAX, (unsigned long long)LLONG_MAX + 1);
    return r.negative
               ? (r.value == (unsigned long long)LLONG_MAX + 1 ? LLONG_MIN : -(long long)r.value)
               : (long long)r.value;
}
long strtol(const char *s, char **end, int base) {
    struct integer r = scan(s, end, base, LONG_MAX, (unsigned long long)LONG_MAX + 1);
    return r.negative ? (r.value == (unsigned long long)LONG_MAX + 1 ? LONG_MIN : -(long)r.value)
                      : (long)r.value;
}
int atoi(const char *s) { return (int)strtol(s, 0, 10); }
static void swap(char *a, char *b, size_t size) {
    while (size--) {
        char c = *a;
        *a++ = *b;
        *b++ = c;
    }
}
static void sift(char *base, size_t root, size_t count, size_t size,
                 int (*compare)(const void *, const void *)) {
    while (root < count / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0)
            ++child;
        if (compare(base + root * size, base + child * size) >= 0)
            break;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    if (!size || count < 2)
        return;
    if (count > (size_t)-1 / size) {
        errno = EINVAL;
        return;
    }
    char *array = base;
    for (size_t start = count / 2; start;) {
        --start;
        sift(array, start, count, size, compare);
    }
    for (size_t end = count - 1; end; --end) {
        swap(array, array + end * size, size);
        sift(array, 0, end, size, compare);
    }
}
_Noreturn void exit(int status) { finish((unsigned)status); }
_Noreturn void abort(void) {
    say("C ABORT\n");
    finish(134);
}
_Noreturn void __assert_fail(const char *condition, const char *file, unsigned line,
                             const char *function) {
    (void)fprintf(stderr, "%s:%u: assertion %s failed (%s)\n", file, line, condition, function);
    abort();
}
