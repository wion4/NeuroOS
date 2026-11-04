#include "abi.h"
#include "file.h"
#include "heap.h"
#include "libc/errno.h"
#include "libc/stdio.h"
extern int test_file_descriptors(void);
static int stream_test(void) {
    FILE *f = fopen("stream.txt", "w+b");
    if (!f || fwrite("ABCDEF", 2, 3, f) != 3 || ftell(f) != 6)
        return 40;
    if (fseek(f, 2, SEEK_SET) || fwrite("xy", 1, 2, f) != 2 || fflush(f))
        return 41;
    rewind(f);
    char data[16] = {0};
    if (fread(data, 2, 4, f) != 3 || !feof(f) || ferror(f))
        return 42;
    const char expected[] = "ABxyEF";
    for (unsigned i = 0; i < 6; ++i)
        if (data[i] != expected[i])
            return 43;
    if (ungetc('!', f) != '!' || feof(f) || ftell(f) != 5 || fgetc(f) != '!')
        return 44;
    if (fseek(f, -7, SEEK_CUR) != EOF || ftell(f) != 6)
        return 45;
    clearerr(f);
    if (fseek(f, FS_FILE_LIMIT - 1, SEEK_SET) || fwrite("zz", 1, 2, f) || errno != EFBIG ||
        !ferror(f))
        return 46;
    if (fclose(f))
        return 47;
    f = fopen("stream.txt", "ab");
    if (!f || fseek(f, 0, SEEK_SET) || fputs("++", f) == EOF || ftell(f) != 8 || fclose(f))
        return 48;
    f = fopen("stream.txt", "rb");
    if (!f || ungetc('Q', f) != 'Q' || fgetc(f) != 'Q' || ftell(f) || fgetc(f) != 'A')
        return 55;
    rewind(f);
    if (!f || fwrite("X", 1, 1, f) || !ferror(f) || errno != EBADF)
        return 49;
    clearerr(f);
    if (!fgets(data, sizeof(data), f) || !feof(f) || ftell(f) != 8)
        return 50;
    if (fread(data, (size_t)-1, 2, f) || errno != EOVERFLOW)
        return 51;
    if (fclose(f) || fopen("stream.txt", "r++") || errno != EINVAL)
        return 52;
    if (fopen("absent.tmp", "rb") || errno != ENOENT)
        return 53;
    if (puts("C STREAM TEST PASS") == EOF)
        return 54;
    return 0;
}
static int heap_test(char mode) {
    volatile unsigned char *raw = (void *)0x1000000;
    if (call3(10, 0, 0, 0) != 0x1000000 || call3(10, 1, 2, 0) != 0x1002000)
        return 20;
    raw[4096] = 123;
    if (call3(10, 1, 1, 0) != 0x1001000 || call3(10, 1, 2, 0) != 0x1002000 || raw[4096])
        return 21;
    if (call3(10, 1, 513, 0) != -22 || call3(10, 0, 0, 0) != 0x1002000)
        return 22;
    if (mode == 'n') {
        raw[0] = 0xc3;
        ((void (*)(void))raw)(); /* Heap pages must remain non-executable. */
        return 23;
    }
    if (mode == 's') {
        (void)call3(10, 1, 1, 0);
        raw[4096] = 1; /* A released page must fault. */
        return 24;
    }
    if (call3(10, 1, 0, 0) != 0x1000000)
        return 25;
    unsigned char *a = malloc(9001), *b = calloc(100, 17);
    if (!a || !b || ((uintptr_t)a & 15) || ((uintptr_t)b & 15))
        return 26;
    for (unsigned i = 0; i < 9001; ++i)
        a[i] = (unsigned char)i;
    for (unsigned i = 0; i < 1700; ++i)
        if (b[i])
            return 27;
    free(b);
    unsigned char *c = realloc(a, 100000);
    if (!c)
        return 28;
    for (unsigned i = 0; i < 9001; ++i)
        if (c[i] != (unsigned char)i)
            return 29;
    if (realloc(c, (size_t)-1) || calloc((size_t)-1, 2) || c[9000] != (unsigned char)9000)
        return 30;
    free(c);
    void *large = malloc(1900000);
    if (!large || malloc(300000))
        return 31;
    /* Every physical page is writable; returning the program frees all of them. */
    for (unsigned i = 0; i < 1900000; i += 4096)
        ((volatile unsigned char *)large)[i] = (unsigned char)i;
    free(large);
    void *again = malloc(1900000);
    if (again != large)
        return 32;
    if (mode == 'd')
        (void)*(volatile uint64_t *)0xffffffff80000000ull;
    /* Deliberately leave this allocation live to test process teardown. */
    say("USER HEAP TEST PASS\n");
    return 0;
}
int user_main(const char *args) {
    volatile double a = 1.5, b = 2.0;
    if (a * b != 3.0)
        return 5;
    if (args && args[0] == 'p') {
        unsigned char malformed = 0;
        if (send_message(1, &malformed, 1))
            return 60;
        struct fs_message q = {.op = FS_QUIT}, r;
        long n;
        while ((n = receive_message(2, &r, sizeof(r))) == -11)
            yield();
        if (n != sizeof(r) || r.status != -22)
            return 61;
        if (nos_fs_request(&q, &r) != -1)
            return 62;
        say("C PROTOCOL DEFENSE PASS\n");
        return 0;
    }
    if (args && args[0] == 'b') {
        struct fs_message q = {.op = FS_STAT, .name = "durable"};
        for (unsigned i = 0; i < 8; ++i)
            if (send_message(1, &q, sizeof(q)))
                return 63;
        /* Deliberately leave requests/replies unconsumed at normal exit. */
        return 0;
    }
    if (args && args[0] == 'f')
        return test_file_descriptors();
    if (args && args[0] == 't')
        return stream_test();
    if (args && (args[0] == 'h' || args[0] == 'n' || args[0] == 's' || args[0] == 'd'))
        return heap_test(args[0]);
    if (args && args[0] == 'c') {
        struct fs_message q = {.op = FS_BEGIN}, r;
        if (nos_fs_request(&q, &r))
            return 12;
        q.op = FS_TRUNCATE;
        const char name[] = "app.txt";
        for (unsigned i = 0; i < sizeof(name); ++i)
            q.name[i] = name[i];
        if (nos_fs_request(&q, &r))
            return 13;
        (void)*(volatile uint64_t *)0xffffffff80000000ull;
        return 14;
    }
    if (args && args[0] == 'j') {
        struct fs_message q = {.op = FS_CLIENT_CLOSED}, r;
        if (nos_fs_request(&q, &r) != -1)
            return 70;
        if (call3(19, 0, 0, 0) != -22 || call3(19, 1001, 0, 0) != -22)
            return 71;
        q = (struct fs_message){.op = FS_BEGIN};
        if (nos_fs_request(&q, &r))
            return 72;
        q.op = FS_CREATE;
        const char name[] = "job-draft.tmp";
        for (unsigned i = 0; i < sizeof(name); ++i)
            q.name[i] = name[i];
        long e = nos_fs_request(&q, &r);
        if (e && e != -17)
            return 73;
        q.op = FS_TRUNCATE;
        if (nos_fs_request(&q, &r))
            return 74;
        q.op = FS_WRITE;
        q.size = 7;
        const char data[] = "private";
        for (unsigned i = 0; i < 7; ++i)
            q.data[i] = data[i];
        if (nos_fs_request(&q, &r))
            return 75;
        say("JOB PRIVATE TRANSACTION OPEN\n");
        (void)call3(19, 1000, 0, 0);
        return 0;
    }
    if (args && args[0] == 'r') {
        say("JOB IPC LOOP READY\n");
        for (unsigned i = 0; i < 100; ++i) {
            if (nos_file_size("hello.nos") < 128 || nos_file_size("missing.tmp") != -2)
                return 76;
            (void)call3(19, 1, 0, 0);
        }
        say("JOB IPC LOOP PASS\n");
        return 0;
    }
    if (args && args[0] == 'w') {
        say("JOB IO WAIT\n");
        (void)call3(19, 300, 0, 0);
    }
    const char text[] = "written by C";
    char buffer[32];
    long n = nos_file_write("app.txt", text, sizeof(text) - 1);
    if (n < 0) {
        say("FILE ACCESS DENIED OR FAILED\n");
        return 13;
    }
    n = nos_file_read("app.txt", 0, buffer, sizeof(buffer) - 1);
    if (n != sizeof(text) - 1)
        return 2;
    buffer[n] = 0;
    for (unsigned i = 0; i < sizeof(text); ++i)
        if (buffer[i] != text[i])
            return 3;
    if (call3(9, 3, 0x1f7, 0) != -1)
        return 4;
    if (call3(11, 0, (uintptr_t)buffer, 0) != -1 || call3(12, 1, 0, 0) != -1 ||
        call3(13, 0, 0, 0) != -1 || call3(14, 0, 0, 0) != -1 || call3(15, 0, 0, 0) != -1 ||
        call3(17, 3, ~0ull, 0) != -1 || call3(20, 0, 0, 0) != -1)
        return 5; /* Ordinary NOS files never inherit desktop/input rights. */
    say("C FILE LIBRARY PASS\n");
    return 0;
}
