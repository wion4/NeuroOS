/* Ordinary app: can operate only on the immutable names/rights from its owner. */
#include "../user/abi.h"
#include "../user/desktop.h"
#include "../user/file.h"
static void check(int ok) {
    if (!ok) {
        say("SCOPED APP FAIL\n");
        finish(99);
    }
}
static long request(unsigned op, const char *name, const char *data) {
    struct fs_message q = {.op = op}, r;
    for (unsigned i = 0; name[i] && i < 15; ++i)
        q.name[i] = name[i];
    for (unsigned i = 0; data && data[i] && i < 32; ++i)
        q.data[q.size++] = data[i];
    return nos_fs_request(&q, &r);
}
static void legacy_write(void) {
    check(request(FS_BEGIN, "", 0) == 0);
    struct fs_message q = {.op = FS_CREATE, .name = "sentinel.txt", .status = 1234}, r;
    check(nos_fs_request(&q, &r) == -17);
    check(r.op == FS_CREATE && !r.size && !r.offset);
    for (unsigned i = 0; i < sizeof(r.data); ++i)
        check(!r.data[i]);
    q.op = FS_TRUNCATE;
    check(nos_fs_request(&q, &r) == 0);
    q.op = FS_WRITE;
    q.size = 4;
    q.data[0] = 'o';
    q.data[1] = 'l';
    q.data[2] = 'd';
    q.data[3] = '!';
    check(nos_fs_request(&q, &r) == 0);
    q.op = FS_COMMIT;
    q.size = 0;
    check(nos_fs_request(&q, &r) == 0);
}
int user_main(const char *args) {
    check(call3(20, NOS_JOB_START_SCOPED, 0, 0) == -1 && call3(21, 0, 0, 0) == -1 &&
          call3(22, 0, 0, 0) == -1);
    check(call3(9, 3, 0x1f7, 0) == -1 && call3(11, 0, 0, 0) == -1);
    check(request(FS_CLIENT_CLOSED, "", 0) == -1 && request(FS_LIST, "", 0) == -1 &&
          request(FS_QUIT, "", 0) == -1);
    check(nos_file_size("scope.nos") == -1 && nos_file_size("bogus.nos") == -1);
    struct fs_message short_q = {.op = FS_READ};
    check(send_message(1, &short_q, sizeof(short_q) - 1) == -1);
    if (args[0] == 'r') {
        char data[4];
        check(nos_file_size("sentinel.txt") == 4 && nos_file_read("sentinel.txt", 0, data, 4) == 4);
        check(data[0] == 's' && data[1] == 'e' && data[2] == 'e' && data[3] == 'd');
        check(nos_file_write("sentinel.txt", "bad", 3) == -1);
        check(request(FS_BEGIN, "", 0) == 0);
        check(request(FS_COMMIT, "", 0) == -1);
        check(request(FS_DELETE, "sentinel.txt", 0) == -1);
        check(request(FS_CREATE, "sentinel.txt", 0) == -1);
        check(request(FS_ABORT, "", 0) == 0);
    } else if (args[0] == 'a') {
        check(nos_file_write("sentinel.txt", "bad", 3) == -1);
        say("SCOPED WRITE DENIED PASS\n");
    } else if (args[0] == 'w') {
        check(nos_file_size("sentinel.txt") == -1);
        check(nos_file_write("sentinel.txt", "edit", 4) == 4);
        check(nos_file_pwrite("sentinel.txt", 1, "x", 1) == 1);
        check(request(FS_CREATE, "sentinel.txt", 0) == -17); /* Existing, never creates. */
        check(request(FS_DELETE, "sentinel.txt", 0) == -1);
    } else if (args[0] == 'l') {
        check(nos_file_size("sentinel.txt") == -1);
        legacy_write();
    } else if (args[0] == 'n') {
        check(request(FS_CREATE, "absent.txt", 0) == -1);
        check(nos_file_write("absent.txt", "bad", 3) == -1);
        check(request(FS_WRITE, "absent.txt", "bad") == -2);
    } else if (args[0] == 'c') {
        check(request(FS_CREATE, "created.txt", 0) == 0);
        check(request(FS_WRITE, "created.txt", "bad") == -1);
        check(nos_file_size("created.txt") == -1);
        check(request(FS_DELETE, "created.txt", 0) == -1);
    } else if (args[0] == 'd') {
        check(request(FS_DELETE, "victim.txt", 0) == 0);
        check(request(FS_CREATE, "victim.txt", 0) == -1);
    } else if (args[0] == 'm') {
        check(nos_file_size("sentinel.txt") == 4);
        check(request(FS_WRITE, "sentinel.txt", "bad") == -1);
        check(nos_file_write("write.txt", "edit", 4) == 4);
        check(nos_file_size("write.txt") == -1);
        check(request(FS_CREATE, "created.txt", 0) == 0);
        check(request(FS_WRITE, "created.txt", "bad") == -1);
        check(request(FS_DELETE, "victim.txt", 0) == 0);
        check(request(FS_READ, "victim.txt", 0) == -1);
    } else if (args[0] == 'h') {
        check(request(FS_BEGIN, "", 0) == 0);
        check(request(FS_CREATE, "allowed.txt", 0) == 0);
        check(request(FS_WRITE, "allowed.txt", "private") == 0);
        check(request(FS_WRITE, "forbidden.txt", "bad") == -1);
        say("SCOPED PRIVATE DRAFT HELD\n");
        for (;;)
            (void)call3(19, 1000, 0, 0);
    } else
        check(0);
    say("SCOPED APP PASS\n");
    return 0;
}
