/* Declarations for the compile-only portability probe. Missing implementations
 * must remain undefined in its report; this is not a complete guest SDK. */
#include "../../../user/libc/stdlib.h"
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
_Noreturn void exit(int);
_Noreturn void abort(void);
char *getenv(const char *);
char *realpath(const char *, char *);
int atoi(const char *);
