#ifndef NEUROOS_MEMORY_H
#define NEUROOS_MEMORY_H
#include <stdbool.h>
#include "limine.h"
struct memory_summary { uint64_t usable_bytes, usable_regions; };
/* No allocation or dereferencing physical addresses. Input is bootloader metadata. */
bool memory_analyze(const struct limine_memmap_response *map, struct memory_summary *out);
#endif
