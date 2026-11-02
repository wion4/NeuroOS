/* Compile-only declarations, not implementations. */
#include "../../../user/libc/stdio.h"
FILE *fdopen(int, const char *);
int fileno(FILE *);
int remove(const char *);
int sscanf(const char *, const char *, ...);
#define getc(s) fgetc(s)
#define putc(c,s) fputc(c,s)
