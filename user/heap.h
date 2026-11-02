#ifndef NEUROOS_USER_HEAP_H
#define NEUROOS_USER_HEAP_H
#include <stddef.h>
void *malloc(size_t bytes);
void *calloc(size_t count, size_t bytes);
void *realloc(void *pointer, size_t bytes);
void free(void *pointer);
#endif
