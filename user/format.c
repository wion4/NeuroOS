#include "heap.h"
#include "libc/errno.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
/* Integer/string subset used by compiler diagnostics. Floating/wide conversions
 * and %n are rejected with EINVAL instead of consuming arguments incorrectly. */
struct output {
    char *data;
    size_t capacity, count;
    int error;
};
static void repeat(struct output *o, char c, size_t n) {
    if (o->error)
        return;
    if (n > (size_t)INT_MAX - o->count) {
        o->error = EOVERFLOW;
        return;
    }
    size_t room = o->capacity && o->count < o->capacity - 1 ? o->capacity - 1 - o->count : 0;
    size_t written = n < room ? n : room;
    for (size_t i = 0; i < written; ++i)
        o->data[o->count + i] = c;
    o->count += n;
}
static void span(struct output *o, const char *s, size_t n) {
    if (o->error)
        return;
    if (n > (size_t)INT_MAX - o->count) {
        o->error = EOVERFLOW;
        return;
    }
    size_t room = o->capacity && o->count < o->capacity - 1 ? o->capacity - 1 - o->count : 0;
    size_t written = n < room ? n : room;
    for (size_t i = 0; i < written; ++i)
        o->data[o->count + i] = s[i];
    o->count += n;
}
static size_t decimal(const char **format, struct output *o) {
    size_t n = 0;
    while (**format >= '0' && **format <= '9') {
        unsigned d = (unsigned)(*(*format)++ - '0');
        if (n > ((size_t)INT_MAX - d) / 10) {
            o->error = EOVERFLOW;
            return 0;
        }
        n = n * 10 + d;
    }
    return n;
}
int vsnprintf(char *buffer, size_t bytes, const char *format, va_list arguments) {
    struct output o = {.data = buffer, .capacity = bytes};
    if (!format || (bytes && !buffer)) {
        errno = EINVAL;
        return -1;
    }
    va_list args;
    va_copy(args, arguments);
    while (*format && !o.error) {
        if (*format != '%') {
            repeat(&o, *format++, 1);
            continue;
        }
        ++format;
        if (*format == '%') {
            repeat(&o, *format++, 1);
            continue;
        }
        bool left = false, zero = false, plus = false, space = false, alternate = false;
        for (;;) {
            if (*format == '-')
                left = true;
            else if (*format == '0')
                zero = true;
            else if (*format == '+')
                plus = true;
            else if (*format == ' ')
                space = true;
            else if (*format == '#')
                alternate = true;
            else
                break;
            ++format;
        }
        size_t width = 0, precision = 0;
        bool specified = false;
        if (*format == '*') {
            int w = va_arg(args, int);
            ++format;
            if (w < 0) {
                left = true;
                width = (size_t)-(int64_t)w;
            } else
                width = (size_t)w;
            if (width > INT_MAX)
                o.error = EOVERFLOW;
        } else
            width = decimal(&format, &o);
        if (*format == '.') {
            ++format;
            specified = true;
            if (*format == '*') {
                int p = va_arg(args, int);
                ++format;
                if (p < 0)
                    specified = false;
                else
                    precision = (size_t)p;
            } else
                precision = decimal(&format, &o);
        }
        enum { NORMAL, HH, H, L, LL, Z, T, J } length = NORMAL;
        if (*format == 'h') {
            ++format;
            length = H;
            if (*format == 'h') {
                ++format;
                length = HH;
            }
        } else if (*format == 'l') {
            ++format;
            length = L;
            if (*format == 'l') {
                ++format;
                length = LL;
            }
        } else if (*format == 'z') {
            ++format;
            length = Z;
        } else if (*format == 't') {
            ++format;
            length = T;
        } else if (*format == 'j') {
            ++format;
            length = J;
        }
        if (o.error)
            break;
        char conversion = *format;
        if (!conversion) {
            o.error = EINVAL;
            break;
        }
        ++format;
        if (conversion == 's' || conversion == 'c') {
            if (length != NORMAL) {
                o.error = EINVAL;
                break;
            }
            char c = 0;
            const char *s;
            size_t n;
            if (conversion == 'c') {
                c = (char)va_arg(args, int);
                s = &c;
                n = 1;
            } else {
                s = va_arg(args, const char *);
                if (!s)
                    s = "(null)";
                n = 0;
                while ((!specified || n < precision) && s[n])
                    ++n;
            }
            size_t pad = width > n ? width - n : 0;
            if (!left)
                repeat(&o, ' ', pad);
            span(&o, s, n);
            if (left)
                repeat(&o, ' ', pad);
            continue;
        }
        bool sign = conversion == 'd' || conversion == 'i';
        bool pointer = conversion == 'p';
        unsigned base = conversion == 'o'                                     ? 8
                        : (conversion == 'x' || conversion == 'X' || pointer) ? 16
                                                                              : 10;
        if (!sign && !pointer && conversion != 'u' && conversion != 'o' && conversion != 'x' &&
            conversion != 'X') {
            o.error = EINVAL;
            break;
        }
        uint64_t value;
        bool negative = false;
        if (pointer) {
            if (length != NORMAL) {
                o.error = EINVAL;
                break;
            }
            value = (uintptr_t)va_arg(args, void *);
        } else if (sign) {
            int64_t v;
            switch (length) {
            case HH:
                v = (signed char)va_arg(args, int);
                break;
            case H:
                v = (short)va_arg(args, int);
                break;
            case L:
                v = va_arg(args, long);
                break;
            case LL:
                v = va_arg(args, long long);
                break;
            case Z:
            case T:
                v = va_arg(args, ptrdiff_t);
                break;
            case J:
                v = va_arg(args, intmax_t);
                break;
            default:
                v = va_arg(args, int);
                break;
            }
            negative = v < 0;
            value = negative ? 0 - (uint64_t)v : (uint64_t)v;
        } else {
            switch (length) {
            case HH:
                value = (unsigned char)va_arg(args, int);
                break;
            case H:
                value = (unsigned short)va_arg(args, int);
                break;
            case L:
                value = va_arg(args, unsigned long);
                break;
            case LL:
                value = va_arg(args, unsigned long long);
                break;
            case Z:
            case T:
                value = va_arg(args, size_t);
                break;
            case J:
                value = va_arg(args, uintmax_t);
                break;
            default:
                value = va_arg(args, unsigned);
                break;
            }
        }
        const char *digits = conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        char reversed[64], number[64], prefix[3];
        size_t n = 0, p = 0;
        uint64_t original = value;
        if (value || !specified || precision || pointer)
            do {
                reversed[n++] = digits[value % base];
                value /= base;
            } while (value);
        for (size_t i = 0; i < n; ++i)
            number[i] = reversed[n - 1 - i];
        if (sign) {
            if (negative)
                prefix[p++] = '-';
            else if (plus)
                prefix[p++] = '+';
            else if (space)
                prefix[p++] = ' ';
        }
        if (pointer || (alternate && base == 16 && original)) {
            prefix[p++] = '0';
            prefix[p++] = conversion == 'X' ? 'X' : 'x';
        }
        if (alternate && base == 8 && (!n || number[0] != '0') && precision <= n)
            precision = n + 1;
        size_t zeros = precision > n ? precision - n : 0;
        size_t total = n + zeros + p;
        size_t padding = width > total ? width - total : 0;
        if (!left && (!zero || specified))
            repeat(&o, ' ', padding);
        span(&o, prefix, p);
        if (!left && zero && !specified)
            repeat(&o, '0', padding);
        repeat(&o, '0', zeros);
        span(&o, number, n);
        if (left)
            repeat(&o, ' ', padding);
    }
    va_end(args);
    if (bytes)
        buffer[o.count < bytes ? o.count : bytes - 1] = 0;
    if (o.error) {
        errno = o.error;
        return -1;
    }
    return (int)o.count;
}
int snprintf(char *buffer, size_t bytes, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer, bytes, format, args);
    va_end(args);
    return n;
}
int vsprintf(char *buffer, const char *format, va_list args) {
    return vsnprintf(buffer, (size_t)-1, format, args);
}
int sprintf(char *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsprintf(buffer, format, args);
    va_end(args);
    return n;
}
int vfprintf(FILE *stream, const char *format, va_list args) {
    char small[256];
    int n = vsnprintf(small, sizeof(small), format, args);
    if (n < 0)
        return -1;
    char *text = small;
    if ((size_t)n >= sizeof(small)) {
        text = malloc((size_t)n + 1);
        if (!text) {
            errno = ENOMEM;
            return -1;
        }
        if (vsnprintf(text, (size_t)n + 1, format, args) != n) {
            free(text);
            return -1;
        }
    }
    int result = fwrite(text, 1, (size_t)n, stream) == (size_t)n ? n : -1;
    if (text != small)
        free(text);
    return result;
}
int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(stream, format, args);
    va_end(args);
    return n;
}
int vprintf(const char *format, va_list args) { return vfprintf(stdout, format, args); }
int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vprintf(format, args);
    va_end(args);
    return n;
}
