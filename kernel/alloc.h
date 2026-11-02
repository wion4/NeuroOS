#ifndef NEUROOS_ALLOC_H
#define NEUROOS_ALLOC_H
#include "memory.h"
#include <stddef.h>
#define PAGE_SIZE 4096ull
/* Metadata capacity: 64 GiB of usable pages, independent of physical holes. */
#define PMM_MAX_PAGES (16u * 1024u * 1024u)
bool pmm_init(const struct limine_memmap_response *map);
uint64_t pmm_alloc(uint64_t pages);
bool pmm_free(uint64_t address, uint64_t pages);
uint64_t pmm_total(void);
uint64_t pmm_available(void);
bool heap_init(void *arena, size_t bytes);
void *heap_alloc(size_t bytes);
bool heap_free(void *ptr);
size_t heap_available(void);
#endif
