/* Test boot loader: leave a read snapshot open, then fail or never finish. */
#include "../user/abi.h"
#include "../user/fs.h"
int user_main(void) {
    struct fs_message q = {.op = FS_BEGIN}, r;
    if (send_message(1, &q, sizeof(q)))
        return 1;
    long n;
    while ((n = receive_wait(2, &r, sizeof(r))) == -11) {
    }
    if (n != sizeof(r) || r.status)
        return 2;
    say("LOADER TEST SNAPSHOT OPEN\n");
#if LOADER_STALL
    for (;;)
        (void)call3(19, 1000, 0, 0);
#else
    *(volatile unsigned *)0 = 1;
#endif
    return 3;
}
