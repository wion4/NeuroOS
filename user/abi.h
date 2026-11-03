#ifndef NEUROOS_USER_ABI_H
#define NEUROOS_USER_ABI_H
#include <stddef.h>
#include <stdint.h>
#ifdef NEUROOS_HOST_ABI
long call3(uint64_t call, uint64_t a, uint64_t b, uint64_t c);
#else
static inline long call3(uint64_t call, uint64_t a, uint64_t b, uint64_t c) {
    __asm__ volatile("int $0x80" : "+a"(call), "+d"(c) : "D"(a), "S"(b) : "memory", "cc");
    return (long)call;
}
#endif
static inline void yield(void) { (void)call3(0, 0, 0, 0); }
static inline long send_message(unsigned cap, const void *message, size_t bytes) {
    return call3(7, cap, (uintptr_t)message, bytes);
}
static inline long receive_message(unsigned cap, void *message, size_t bytes) {
    return call3(8, cap, (uintptr_t)message, bytes);
}
static inline long receive_wait(unsigned cap, void *message, size_t bytes) {
    return call3(16, cap, (uintptr_t)message, bytes);
}
static inline void say(const char *s) {
    size_t n = 0;
    while (s[n])
        ++n;
    (void)call3(6, (uintptr_t)s, n, 0);
}
static inline __attribute__((noreturn)) void finish(unsigned code) {
    (void)call3(1, code, 0, 0);
    for (;;) {
    }
}
#endif
