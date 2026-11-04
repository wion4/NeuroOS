#include "loader.h"
#include "abi.h"
#include "fs.h"
#include "heap.h"

static long rpc(struct fs_message *q, struct fs_message *r) {
    long n;
    while ((n = send_message(1, q, sizeof(*q))) == -11)
        yield();
    if (n < 0)
        return n;
    while ((n = receive_wait(2, r, sizeof(*r))) == -11) {
    }
    return n == sizeof(*r) ? r->status : n < 0 ? n : -5;
}
static void fail(long error) {
    (void)call3(22, NOS_LOADER_FAIL, (uint64_t)error, 0);
    finish(1); /* The private completion syscall normally never returns. */
}
int user_main(const struct nos_loader_request *boot) {
    if (!boot)
        return 1;
    struct fs_message q = {.op = FS_BEGIN}, r;
    long e = rpc(&q, &r);
    if (e)
        fail(e);
    for (unsigned i = 0; i < sizeof(q.name); ++i)
        q.name[i] = boot->name[i];
    q.op = FS_STAT;
    if ((e = rpc(&q, &r)))
        fail(e);
    unsigned bytes = r.size;
    if (!bytes || bytes > FS_FILE_LIMIT)
        fail(-27);
    uint8_t *image = malloc(bytes);
    if (!image)
        fail(-12);
    q.op = FS_READ;
    while (q.offset < bytes) {
        q.size = bytes - q.offset;
        if (q.size > sizeof(q.data))
            q.size = sizeof(q.data);
        if ((e = rpc(&q, &r)))
            fail(e);
        if (r.size != q.size)
            fail(-5);
        for (unsigned i = 0; i < r.size; ++i)
            image[q.offset + i] = (uint8_t)r.data[i];
        q.offset += r.size;
    }
    q.op = FS_ABORT; /* Release the read snapshot before replacing this task. */
    q.size = 0;
    if ((e = rpc(&q, &r)))
        fail(e);
    fail(call3(22, NOS_LOADER_COMMIT, (uintptr_t)image, bytes));
    return 1;
}
