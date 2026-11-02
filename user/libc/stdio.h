#ifndef NEUROOS_STDIO_H
#define NEUROOS_STDIO_H
#include <stdarg.h>
#include <stddef.h>
typedef struct nos_stream FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *name, const char *mode);
FILE *fdopen(int descriptor, const char *mode);
int fileno(FILE *stream);
int fclose(FILE *stream);
size_t fread(void *data, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *data, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fflush(FILE *stream);
int fgetc(FILE *stream);
int fputc(int character, FILE *stream);
int ungetc(int character, FILE *stream);
char *fgets(char *buffer, int count, FILE *stream);
int fputs(const char *text, FILE *stream);
int puts(const char *text);
int vsnprintf(char *buffer, size_t bytes, const char *format, va_list arguments);
int snprintf(char *buffer, size_t bytes, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list arguments);
int sprintf(char *buffer, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list arguments);
int printf(const char *format, ...);
#endif
