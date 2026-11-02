#include "../user/fs.h"
#include "process.h"
#include "shell.h"
static uint8_t executable[FS_FILE_LIMIT];
static bool command_transaction;
static void abort_write(void) {
    struct fs_message q = {.op = FS_ABORT}, r;
    (void)process_file_request(&q, &r);
    command_transaction = false;
}
static bool same(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static bool rpc(struct fs_message *q, struct fs_message *r) {
    if (!process_file_request(q, r)) {
        if (command_transaction)
            abort_write();
        console_write("ERROR: filesystem service unavailable\n");
        return false;
    }
    if (r->status) {
        if (command_transaction)
            abort_write();
        console_write("ERROR: filesystem code ");
        console_number((uint64_t)(-(int64_t)r->status));
        console_write("\n");
        return false;
    }
    return true;
}
bool filesystem_command(const char *command, char *args) {
    bool file_access=same(command,"runfs");
    bool run = same(command, "run")||file_access, install = same(command, "install");
    bool list = same(command, "ls"), write = same(command, "write"), cat = same(command, "cat"),
         create = same(command, "touch"), remove = same(command, "rm"),
         stat = same(command, "stat");
    if (!list && !write && !cat && !create && !remove && !stat && !run && !install)
        return false;
    struct fs_message q = {0}, r;
    char *name = args;
    while (*args && *args != ' ')
        ++args;
    unsigned length = (unsigned)(args - name);
    if (*args)
        *args++ = 0;
    while (*args == ' ')
        ++args;
    if ((list && length) || (!list && (!length || length > 15)) || (!write && !run && *args)) {
        console_write("ERROR: invalid file arguments\n");
        return true;
    }
    for (unsigned i = 0; i < length && i < 16; ++i)
        q.name[i] = name[i];
    if (run || install) {
        if (length < 5 || !same(name + length - 4, ".nos")) {
            console_write("ERROR: executable name must end in .nos\n");
            return true;
        }
        if (install) {
            (void)kernel_install_program(name);
            return true;
        }
        q.op = FS_STAT;
        if (!rpc(&q, &r))
            return true;
        unsigned bytes = r.size;
        if (!bytes || bytes > sizeof(executable)) {
            console_write("ERROR: executable exceeds current file limit\n");
            return true;
        }
        q.op = FS_READ;
        for (q.offset = 0; q.offset < bytes;) {
            q.size = bytes - q.offset;
            if (q.size > 32)
                q.size = 32;
            if (!rpc(&q, &r) || r.size != q.size)
                return true;
            for (unsigned i = 0; i < r.size; ++i)
                executable[q.offset + i] = (uint8_t)r.data[i];
            q.offset += r.size;
        }
        (void)process_run_file(executable, bytes, args,file_access);
        return true;
    }
    if (list) {
        q.op = FS_LIST;
        for (unsigned count = 0; count < FS_FILE_COUNT; ++count) {
            if (!process_file_request(&q, &r)) {
                console_write("ERROR: filesystem service unavailable\n");
                break;
            }
            if (r.status == -2)
                break;
            if (r.status) {
                console_write("ERROR: directory response\n");
                break;
            }
            for (unsigned i = 0; i < 15 && r.name[i]; ++i)
                console_char(r.name[i]);
            console_write(" ");
            console_number(r.size);
            console_write(" BYTES\n");
            if (r.offset <= q.offset || r.offset > FS_FILE_COUNT)
                break;
            q.offset = r.offset;
        }
    } else if (write) {
        q.op = FS_BEGIN;
        if (!rpc(&q, &r))
            return true;
        command_transaction = true;
        q.op = FS_CREATE;
        if (!process_file_request(&q, &r) || (r.status && r.status != -17)) {
            abort_write();
            console_write("ERROR: cannot create file\n");
            return true;
        }
        q.op = FS_TRUNCATE;
        if (!rpc(&q, &r))
            return true;
        q.op = FS_WRITE;
        q.offset = 0;
        while (*args) {
            q.size = 0;
            while (*args && q.size < 32)
                q.data[q.size++] = *args++;
            if (!rpc(&q, &r))
                return true;
            q.offset += q.size;
        }
        unsigned written = q.offset;
        q.op = FS_COMMIT;
        q.size = 0;
        if (!rpc(&q, &r))
            return true;
        command_transaction = false;
        console_write("WROTE ");
        console_number(written);
        console_write(" BYTES\n");
    } else if (cat) {
        q.op = FS_READ;
        q.size = 32;
        for (q.offset = 0; q.offset < FS_FILE_LIMIT; q.offset += 32) {
            if (!rpc(&q, &r))
                return true;
            if (r.size > 32) {
                console_write("ERROR: invalid read response\n");
                break;
            }
            for (unsigned i = 0; i < r.size; ++i)
                if ((r.data[i] >= 32 && r.data[i] <= 126) || r.data[i] == '\n')
                    console_char(r.data[i]);
            if (r.size < 32)
                break;
        }
        console_write("\n");
    } else {
        q.op = create ? FS_CREATE : remove ? FS_DELETE : FS_STAT;
        if (rpc(&q, &r)) {
            if (stat) {
                console_number(r.size);
                console_write(" BYTES\n");
            } else
                console_write("OK\n");
        }
    }
    return true;
}
bool filesystem_store(const char *name, const void *data, unsigned bytes) {
    unsigned length = 0;
    while (name[length])
        ++length;
    if (!length || length > 15 || bytes > FS_FILE_LIMIT)
        return false;
    struct fs_message q = {.op = FS_BEGIN}, r;
    for (unsigned i = 0; i < length; ++i)
        q.name[i] = name[i];
    if (!rpc(&q, &r))
        return false;
    command_transaction = true;
    q.op = FS_CREATE;
    if (!process_file_request(&q, &r) || (r.status && r.status != -17)) {
        abort_write();
        return false;
    }
    q.op = FS_TRUNCATE;
    if (!rpc(&q, &r))
        return false;
    q.op = FS_WRITE;
    while (q.offset < bytes) {
        q.size = bytes - q.offset;
        if (q.size > 32)
            q.size = 32;
        for (unsigned i = 0; i < q.size; ++i)
            q.data[i] = ((const char *)data)[q.offset + i];
        if (!rpc(&q, &r))
            return false;
        q.offset += q.size;
    }
    q.op = FS_COMMIT;
    q.size = 0;
    if (!rpc(&q, &r))
        return false;
    command_transaction = false;
    return true;
}
