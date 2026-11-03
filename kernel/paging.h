#ifndef NEUROOS_PAGING_H
#define NEUROOS_PAGING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define VM_TEST_ADDRESS 0xffffc00000000000ull
bool paging_init(uint64_t hhdm);
bool paging_selftest(void);
#define USER_CODE 0x400000ull
#define USER_DATA 0x600000ull
#define USER_STACK 0x800000ull
#define USER_ELF_STACK_PAGES 16u
#define USER_ELF_STACK_TOP (USER_STACK + USER_ELF_STACK_PAGES * 4096ull)
#define USER_HEAP 0x1000000ull
#define USER_HEAP_PAGES 512u
struct user_space {
    uint64_t root, owned[192];
    unsigned count, heap_pages;
};
bool user_heap_resize(struct user_space *space, unsigned pages);
bool user_space_create(struct user_space *space, const void *code, size_t bytes, uint64_t seed);
void user_space_destroy(struct user_space *space);
bool user_copy_from(const struct user_space *space, void *dst, uint64_t src, size_t bytes);
bool user_copy_to(const struct user_space *space, uint64_t dst, const void *src, size_t bytes);
bool user_check_write(const struct user_space *space, uint64_t dst, size_t bytes);
bool user_space_elf(struct user_space *space, const void *file, size_t bytes, uint64_t *entry);
void paging_switch(uint64_t address_space);
uint64_t paging_kernel_root(void);
#endif
