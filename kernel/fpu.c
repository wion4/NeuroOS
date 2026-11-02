#include "fpu.h"
static struct fpu_state kernel;
static uint64_t saved_cr0, saved_cr4;
void fpu_init(struct fpu_state *s) {
    for (unsigned i = 0; i < 512; ++i)
        s->bytes[i] = 0;
    s->bytes[0] = 0x7f;
    s->bytes[1] = 3; /* x87 control word 0x037f, empty register stack. */
    s->bytes[24] = 0x80;
    s->bytes[25] = 0x1f; /* MXCSR 0x1f80; all XMM registers zero. */
}
void fpu_save(struct fpu_state *s) { __asm__ volatile("fxsave64 %0" : "=m"(*s)::"memory"); }
void fpu_restore(const struct fpu_state *s) {
    __asm__ volatile("fxrstor64 %0" ::"m"(*s) : "memory");
}
void fpu_begin(void) {
    __asm__ volatile("mov %%cr0,%0;mov %%cr4,%1" : "=r"(saved_cr0), "=r"(saved_cr4));
    uint64_t cr0 = (saved_cr0 | 2) & ~12ull;
    uint64_t cr4 = (saved_cr4 | (3ull << 9)) & ~(1ull << 18); /* SSE, no unsaved AVX/XSAVE state. */
    __asm__ volatile("mov %0,%%cr0;mov %1,%%cr4" ::"r"(cr0), "r"(cr4) : "memory");
    fpu_save(&kernel);
}
void fpu_end(void) {
    fpu_restore(&kernel);
    __asm__ volatile("mov %0,%%cr4;mov %1,%%cr0" ::"r"(saved_cr4), "r"(saved_cr0) : "memory");
}
