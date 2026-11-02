#ifndef NEUROOS_STDLIB_H
#define NEUROOS_STDLIB_H
#include "../heap.h"
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
int atoi(const char *);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
_Noreturn void exit(int);
_Noreturn void abort(void);
char *realpath(const char *path, char *resolved);
#endif
