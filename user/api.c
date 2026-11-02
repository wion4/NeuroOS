#include "abi.h"
long nos_write(const void *buffer, size_t bytes) {
    size_t done = 0;
    while (done < bytes) {
        size_t n = bytes - done > 128 ? 128 : bytes - done;
        long result = call3(6, (uintptr_t)((const char *)buffer + done), n, 0);
        if (result < 0)
            return done ? (long)done : result;
        done += (size_t)result;
    }
    return (long)done;
}
void nos_yield(void) { yield(); }
uint64_t nos_ticks(void) { return (uint64_t)call3(5, 0, 0, 0); }
_Noreturn void nos_exit(int status) { finish((unsigned)status); }
