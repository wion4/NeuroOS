#include "heap.h"
#include "abi.h"
#define HEAP_BASE 0x1000000ul
#define HEAP_BYTES (512ul * 4096)
struct block {
    size_t size;
    struct block *next;
    unsigned available;
    unsigned reserved[3];
};
_Static_assert(sizeof(struct block) == 32, "16-byte malloc alignment");
static struct block *first;
static size_t committed;
static void split(struct block *b, size_t n) {
    if (b->size >= n + sizeof(*b) + 16) {
        struct block *rest = (struct block *)((char *)(b + 1) + n);
        *rest = (struct block){.size = b->size - n - sizeof(*b), .next = b->next, .available = 1};
        b->size = n;
        b->next = rest;
    }
}
static void coalesce(void) {
    for (struct block *b = first; b && b->next;) {
        if (b->available && b->next->available) {
            b->size += sizeof(*b) + b->next->size;
            b->next = b->next->next;
        } else
            b = b->next;
    }
}
void *malloc(size_t bytes) {
    if (!bytes || bytes > HEAP_BYTES - sizeof(struct block) - 15)
        return 0;
    bytes = (bytes + 15) & ~(size_t)15;
    struct block *tail = 0;
    for (struct block *b = first; b; b = b->next) {
        if (b->available && b->size >= bytes) {
            split(b, bytes);
            b->available = 0;
            return b + 1;
        }
        tail = b;
    }
    size_t need = bytes + sizeof(struct block);
    if (tail && tail->available)
        need = bytes - tail->size;
    if (need > HEAP_BYTES - committed)
        return 0;
    size_t growth = (need + 4095) & ~(size_t)4095;
    long end = call3(10, 1, (committed + growth) / 4096, 0);
    if (end < 0)
        return 0;
    struct block *b;
    if (tail && tail->available) {
        b = tail;
        b->size += growth;
    } else {
        b = (struct block *)(HEAP_BASE + committed);
        *b = (struct block){.size = growth - sizeof(*b), .available = 1};
        if (tail)
            tail->next = b;
        else
            first = b;
    }
    committed += growth;
    split(b, bytes);
    b->available = 0;
    return b + 1;
}
void free(void *pointer) {
    if (!pointer)
        return;
    ((struct block *)pointer - 1)->available = 1;
    coalesce();
}
void *calloc(size_t count, size_t bytes) {
    if (bytes && count > (size_t)-1 / bytes)
        return 0;
    size_t total = count * bytes;
    unsigned char *p = malloc(total);
    if (p)
        for (size_t i = 0; i < total; ++i)
            p[i] = 0;
    return p;
}
void *realloc(void *pointer, size_t bytes) {
    if (!pointer)
        return malloc(bytes);
    if (!bytes) {
        free(pointer);
        return 0;
    }
    struct block *old = (struct block *)pointer - 1;
    if (bytes <= old->size) {
        size_t aligned = (bytes + 15) & ~(size_t)15;
        split(old, aligned);
        coalesce();
        return pointer;
    }
    unsigned char *p = malloc(bytes);
    if (!p)
        return 0;
    for (size_t i = 0; i < old->size; ++i)
        p[i] = ((unsigned char *)pointer)[i];
    free(pointer);
    return p;
}
