#ifndef NEUROOS_MUSL_SCAN_STDIO_H
#define NEUROOS_MUSL_SCAN_STDIO_H
/* Private string scanner; deliberately separate from the public FILE ABI. */
#include <stddef.h>
typedef struct { const unsigned char *current, *count_from; } FILE;
#endif
