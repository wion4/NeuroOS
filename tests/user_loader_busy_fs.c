/* Test wrapper: keep an unrelated successful STAT in flight across exec.
   Filesystem storage/transactions remain the production FS3 implementation. */
#include "../user/abi.h"
#include "../user/fs3.h"
static void delayed_handle(struct fs3 *f, uint64_t client, const struct fs_message *q,
                           struct fs_message *r) {
    fs3_handle_client(f, client, q, r);
    static const char name[16] = "sentinel.txt";
    unsigned match = 1;
    for (unsigned i = 0; i < sizeof(name); ++i)
        if (name[i] != q->name[i])
            match = 0;
    if (q->op == FS_STAT && !r->status && match) {
        say("FS TEST UNRELATED REQUEST BUSY\n");
        (void)call3(19, 100, 0, 0);
        say("FS TEST UNRELATED REQUEST DONE\n");
    }
}
#define fs3_handle_client delayed_handle
#include "../user/ramfs3.c"
