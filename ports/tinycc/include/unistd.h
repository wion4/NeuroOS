#ifndef NEUROOS_TCC_PROBE_UNISTD_H
#define NEUROOS_TCC_PROBE_UNISTD_H
#include <stddef.h>
typedef long ssize_t;
typedef long off_t;
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
off_t lseek(int, off_t, int);
int close(int);
int unlink(const char *);
int fchmod(int, unsigned);
char *getcwd(char *, size_t);
#endif
