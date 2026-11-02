#include "memory.h"
bool memory_analyze(const struct limine_memmap_response *map, struct memory_summary *out) {
    if (!out) return false;
    out->usable_bytes = 0;
    out->usable_regions = 0;
    if (!map || !map->entries || !map->entry_count || map->entry_count > 4096)
        return false;
    uint64_t bytes = 0, regions = 0;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *e = map->entries[i];
        if (!e || e->length == 0 || e->base > UINT64_MAX - e->length) return false;
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (bytes > UINT64_MAX - e->length) return false;
        /* A usable range must not overlap any other kind of range. Reserved
           ranges may overlap each other; that is legal in the boot protocol. */
        for (uint64_t j = 0; j < map->entry_count; ++j) {
            if (i == j) continue;
            const struct limine_memmap_entry *other = map->entries[j];
            if (!other || other->base > UINT64_MAX - other->length) return false;
            if (other->length && e->base < other->base + other->length &&
                other->base < e->base + e->length) return false;
        }
        bytes += e->length;
        ++regions;
    }
    if (!bytes) return false;
    out->usable_bytes = bytes;
    out->usable_regions = regions;
    return true;
}
