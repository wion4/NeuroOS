#include "file.h"
#include "abi.h"
long nos_fs_request(struct fs_message *q, struct fs_message *r) {
    long n;
    while ((n = send_message(1, q, sizeof(*q))) == -11)
        yield();
    if (n < 0)
        return n;
    while ((n = receive_message(2, r, sizeof(*r))) == -11)
        yield();
    return n == sizeof(*r) ? r->status : n < 0 ? n : -5;
}
const char *nos_file_path(const char *path) {
    if(!path)return 0;
    while(path[0]=='/' || (path[0]=='.' && path[1]=='/')) {
        if(path[0]=='/')++path;else path+=2;
    }
    return path;
}
static bool name_copy(struct fs_message *q, const char *name) {
    name=nos_file_path(name);if(!name)return false;
    unsigned n = 0;
    while (name[n] && n < 15) {
        q->name[n] = name[n];
        ++n;
    }
    return n && !name[n];
}
long nos_file_read(const char *name, unsigned offset, void *data, unsigned bytes) {
    struct fs_message q = {.op = FS_READ, .offset = offset}, r;
    if (!name_copy(&q, name))
        return -22;
    unsigned done = 0;
    while (done < bytes) {
        q.size = bytes - done;
        if (q.size > 32)
            q.size = 32;
        long e = nos_fs_request(&q, &r);
        if (e)
            return e;
        if (r.size > q.size)
            return -5;
        for (unsigned i = 0; i < r.size; ++i)
            ((char *)data)[done + i] = r.data[i];
        done += r.size;
        q.offset += r.size;
        if (r.size < q.size)
            break;
    }
    return done;
}
long nos_file_size(const char *name) {
    struct fs_message q = {.op = FS_STAT}, r;
    if (!name_copy(&q, name))
        return -22;
    long e = nos_fs_request(&q, &r);
    return e ? e : (long)r.size;
}
static long update(const char *name, unsigned offset, const void *data, unsigned bytes,
                   bool replace) {
    struct fs_message q = {.op = FS_BEGIN}, r;
    if (!name_copy(&q, name))
        return -22;
    if (offset > FS_FILE_LIMIT || bytes > FS_FILE_LIMIT - offset)
        return -27;
    long e = nos_fs_request(&q, &r);
    if (e)
        return e;
    q.op = FS_CREATE;
    e = nos_fs_request(&q, &r);
    if (e && e != -17)
        goto abort;
    if (replace) {
        q.op = FS_TRUNCATE;
        e = nos_fs_request(&q, &r);
        if (e)
            goto abort;
    }
    q.op = FS_WRITE;
    unsigned done = 0;
    while (done < bytes) {
        q.offset = offset + done;
        q.size = bytes - done;
        if (q.size > 32)
            q.size = 32;
        for (unsigned i = 0; i < q.size; ++i)
            q.data[i] = ((const char *)data)[done + i];
        e = nos_fs_request(&q, &r);
        if (e)
            goto abort;
        done += q.size;
    }
    q.op = FS_COMMIT;
    q.size = 0;
    e = nos_fs_request(&q, &r);
    if (e)
        goto abort;
    return bytes;
abort:
    q.op = FS_ABORT;
    q.size = 0;
    (void)nos_fs_request(&q, &r);
    return e;
}
long nos_file_write(const char *name, const void *data, unsigned bytes) {
    return update(name, 0, data, bytes, true);
}
long nos_file_pwrite(const char *name, unsigned offset, const void *data, unsigned bytes) {
    return update(name, offset, data, bytes, false);
}
