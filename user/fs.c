#include "fs.h"
#include <stdbool.h>
struct file {
    char name[16];
    char data[FS_LEGACY_FILE_LIMIT];
    uint32_t length;
    bool used;
};
static struct file files[8];
static void file_clear(struct file *file) {
    uint8_t *p = (uint8_t *)file;
    for (unsigned i = 0; i < sizeof(*file); ++i)
        p[i] = 0;
}
void fs_reset(void) {
    for (unsigned i = 0; i < 8; ++i)
        file_clear(&files[i]);
}
static bool same(const char *a, const char *b) {
    for (unsigned i = 0; i < 16; ++i) {
        if (a[i] != b[i])
            return false;
        if (!a[i])
            return true;
    }
    return false;
}
static bool valid_name(const char *name) {
    if (!name[0] || name[0] == '.')
        return false;
    for (unsigned i = 0; i < 16; ++i) {
        char c = name[i];
        if (!c)
            return true;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return false;
    }
    return false;
}
bool fs_export(uint8_t *out, size_t bytes) {
    if (!out || bytes != FS_SNAPSHOT_SIZE)
        return false;
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t *p = out + i * FS_RECORD_SIZE;
        for (unsigned j = 0; j < FS_RECORD_SIZE; ++j)
            p[j] = 0;
        p[16] = files[i].used ? 1 : 0;
        if (!files[i].used)
            continue;
        for (unsigned j = 0; j < 16; ++j)
            p[j] = (uint8_t)files[i].name[j];
        for (unsigned j = 0; j < 4; ++j)
            p[20 + j] = (uint8_t)(files[i].length >> (8 * j));
        for (unsigned j = 0; j < files[i].length; ++j)
            p[24 + j] = (uint8_t)files[i].data[j];
    }
    return true;
}
bool fs_import(const uint8_t *in, size_t bytes) {
    if (!in || (bytes != FS_SNAPSHOT_SIZE && bytes != 8384))
        return false;
    unsigned record = bytes == 8384 ? 1048 : FS_RECORD_SIZE;
    unsigned limit = bytes == 8384 ? 1024 : FS_LEGACY_FILE_LIMIT;
    /* Validate before replacing live files. */
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *p = in + i * record;
        uint32_t length = 0;
        for (unsigned j = 0; j < 4; ++j)
            length |= (uint32_t)p[20 + j] << (8 * j);
        if (p[16] > 1 || p[17] || p[18] || p[19] || length > limit)
            return false;
        if (p[16]) {
            if (!valid_name((const char *)p))
                return false;
            for (unsigned k = 0; k < i; ++k)
                if (in[k * record + 16] && same((const char *)p, (const char *)(in + k * record)))
                    return false;
        }
    }
    fs_reset();
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *p = in + i * record;
        if (!p[16])
            continue;
        files[i].used = true;
        for (unsigned j = 0; j < 16; ++j)
            files[i].name[j] = (char)p[j];
        for (unsigned j = 0; j < 4; ++j)
            files[i].length |= (uint32_t)p[20 + j] << (8 * j);
        for (unsigned j = 0; j < files[i].length; ++j)
            files[i].data[j] = (char)p[24 + j];
    }
    return true;
}
void fs_handle(const struct fs_message *q, struct fs_message *r) {
    *r = (struct fs_message){.op = q->op, .status = -22};
    if (q->size > 32 || q->op < FS_CREATE || q->op > FS_TRUNCATE)
        return;
    if (q->op == FS_QUIT) {
        r->status = 0;
        return;
    }
    if (q->op == FS_LIST) {
        for (unsigned i = q->offset; i < 8; ++i)
            if (files[i].used) {
                for (unsigned j = 0; j < 16; ++j)
                    r->name[j] = files[i].name[j];
                r->offset = i + 1;
                r->size = files[i].length;
                r->status = 0;
                return;
            }
        r->status = -2;
        return;
    }
    if (!valid_name(q->name))
        return;
    unsigned found = 8, empty = 8;
    for (unsigned i = 0; i < 8; ++i) {
        if (!files[i].used) {
            if (empty == 8)
                empty = i;
        } else if (same(q->name, files[i].name))
            found = i;
    }
    if (q->op == FS_CREATE) {
        if (found < 8) {
            r->status = -17;
            return;
        }
        if (empty == 8) {
            r->status = -28;
            return;
        }
        file_clear(&files[empty]);
        files[empty].used = true;
        for (unsigned j = 0; j < 16; ++j)
            files[empty].name[j] = q->name[j];
        r->status = 0;
        return;
    }
    if (found == 8) {
        r->status = -2;
        return;
    }
    struct file *f = &files[found];
    if (q->op == FS_WRITE) {
        if (q->offset > FS_LEGACY_FILE_LIMIT || q->size > FS_LEGACY_FILE_LIMIT - q->offset) {
            r->status = -27;
            return;
        }
        if (q->size) {
            for (unsigned i = f->length; i < q->offset; ++i)
                f->data[i] = 0;
            for (unsigned i = 0; i < q->size; ++i)
                f->data[q->offset + i] = q->data[i];
            if (q->offset + q->size > f->length)
                f->length = q->offset + q->size;
        }
        r->size = q->size;
        r->status = 0;
    } else if (q->op == FS_READ) {
        uint32_t n = q->offset < f->length ? f->length - q->offset : 0;
        if (n > q->size)
            n = q->size;
        for (unsigned i = 0; i < n; ++i)
            r->data[i] = f->data[q->offset + i];
        r->size = n;
        r->status = 0;
    } else if (q->op == FS_STAT) {
        r->size = f->length;
        r->status = 0;
    } else if (q->op == FS_TRUNCATE) {
        f->length = 0;
        for (unsigned i = 0; i < FS_LEGACY_FILE_LIMIT; ++i)
            f->data[i] = 0;
        r->status = 0;
    } else if (q->op == FS_DELETE) {
        file_clear(f);
        r->status = 0;
    }
}
