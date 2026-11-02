#ifndef NEUROOS_ELF_H
#define NEUROOS_ELF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ELF_MAX_SEGMENTS 16
struct elf_segment {
    uint64_t address, offset, filesz, memsz, flags;
};
struct elf_image {
    uint64_t entry, count;
    struct elf_segment segments[ELF_MAX_SEGMENTS];
};
/* Restricted static ELF64 ABI: addresses 4..7 MiB, page-aligned PT_LOAD, W^X. */
bool elf_validate(const void *file, size_t bytes, struct elf_image *out);
#endif
