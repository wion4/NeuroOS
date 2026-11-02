#include "alloc.h"
struct range { uint64_t base, pages, bit; };
static struct range ranges[4096];
static unsigned range_count;
static uint64_t total_pages, free_pages;
static uint8_t used[PMM_MAX_PAGES / 8];
static bool bit(uint64_t n) { return (used[n / 8] >> (n % 8)) & 1; }
static void set(uint64_t n, bool value) {
    if (value) used[n / 8] |= (uint8_t)(1u << (n % 8));
    else used[n / 8] &= (uint8_t)~(1u << (n % 8));
}
bool pmm_init(const struct limine_memmap_response *map) {
    struct memory_summary summary;
    total_pages = free_pages = range_count = 0;
    if (!memory_analyze(map, &summary)) return false;
    uint64_t total = 0;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *e = map->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        uint64_t first = e->base / PAGE_SIZE + (e->base % PAGE_SIZE != 0);
        if (!first) first = 1; /* Zero is the allocation-failure sentinel. */
        uint64_t end = (e->base + e->length) / PAGE_SIZE;
        if (end <= first) continue;
        uint64_t pages = end - first;
        if (pages > PMM_MAX_PAGES - total) { range_count = 0; return false; }
        ranges[range_count++] = (struct range){first * PAGE_SIZE, pages, total};
        total += pages;
    }
    for (uint64_t i = 0; i < (total + 7) / 8; ++i) used[i] = 0;
    total_pages = free_pages = total;
    return total != 0;
}
uint64_t pmm_alloc(uint64_t pages) {
    if (!pages || pages > free_pages) return 0;
    for (unsigned r = 0; r < range_count; ++r) {
        struct range *range = &ranges[r];
        uint64_t run = 0;
        for (uint64_t i = 0; i < range->pages; ++i) {
            run = bit(range->bit + i) ? 0 : run + 1;
            if (run == pages) {
                uint64_t first = i + 1 - pages;
                for (uint64_t j = 0; j < pages; ++j) set(range->bit + first + j, true);
                free_pages -= pages;
                return range->base + first * PAGE_SIZE;
            }
        }
    }
    return 0;
}
bool pmm_free(uint64_t address, uint64_t pages) {
    if (!address || !pages || address % PAGE_SIZE || pages > total_pages) return false;
    for (unsigned r = 0; r < range_count; ++r) {
        struct range *range = &ranges[r];
        if (address < range->base) continue;
        uint64_t first = (address - range->base) / PAGE_SIZE;
        if (first >= range->pages || pages > range->pages - first) continue;
        for (uint64_t j = 0; j < pages; ++j) if (!bit(range->bit + first + j)) return false;
        for (uint64_t j = 0; j < pages; ++j) set(range->bit + first + j, false);
        free_pages += pages;
        return true;
    }
    return false;
}
uint64_t pmm_total(void) { return total_pages; }
uint64_t pmm_available(void) { return free_pages; }

/* First-fit arena heap. Single CPU, no allocation inside IRQ handlers.
   Header is a multiple of 16 so all payloads retain 16-byte alignment. */
struct block { size_t bytes; struct block *next; uint64_t magic, allocated; };
_Static_assert(sizeof(struct block) == 32, "heap header alignment");
static struct block *head;
#define HEAP_MAGIC 0x4e4555524f484541ull
bool heap_init(void *arena, size_t bytes) {
    head = 0;
    if (!arena || (uintptr_t)arena % 16 || bytes < sizeof(struct block) + 16 ||
        (uintptr_t)arena > UINTPTR_MAX - bytes) return false;
    bytes &= ~(size_t)15;
    head = arena;
    *head = (struct block){bytes - sizeof(*head), 0, HEAP_MAGIC, 0};
    return true;
}
void *heap_alloc(size_t bytes) {
    if (!bytes || bytes > SIZE_MAX - 15) return 0;
    bytes = (bytes + 15) & ~(size_t)15;
    for (struct block *b = head; b; b = b->next) {
        if (b->magic != HEAP_MAGIC) return 0;
        if (b->allocated || b->bytes < bytes) continue;
        if (b->bytes - bytes >= sizeof(*b) + 16) {
            struct block *tail = (struct block *)((uint8_t *)(b + 1) + bytes);
            *tail = (struct block){b->bytes - bytes - sizeof(*b), b->next, HEAP_MAGIC, 0};
            b->bytes = bytes;
            b->next = tail;
        }
        b->allocated = 1;
        return b + 1;
    }
    return 0;
}
bool heap_free(void *ptr) {
    if (!ptr) return false;
    for (struct block *b = head; b; b = b->next) {
        if (b->magic != HEAP_MAGIC) return false;
        if ((void *)(b + 1) != ptr) continue;
        if (!b->allocated) return false;
        b->allocated = 0;
        for (struct block *c = head; c && c->next;) {
            if (!c->allocated && !c->next->allocated) {
                c->bytes += sizeof(*c) + c->next->bytes;
                c->next = c->next->next;
            } else c = c->next;
        }
        return true;
    }
    return false;
}
size_t heap_available(void) {
    size_t bytes = 0;
    for (struct block *b = head; b; b = b->next) if (!b->allocated) bytes += b->bytes;
    return bytes;
}
