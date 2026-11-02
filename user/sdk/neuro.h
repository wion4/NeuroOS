#ifndef NEUROOS_SDK_H
#define NEUROOS_SDK_H
#include <stddef.h>
#include <stdint.h>
long nos_write(const void *buffer, size_t bytes);
void nos_yield(void);
uint64_t nos_ticks(void);
_Noreturn void nos_exit(int status);
long nos_file_read(const char *name, unsigned offset, void *data, unsigned bytes);
long nos_file_write(const char *name, const void *data, unsigned bytes);
long nos_file_pwrite(const char *name, unsigned offset, const void *data, unsigned bytes);
long nos_file_size(const char *name);
#endif
