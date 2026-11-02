#include "elf.h"
struct __attribute__((packed)) header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phsize, phnum, shsize, shnum, shstr;
};
struct __attribute__((packed)) program {
    uint32_t type, flags;
    uint64_t offset, va, pa, filesz, memsz, align;
};
_Static_assert(sizeof(struct header) == 64, "ELF header");
_Static_assert(sizeof(struct program) == 56, "ELF program header");
bool elf_validate(const void *file, size_t bytes, struct elf_image *out) {
    if (!out)
        return false;
    out->entry = out->count = 0;
    if (!file || bytes < sizeof(struct header))
        return false;
    const uint8_t *data = file;
    const struct header *h = file;
    if (h->ident[0] != 0x7f || h->ident[1] != 'E' || h->ident[2] != 'L' || h->ident[3] != 'F' ||
        h->ident[4] != 2 || h->ident[5] != 1 || h->ident[6] != 1 || h->ident[7]!=0x4e || h->ident[8]!=1 || h->type != 2 ||
        h->machine != 62 || h->version != 1 || h->ehsize != 64 || h->phsize != 56 || !h->phnum ||
        h->phnum > 16 || h->phoff > bytes || (uint64_t)h->phnum * 56 > bytes - h->phoff)
        return false;
    struct elf_image result = {0};
    bool entry_ok = false;
    uint64_t pages = 0;
    for (unsigned i = 0; i < h->phnum; ++i) {
        const struct program *p = (const struct program *)(data + h->phoff + i * 56);
        if (p->type == 2 || p->type == 3 || p->type == 7)
            return false; /* No dynamic loader/TLS. */
        if (p->type == 0x6474e551 && (p->flags & 1))
            return false; /* Executable stack. */
        if (p->type != 1)
            continue;
        if (!p->memsz) {
            if (p->filesz)
                return false;
            continue;
        }
        if (p->va < 0x400000 || p->va >= 0x700000 || p->va % 4096 || p->memsz > 0x700000 - p->va ||
            p->filesz > p->memsz || p->offset > bytes || p->filesz > bytes - p->offset ||
            !(p->flags & 4) || (p->flags & ~7u) || (p->flags & 3) == 3 ||
            (p->align > 1 &&
             ((p->align & (p->align - 1)) || p->va % p->align != p->offset % p->align)))
            return false;
        uint64_t end = (p->va + p->memsz + 4095) & ~4095ull;
        for (uint64_t j = 0; j < result.count; ++j) {
            const struct elf_segment *s = &result.segments[j];
            uint64_t previous_end = (s->address + s->memsz + 4095) & ~4095ull;
            if (p->va < previous_end && s->address < end)
                return false;
        }
        pages += (end - p->va) / 4096;
        if (pages > 160)
            return false;
        if ((p->flags & 1) && h->entry >= p->va && h->entry - p->va < p->memsz)
            entry_ok = true;
        result.segments[result.count++] =
            (struct elf_segment){p->va, p->offset, p->filesz, p->memsz, p->flags};
    }
    if (!entry_ok || !result.count)
        return false;
    result.entry = h->entry;
    *out = result;
    return true;
}
