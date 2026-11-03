#include "abi.h"
#include "fs.h"
static int rpc(struct fs_message *q, struct fs_message *r) {
    long n;
    while ((n = send_message(1, q, sizeof(*q))) == -11)
        yield();
    if (n < 0)
        return -1;
    while ((n = receive_wait(2, r, sizeof(*r))) == -11)
        yield();
    return n == sizeof(*r) ? 0 : -1;
}
static struct fs_message request(unsigned op) {
    struct fs_message q = {.op = op};
    const char name[] = "demo.txt";
    for (unsigned i = 0; i < sizeof(name); ++i)
        q.name[i] = name[i];
    return q;
}
int user_main(struct fs_message *boot) {
    if (boot) {
        struct fs_message response;
        if (rpc(boot, &response))
            return 20;
        *boot = response;
        return 0;
    }
    struct fs_message q = request(FS_CREATE), r;
    if (send_message(2, &q, sizeof(q)) != -1)
        return 1; /* Receive-only cap. */
    if (send_message(1, (void *)0xffffffff80000000ull, 64) != -14)
        return 2;
    if (send_message(1, &q, sizeof(q)))
        return 3;
    long n;
    while ((n = receive_message(2, (void *)0x400000, 64)) == -11)
        yield();
    if (n != -14)
        return 21; /* Read-only code must not be overwritten. */
    volatile uint64_t *edge = (volatile uint64_t *)0x80fff0; /* End of the 64 KiB ELF stack. */
    *edge = 0xabcdef1234567890ull;
    if (receive_wait(2, (void *)edge, 64) != -14 || *edge != 0xabcdef1234567890ull)
        return 22; /* Cross-page rejection must leave the valid prefix unchanged. */
    if (receive_wait(2, &r, 32) != -90)
        return 23;
    if (receive_wait(2, &r, sizeof(r)) != sizeof(r) || r.status)
        return 24;
    if (rpc(&q, &r) || r.status != -17)
        return 4;
    q = request(FS_WRITE);
    q.size = 13;
    const char text[] = "hello neuroos";
    for (unsigned i = 0; i < 13; ++i)
        q.data[i] = text[i];
    if (rpc(&q, &r) || r.status || r.size != 13)
        return 5;
    q = request(FS_READ);
    q.size = 32;
    if (rpc(&q, &r) || r.status || r.size != 13)
        return 6;
    for (unsigned i = 0; i < 13; ++i)
        if (r.data[i] != text[i])
            return 7;
    say("RAMFS READ: hello neuroos\n");
    q=request(FS_BEGIN);if(rpc(&q,&r)||r.status)return 25;
    if(rpc(&q,&r)||r.status!=-16)return 26;
    q=request(FS_WRITE);q.size=3;q.data[0]='b';q.data[1]='a';q.data[2]='d';
    if(rpc(&q,&r)||r.status)return 27;
    q=request(FS_ABORT);if(rpc(&q,&r)||r.status)return 28;
    q=request(FS_READ);q.size=32;if(rpc(&q,&r)||r.status||r.size!=13)return 29;
    for(unsigned i=0;i<13;++i)if(r.data[i]!=text[i])return 30;
    q=request(FS_COMMIT);if(rpc(&q,&r)||r.status!=-22)return 31;
    q = request(FS_STAT);
    if (rpc(&q, &r) || r.status || r.size != 13)
        return 8;
    q = request(FS_LIST);
    if (rpc(&q, &r) || r.status || r.name[0] != 'd')
        return 9;
    q = request(FS_WRITE);
    q.offset = FS_FILE_LIMIT-4;
    q.size = 10;
    if (rpc(&q, &r) || r.status != -27)
        return 10;
    q = request(FS_DELETE);
    if (rpc(&q, &r) || r.status)
        return 11;
    q = request(FS_READ);
    q.size = 32;
    if (rpc(&q, &r) || r.status != -2)
        return 12;
    q = request(FS_QUIT);
    if (rpc(&q, &r) || r.status)
        return 13;
    say("RAMFS CLIENT PASS\n");
    return 0;
}
