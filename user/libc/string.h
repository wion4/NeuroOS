#ifndef NEUROOS_STRING_H
#define NEUROOS_STRING_H
#include <stddef.h>
void *memset(void *destination, int value, size_t bytes);
void *memcpy(void *destination, const void *source, size_t bytes);
void *memmove(void *destination, const void *source, size_t bytes);
int memcmp(const void *left, const void *right, size_t bytes);
size_t strlen(const char *text);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t bytes);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t bytes);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t bytes);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strstr(const char *text, const char *pattern);
char *strdup(const char *text);
char *strerror(int error);
#endif
