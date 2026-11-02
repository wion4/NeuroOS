#ifndef NEUROOS_FPU_H
#define NEUROOS_FPU_H
#include <stdint.h>
struct fpu_state {
    uint8_t bytes[512];
} __attribute__((aligned(16)));
void fpu_init(struct fpu_state *state);
void fpu_save(struct fpu_state *state);
void fpu_restore(const struct fpu_state *state);
void fpu_begin(void);
void fpu_end(void);
#endif
