#ifndef NEUROOS_SETJMP_H
#define NEUROOS_SETJMP_H
#include <stdint.h>
typedef struct {
    uint64_t registers[8];
} jmp_buf[1];
int setjmp(jmp_buf environment) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf environment, int value);
#endif
