#include "paging.h"
#include "alloc.h"
#include "elf.h"
#define ADDRESS_MASK 0x000ffffffffff000ull
static uint64_t direct, root;
static bool nx_ready;
static uint64_t *table(uint64_t phys) { return (uint64_t *)(direct + phys); }
static void release_tree(uint64_t phys, unsigned level) {
    uint64_t *t = table(phys);
    if (level > 1)
        for (unsigned i = 0; i < 512; ++i)
            if ((t[i] & 1) && !(t[i] & 128))
                release_tree(t[i] & ADDRESS_MASK, level - 1);
    (void)pmm_free(phys, 1);
}
static uint64_t clone_tree(uint64_t phys, unsigned level) {
    uint64_t copy = pmm_alloc(1);
    if (!copy)
        return 0;
    uint64_t *src = table(phys), *dst = table(copy);
    for (unsigned i = 0; i < 512; ++i)
        dst[i] = 0;
    for (unsigned i = 0; i < 512; ++i) {
        uint64_t entry = src[i];
        if ((entry & 1) && level > 1 && !(entry & 128)) {
            uint64_t child = clone_tree(entry & ADDRESS_MASK, level - 1);
            if (!child) {
                release_tree(copy, level);
                return 0;
            }
            entry = (entry & ~ADDRESS_MASK) | child;
        }
        dst[i] = entry;
    }
    return copy;
}
bool paging_init(uint64_t hhdm) {
    uint64_t cr3, cr4;
    __asm__ volatile("mov %%cr3,%0; mov %%cr4,%1" : "=r"(cr3), "=r"(cr4));
    if (cr4 & (1ull << 12))
        return false; /* Four-level only. */
    direct = hhdm;
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001u), "c"(0));
    if (!(d & (1u << 20)))
        return false; /* Require NX for user data/stack. */
    __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080u));
    a |= 1u << 11;
    __asm__ volatile("wrmsr" : : "a"(a), "d"(d), "c"(0xc0000080u) : "memory");
    nx_ready = true;
    /* This dedicated scratch region must not shadow an existing mapping. */
    if (table(cr3 & ADDRESS_MASK)[(VM_TEST_ADDRESS >> 39) & 511] & 1)
        return false;
    root = clone_tree(cr3 & ADDRESS_MASK, 4);
    if (!root)
        return false;
    __asm__ volatile("mov %0,%%cr3" : : "r"(root | (cr3 & 0xfff)) : "memory");
    return true;
}

