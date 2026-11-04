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
#ifdef NEUROOS_HOST_ABI
long receive_wait_from(unsigned cap, void *message, size_t bytes, uint64_t *sender);
#else
static inline long receive_wait_from(unsigned cap, void *message, size_t bytes, uint64_t *sender) {
    uint64_t call = 16, source = bytes;
    __asm__ volatile("int $0x80"
                     : "+a"(call), "+d"(source)
                     : "D"((uint64_t)cap), "S"((uintptr_t)message)
                     : "memory", "cc");
    if ((long)call >= 0 && sender)
        *sender = source;
    return (long)call;
}
#endif
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
