/* Substitute only the private boot module in an isolated guest. Exercise the
   real kernel role boundary, then run the actual production loader. */
#include "../user/abi.h"
#include "../user/fs.h"
#include "../user/loader.h"
static void check(int ok) {
    if (!ok) {
        say("LOADER PRIVILEGE TEST FAIL\n");
        finish(99);
    }
}
static long probe_call(uint64_t call, uint64_t a, uint64_t b, uint64_t c) {
    if (call == 22 && a == NOS_LOADER_COMMIT) {
#if LOADER_DELAY_COMMIT
        (void)call3(19, 10, 0, 0); /* Only the concurrency test, never production. */
#endif
        check(call3(22, 99, 0, 0) == -22);
        check(call3(22, NOS_LOADER_FAIL, 0, 0) == -22);
        check(call3(22, NOS_LOADER_FAIL, (uint64_t)-5, 1) == -22);
        check(call3(22, NOS_LOADER_COMMIT, b, FS_FILE_LIMIT + 1) == -22);
        check(call3(22, NOS_LOADER_COMMIT, 0xffffffff80000000ull, 64) == -14);
        check(call3(22, NOS_LOADER_COMMIT, 0x80fff0, 64) == -14);
        static const uint8_t invalid[64];
        check(call3(22, NOS_LOADER_COMMIT, (uintptr_t)invalid, sizeof(invalid)) == -8);
        say("LOADER PRIVILEGE TEST PASS\n");
    }
    return call3(call, a, b, c);
}
#define call3 probe_call
#define user_main production_loader_main
#include "../user/loader.c"
#undef user_main
#undef call3
int user_main(const struct nos_loader_request *boot) {
    check(call3(20, 0, 0, 0) == -1);
    check(call3(21, 0, 0, 0) == -1);
    check(call3(11, 0, 0, 0) == -1);
    check(call3(9, 3, 0x1f7, 0) == -1);
    check(call3(22, NOS_LOADER_COMMIT, 0, 64) == -16); /* No released snapshot. */
    struct fs_message q = {.op = FS_WRITE}, r;
    check(send_message(1, &q, sizeof(q)) == -1);
    q.op = FS_COMMIT;
    check(send_message(1, &q, sizeof(q)) == -1);
    q.op = FS_CLIENT_CLOSED;
    check(send_message(1, &q, sizeof(q)) == -1);
    q.op = FS_READ;
    q.name[0] = '?';
    check(send_message(1, &q, sizeof(q)) == -1);
    check(receive_message(2, &r, sizeof(r)) == -11);
    return production_loader_main(boot);
}