void paging_switch(uint64_t address_space) {
    __asm__ volatile("mov %0,%%cr3" : : "r"(address_space) : "memory");
}
uint64_t paging_kernel_root(void) { return root; }
static uint64_t user_page(struct user_space *s) {
    if (s->count == 192)
        return 0;
    uint64_t phys = pmm_alloc(1);
    if (!phys)
        return 0;
    s->owned[s->count++] = phys;
    for (unsigned i = 0; i < 512; ++i)
        table(phys)[i] = 0;
    return phys;
}
static bool user_map(struct user_space *s, uint64_t va, uint64_t phys, uint64_t flags) {
    uint64_t *t = table(s->root);
    for (unsigned shift = 39; shift > 12; shift -= 9) {
        unsigned index = (va >> shift) & 511;
        if (!(t[index] & 1)) {
            uint64_t child = user_page(s);
            if (!child)
                return false;
            t[index] = child | 7;
        }
        t = table(t[index] & ADDRESS_MASK);
    }
    t[(va >> 12) & 511] = phys | flags;
    return true;
}
static uint64_t *heap_table(const struct user_space *s) {
    if (!s->root)
        return 0;
    uint64_t *t = table(s->root);
    for (unsigned shift = 39; shift > 12; shift -= 9) {
        uint64_t e = t[(USER_HEAP >> shift) & 511];
        if ((e & 7) != 7 || (e & 128))
            return 0;
        t = table(e & ADDRESS_MASK);
    }
    return t;
}
static void heap_remove(uint64_t *t, unsigned first, unsigned end) {
    for (unsigned i = first; i < end; ++i) {
        uint64_t phys = t[i] & ADDRESS_MASK;
        t[i] = 0;
        __asm__ volatile("invlpg (%0)" ::"r"(USER_HEAP + (uint64_t)i * 4096) : "memory");
        (void)pmm_free(phys, 1);
    }
}
bool user_heap_resize(struct user_space *s, unsigned pages) {
    if (pages > USER_HEAP_PAGES)
        return false;
    uint64_t *t = heap_table(s);
    if (!t)
        return false;
    if (pages < s->heap_pages)
        heap_remove(t, pages, s->heap_pages);
    else {
        unsigned i = s->heap_pages;
        for (; i < pages; ++i) {
            uint64_t phys = pmm_alloc(1);
            if (!phys) {
                /* Failed growth leaves the previous break and contents intact. */
                heap_remove(t, s->heap_pages, i);
                return false;
            }
            for (unsigned j = 0; j < 512; ++j)
                table(phys)[j] = 0;
            t[i] = phys | 7 | (1ull << 63);
            __asm__ volatile("invlpg (%0)" ::"r"(USER_HEAP + (uint64_t)i * 4096) : "memory");
        }
    }
    s->heap_pages = pages;
    return true;
}
void user_space_destroy(struct user_space *s) {
    /* Caller must switch away first. Shared supervisor mappings are not owned. */
    if (s->heap_pages)
        heap_remove(heap_table(s), 0, s->heap_pages);
    s->heap_pages = 0;
    for (unsigned i = 0; i < s->count; ++i)
        (void)pmm_free(s->owned[i], 1);
    s->count = 0;
    s->root = 0;
}
bool user_space_create(struct user_space *s, const void *code, size_t bytes, uint64_t seed) {
    s->count = 0;
    s->heap_pages = 0;
    s->root = 0;
    if (!nx_ready || !code || !bytes || bytes > 4096)
        return false;
    s->root = user_page(s);
    if (!s->root)
        return false;
    /* All user roots share only the supervisor-only higher half. */
    for (unsigned i = 256; i < 512; ++i)
        table(s->root)[i] = table(root)[i] & ~4ull;
    uint64_t text = user_page(s), data = user_page(s), stack = user_page(s);
    if (!text || !data || !stack || !user_map(s, USER_CODE, text, 5) ||
        !user_map(s, USER_DATA, data, 7 | (1ull << 63)) ||
        !user_map(s, USER_STACK, stack, 7 | (1ull << 63)) || !user_map(s, USER_HEAP, 0, 0)) {
        user_space_destroy(s);
        return false;
    }
    const uint8_t *src = code;
    uint8_t *dst = (uint8_t *)table(text);
    for (size_t i = 0; i < bytes; ++i)
        dst[i] = src[i];
    table(data)[0] = seed;
    return true;
}
bool user_copy_from(const struct user_space *s, void *dst, uint64_t src, size_t bytes) {
    if (src >= (1ull << 47) || bytes > (1ull << 47) - src)
        return false;
    uint8_t *out = dst;
    while (bytes) {
        uint64_t *t = table(s->root), entry = 0;
        for (unsigned shift = 39;; shift -= 9) {
            entry = t[(src >> shift) & 511];
            if ((entry & 5) != 5)
                return false;
            if (shift == 12)
                break;
            if (entry & 128)
                return false;
            t = table(entry & ADDRESS_MASK);
        }
        size_t offset = src & 4095, chunk = 4096 - offset;
        if (chunk > bytes)
            chunk = bytes;
        uint8_t *physical = (uint8_t *)table(entry & ADDRESS_MASK) + offset;
        for (size_t i = 0; i < chunk; ++i)
            *out++ = physical[i];
        src += chunk;
        bytes -= chunk;
    }
    return true;
}
static uint64_t user_writable(const struct user_space *s, uint64_t va) {
    if (va >= (1ull << 47))
        return 0;
    uint64_t *t = table(s->root);
    for (unsigned shift = 39;; shift -= 9) {
        uint64_t e = t[(va >> shift) & 511];
        if ((e & 7) != 7)
            return 0;
        if (shift == 12)
            return (e & ADDRESS_MASK) + (va & 4095);
        if (e & 128)
            return 0;
        t = table(e & ADDRESS_MASK);
    }
}
bool user_check_write(const struct user_space *s, uint64_t dst, size_t bytes) {
    if (dst >= (1ull << 47) || bytes > (1ull << 47) - dst)
        return false;
    /* Validate the entire destination before any write: errors are atomic. */
    for (size_t done = 0; done < bytes;) {
        if (!user_writable(s, dst + done))
            return false;
        size_t n = 4096 - ((dst + done) & 4095);
        if (n > bytes - done)
            n = bytes - done;
        done += n;
    }
    return true;
}
bool user_copy_to(const struct user_space *s, uint64_t dst, const void *src, size_t bytes) {
    if (!user_check_write(s, dst, bytes))
        return false;
    const uint8_t *in = src;
    while (bytes) {
        uint64_t phys = user_writable(s, dst);
        size_t n = 4096 - (dst & 4095);
        if (n > bytes)
            n = bytes;
        uint8_t *out = (uint8_t *)(direct + phys);
        for (size_t i = 0; i < n; ++i)
            *out++ = *in++;
        dst += n;
        bytes -= n;
    }
    return true;
}
bool user_space_elf(struct user_space *s, const void *file, size_t bytes, uint64_t *entry) {
    struct elf_image image;
    s->count = 0;
    s->heap_pages = 0;
    s->root = 0;
    if (!nx_ready || !entry || !elf_validate(file, bytes, &image))
        return false;
    s->root = user_page(s);
    if (!s->root)
        return false;
    for (unsigned i = 256; i < 512; ++i)
        table(s->root)[i] = table(root)[i] & ~4ull;
    for (uint64_t i = 0; i < image.count; ++i) {
        const struct elf_segment *segment = &image.segments[i];
        uint64_t flags =
            5 | ((segment->flags & 2) ? 2 : 0) | ((segment->flags & 1) ? 0 : (1ull << 63));
        for (uint64_t offset = 0; offset < segment->memsz; offset += 4096) {
            uint64_t phys = user_page(s);
            if (!phys || !user_map(s, segment->address + offset, phys, flags))
                goto fail;
            size_t n = offset < segment->filesz ? (size_t)(segment->filesz - offset) : 0;
            if (n > 4096)
                n = 4096;
            const uint8_t *in = n ? (const uint8_t *)file + segment->offset + offset : 0;
            uint8_t *out = (uint8_t *)table(phys);
            for (size_t j = 0; j < n; ++j)
                out[j] = in[j];
        }
    }
    for (unsigned i = 0; i < USER_ELF_STACK_PAGES; ++i) {
        uint64_t stack = user_page(s);
        if (!stack || !user_map(s, USER_STACK + (uint64_t)i * 4096, stack, 7 | (1ull << 63)))
            goto fail;
    }
    if (!user_map(s, USER_HEAP, 0, 0))
        goto fail;
    *entry = image.entry;
    return true;
fail:
    user_space_destroy(s);
    return false;
}
bool paging_selftest(void) {
    if (!root)
        return false;
    uint64_t pages[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < 4; ++i) {
        pages[i] = pmm_alloc(1);
        if (!pages[i]) {
            for (unsigned j = 0; j < i; ++j)
                (void)pmm_free(pages[j], 1);
            return false;
        }
        uint64_t *t = table(pages[i]);
        for (unsigned j = 0; j < 512; ++j)
            t[j] = 0;
    }
    const unsigned slot = (VM_TEST_ADDRESS >> 39) & 511;
    if (table(root)[slot] & 1) {
        for (unsigned i = 0; i < 4; ++i)
            (void)pmm_free(pages[i], 1);
        return false;
    }
    table(pages[0])[0] = pages[1] | 3;
    table(pages[1])[0] = pages[2] | 3;
    table(pages[2])[0] = pages[3] | 3;
    table(root)[slot] = pages[0] | 3;
    __asm__ volatile("invlpg (%0)" : : "r"(VM_TEST_ADDRESS) : "memory");
    volatile uint64_t *alias = (volatile uint64_t *)VM_TEST_ADDRESS;
    alias[0] = 0x4e4555524f4f5303ull;
    alias[511] = ~alias[0];
    bool ok = table(pages[3])[0] == 0x4e4555524f4f5303ull &&
              table(pages[3])[511] == ~0x4e4555524f4f5303ull;
    table(root)[slot] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(VM_TEST_ADDRESS) : "memory");
    for (unsigned i = 0; i < 4; ++i)
        ok = pmm_free(pages[i], 1) && ok;
    return ok;
}
