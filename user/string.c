#include "libc/string.h"
#include "heap.h"
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}
char *strcpy(char *d, const char *s) {
    char *out = d;
    do {
        *d++ = *s;
    } while (*s++);
    return out;
}
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; ++i)
        d[i] = s[i];
    for (; i < n; ++i)
        d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}
char *strncat(char *d, const char *s, size_t n) {
    char *end = d + strlen(d);
    while (n-- && *s)
        *end++ = *s++;
    *end = 0;
    return d;
}
int strcmp(const char *a, const char *b) {
    while (*a && (unsigned char)*a == (unsigned char)*b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n--) {
        int d = (unsigned char)*a - (unsigned char)*b;
        if (d || !*a)
            return d;
        ++a;
        ++b;
    }
    return 0;
}
char *strchr(const char *s, int c) {
    do {
        if ((unsigned char)*s == (unsigned char)c)
            return (char *)s;
    } while (*s++);
    return 0;
}
char *strrchr(const char *s, int c) {
    const char *found = 0;
    do {
        if ((unsigned char)*s == (unsigned char)c)
            found = s;
    } while (*s++);
    return (char *)found;
}
char *strstr(const char *s, const char *pattern) {
    if (!*pattern)
        return (char *)s;
    size_t n = strlen(pattern);
    for (; *s; ++s)
        if (!strncmp(s, pattern, n))
            return (char *)s;
    return 0;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}
char *strerror(int e) {
    switch (e) {
    case 0:
        return "Success";
    case 1:
        return "Permission denied";
    case 2:
        return "No such file";
    case 5:
        return "Input/output error";
    case 9:
        return "Bad file handle";
    case 12:
        return "Out of memory";
    case 22:
        return "Invalid argument";
    case 24:
        return "Too many open files";
    case 27:
        return "File too large";
    case 28:
        return "No space left";
    case 75:
        return "Value too large";
    default:
        return "Unknown error";
    }
}
