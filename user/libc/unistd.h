#ifndef NEUROOS_UNISTD_H
#define NEUROOS_UNISTD_H
#include <stddef.h>
typedef long ssize_t;
typedef long off_t;
ssize_t read(int descriptor, void *buffer, size_t bytes);
ssize_t write(int descriptor, const void *buffer, size_t bytes);
off_t lseek(int descriptor, off_t offset, int origin);
int close(int descriptor);
int unlink(const char *path);
char *getcwd(char *buffer, size_t bytes);
#endif
