#include "abi.h"
#include "file.h"
#include "fs3.h"
static struct fs_message query(unsigned op, const char *name) {
    struct fs_message q = {.op = op};
    unsigned i = 0;
    while (name[i] && i < 15) {
        q.name[i] = name[i];
        ++i;
    }
    return q;
}
int user_main(const char *args) {
    struct fs_message q, r;
    bool read_only = args && args[0] == 'r';
    if (!read_only) {
        q = query(FS_BEGIN, "big.bin");
        if (nos_fs_request(&q, &r))
            return 1;
        q.op = FS_CREATE;
        long e = nos_fs_request(&q, &r);
        if (e && e != -17)
            return 2;
        q.op = FS_TRUNCATE;
        if (nos_fs_request(&q, &r))
            return 3;
        q.op = FS_WRITE;
        q.offset = FS3_FILE_LIMIT - 32;
        q.size = 32;
        for (unsigned i = 0; i < 32; ++i)
            q.data[i] = (char)(i + 32);
        if (nos_fs_request(&q, &r))
            return 4;
        q.op = FS_COMMIT;
        q.size = 0;
        if (nos_fs_request(&q, &r))
            return 5;
        for (unsigned i = 0; i < 40; ++i) {
            char name[] = "g00";
            name[1] = (char)('0' + i / 10);
            name[2] = (char)('0' + i % 10);
            q = query(FS_CREATE, name);
            e = nos_fs_request(&q, &r);
            if (e && e != -17)
                return 6;
        }
    }
    q = query(FS_STAT, "big.bin");
    if (nos_fs_request(&q, &r) || r.size != FS3_FILE_LIMIT)
        return 7;
    q.op = FS_READ;
    q.offset = FS3_FILE_LIMIT - 32;
    q.size = 32;
    if (nos_fs_request(&q, &r) || r.size != 32)
        return 8;
    for (unsigned i = 0; i < 32; ++i)
        if ((uint8_t)r.data[i] != i + 32)
            return 9;
    q.offset = 0;
    if (nos_fs_request(&q, &r) || r.size != 32)
        return 10;
    for (unsigned i = 0; i < 32; ++i)
        if (r.data[i])
            return 11;
    unsigned count = 0;
    q = query(FS_LIST, "");
    for (;;) {
        long e = nos_fs_request(&q, &r);
        if (e == -2)
            break;
        if (e || ++count > FS3_FILES || r.offset <= q.offset)
            return 12;
        q.offset = r.offset;
    }
    if (count < 41)
        return 13;
    if (call3(9, 3, 0x1f7, 0) != -1)
        return 14;
    say(read_only ? "FS3 COLD READ PASS\n" : "FS3 LARGE FILE AND DIRECTORY PASS\n");
    return 0;
}
