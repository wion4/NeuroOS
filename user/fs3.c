/* Sector-backed COW backend. All IO is supplied by the userspace server. */
#include "fs3.h"
#include <limits.h>
#define MAGIC 0x3353464f5255454eull
#define MARKER 0x3354494d4d4f43ull
#define RECORD_OFFSET 64u
static uint32_t get32(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint64_t get64(const uint8_t *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}
static void zero(void *data, size_t bytes) {
    uint8_t *p = data;
    while (bytes--)
        *p++ = 0;
}
static void copy(void *dst, const void *src, size_t bytes) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (bytes--)
        *d++ = *s++;
}
static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t bytes) {
    while (bytes--) {
        c ^= *p++;
        for (unsigned i = 0; i < 8; ++i)
            c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return c;
}
static uint32_t root_crc(const uint8_t *root) {
    uint32_t c = crc_update(~0u, root, 32);
    const uint8_t empty[4] = {0};
    c = crc_update(c, empty, 4);
    return ~crc_update(c, root + 36, FS3_ROOT_BYTES - 36);
}
static uint8_t *record(uint8_t *root, unsigned i) { return root + RECORD_OFFSET + i * 32; }
static const uint8_t *const_record(const uint8_t *root, unsigned i) {
    return root + RECORD_OFFSET + i * 32;
}
static bool same_name(const uint8_t *a, const char *b) {
    for (unsigned i = 0; i < 16; ++i) {
        if (a[i] != (uint8_t)b[i])
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
static bool root_valid(const uint8_t *root) {
    if (get64(root) != MAGIC || !get64(root + 8) || get32(root + 16) != FS3_DISK_BLOCKS ||
        get32(root + 20) != FS3_FILES || get32(root + 24) != FS3_FILE_LIMIT ||
        get32(root + 28) != FS3_BLOCK || get32(root + 36) != 3 ||
        get32(root + 32) != root_crc(root))
        return false;
    for (unsigned i = 40; i < RECORD_OFFSET; ++i)
        if (root[i])
            return false;
    for (unsigned i = RECORD_OFFSET + FS3_FILES * 32; i < FS3_ROOT_BYTES; ++i)
        if (root[i])
            return false;
    for (unsigned i = 0; i < FS3_FILES; ++i) {
        const uint8_t *r = const_record(root, i);
        if (!r[0]) {
            for (unsigned j = 0; j < 32; ++j)
                if (r[j])
                    return false;
            continue;
        }
        if (!valid_name((const char *)r))
            return false;
        uint32_t size = get32(r + 16), start = get32(r + 20), blocks = get32(r + 24);
        if (size > FS3_FILE_LIMIT)
            return false;
        if (!size) {
            if (start || blocks || get32(r + 28))
                return false;
        } else if (!blocks || blocks > FS3_FILE_LIMIT / FS3_BLOCK || (blocks & (blocks - 1)) ||
                   start < FS3_FIRST_DATA || start >= FS3_DISK_BLOCKS ||
                   blocks > FS3_DISK_BLOCKS - start || size > blocks * FS3_BLOCK)
            return false;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *other = const_record(root, j);
            if (!other[0])
                continue;
            if (same_name(other, (const char *)r))
                return false;
            uint32_t first = get32(other + 20), length = get32(other + 24);
            if (blocks && length && start < first + length && first < start + blocks)
                return false;
        }
    }
    return true;
}
static void blank_root(uint8_t *root) {
    zero(root, FS3_ROOT_BYTES);
    put64(root, MAGIC);
    put32(root + 16, FS3_DISK_BLOCKS);
    put32(root + 20, FS3_FILES);
    put32(root + 24, FS3_FILE_LIMIT);
    put32(root + 28, FS3_BLOCK);
    put32(root + 36, 3);
}
static bool read_sectors(struct fs3 *f, uint32_t sector, uint8_t *data, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (!f->io.read(f->io.context, sector + i, data + i * 512)) {
            f->poisoned = true;
            return false;
        }
    return true;
}
static bool write_sectors(struct fs3 *f, uint32_t sector, const uint8_t *data, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (!f->io.write(f->io.context, sector + i, data + i * 512)) {
            f->poisoned = true;
            return false;
        }
    return true;
}
static bool barrier(struct fs3 *f) {
    if (f->io.flush(f->io.context))
        return true;
    f->poisoned = true;
    return false;
}
int fs3_mount(struct fs3 *f, const struct fs3_io *io) {
    if (!f || !io || !io->read || !io->write || !io->flush)
        return -22;
    zero(f, sizeof(*f));
    f->io = *io;
    f->active = -1;
    f->cache_block = UINT32_MAX;
    for (unsigned bank = 0; bank < 2; ++bank) {
        if (!read_sectors(f, (128 + bank * 2) * 8, f->roots[bank], 16))
            return -5;
        f->valid[bank] = root_valid(f->roots[bank]);
        if (f->valid[bank] &&
            (f->active < 0 || get64(f->roots[bank] + 8) > get64(f->roots[f->active] + 8)))
            f->active = (int)bank;
    }
    if (f->valid[0] && f->valid[1] && get64(f->roots[0] + 8) == get64(f->roots[1] + 8)) {
        for (unsigned i = 0; i < FS3_ROOT_BYTES; ++i)
            if (f->roots[0][i] != f->roots[1][i]) {
                f->poisoned = true;
                return -5;
            }
    }
    if (!read_sectors(f, 1023, f->scratch, 1))
        return -5;
    for (unsigned i = 0; i < 512; ++i)
        if (f->scratch[i])
            f->marked = true;
    if (f->active >= 0) {
        copy(f->draft, f->roots[f->active], FS3_ROOT_BYTES);
        return 1;
    }
    if (f->marked) {
        f->poisoned = true;
        return -5;
    }
    blank_root(f->draft);
    return 0; /* Caller must validate migration/new-device policy before writing. */
}
static bool cache_flush(struct fs3 *f) {
    if (f->cache_dirty && !write_sectors(f, f->cache_block * 8, f->cache, 8))
        return false;
    f->cache_dirty = false;
    return true;
}
static uint8_t *cache_get(struct fs3 *f, uint32_t block) {
    if (f->cache_block == block)
        return f->cache;
    if (!cache_flush(f) || !read_sectors(f, block * 8, f->cache, 8))
        return 0;
    f->cache_block = block;
    return f->cache;
}
static void mark_extent(struct fs3 *f, uint32_t start, uint32_t blocks) {
    for (uint32_t i = start; i < start + blocks; ++i)
        f->occupied[i / 8] |= (uint8_t)(1u << (i % 8));
}
static uint32_t allocate(struct fs3 *f, unsigned blocks) {
    zero(f->occupied, sizeof(f->occupied));
    mark_extent(f, 0, FS3_FIRST_DATA);
    for (unsigned source = 0; source < 3; ++source) {
        if (source < 2 && !f->valid[source])
            continue;
        const uint8_t *root = source < 2 ? f->roots[source] : f->draft;
        for (unsigned i = 0; i < FS3_FILES; ++i) {
            const uint8_t *r = const_record(root, i);
            if (r[0])
                mark_extent(f, get32(r + 20), get32(r + 24));
        }
    }
    unsigned run = 0;
    for (unsigned i = FS3_FIRST_DATA; i < FS3_DISK_BLOCKS; ++i) {
        if (f->occupied[i / 8] & (1u << (i % 8)))
            run = 0;
        else if (++run == blocks)
            return i + 1 - blocks;
    }
    return 0;
}
static int prepare_write(struct fs3 *f, unsigned index, unsigned needed) {
    uint8_t *r = record(f->draft, index);
    uint32_t old = get32(r + 20), capacity = get32(r + 24), size = get32(r + 16);
    if (f->dirty[index] && capacity * FS3_BLOCK >= needed)
        return 0;
    unsigned blocks = 1;
    while (blocks * FS3_BLOCK < needed || blocks * FS3_BLOCK < size)
        blocks *= 2;
    uint32_t fresh = allocate(f, blocks);
    if (!fresh)
        return -28;
    if (!cache_flush(f))
        return -5;
    f->cache_block = UINT32_MAX;
    for (unsigned i = 0; i < blocks; ++i) {
        if (i * FS3_BLOCK < size) {
            if (!read_sectors(f, (old + i) * 8, f->scratch, 8))
                return -5;
            unsigned used = size - i * FS3_BLOCK;
            if (used < FS3_BLOCK)
                zero(f->scratch + used, FS3_BLOCK - used);
        } else
            zero(f->scratch, FS3_BLOCK);
        if (!write_sectors(f, (fresh + i) * 8, f->scratch, 8))
            return -5;
    }
    put32(r + 20, fresh);
    put32(r + 24, blocks);
    f->dirty[index] = true;
    return 0;
}
static bool data_crc(struct fs3 *f, const uint8_t *r, uint32_t *result) {
    uint32_t remaining = get32(r + 16), start = get32(r + 20), c = ~0u;
    for (unsigned i = 0; remaining; ++i) {
        if (!read_sectors(f, (start + i) * 8, f->scratch, 8))
            return false;
        unsigned n = remaining > FS3_BLOCK ? FS3_BLOCK : remaining;
        c = crc_update(c, f->scratch, n);
        remaining -= n;
    }
    *result = ~c;
    return true;
}
static int begin(struct fs3 *f) {
    if (f->transaction)
        return -16;
    if (f->active >= 0)
        copy(f->draft, f->roots[f->active], FS3_ROOT_BYTES);
    else
        blank_root(f->draft);
    zero(f->dirty, sizeof(f->dirty));
    f->transaction = true;
    f->transaction_client = 0;
    return 0;
}
static void abort_transaction(struct fs3 *f) {
    f->cache_dirty = false;
    f->cache_block = UINT32_MAX;
    f->transaction = false;
    f->transaction_client = 0;
    if (f->active >= 0)
        copy(f->draft, f->roots[f->active], FS3_ROOT_BYTES);
    else
        blank_root(f->draft);
    zero(f->dirty, sizeof(f->dirty));
}
static int commit(struct fs3 *f) {
    if (!f->transaction)
        return -22;
    if (!cache_flush(f))
        return -5;
    uint64_t generation = f->active >= 0 ? get64(f->roots[f->active] + 8) : 0;
    if (generation == UINT64_MAX)
        return -75;
    for (unsigned i = 0; i < FS3_FILES; ++i)
        if (f->dirty[i]) {
            uint8_t *r = record(f->draft, i);
            uint32_t c;
            if (r[0]) {
                if (!data_crc(f, r, &c))
                    return -5;
                put32(r + 28, c);
            }
        }
    put64(f->draft + 8, generation + 1);
    put32(f->draft + 32, root_crc(f->draft));
    unsigned bank = f->active == 0 ? 1 : 0;
    if (!barrier(f) || !write_sectors(f, (128 + bank * 2) * 8, f->draft, 16) || !barrier(f))
        return -5;
    if (!f->marked) {
        zero(f->scratch, 512);
        put64(f->scratch, MARKER);
        if (!write_sectors(f, 1023, f->scratch, 1) || !barrier(f))
            return -5;
        f->marked = true;
    }
    copy(f->roots[bank], f->draft, FS3_ROOT_BYTES);
    f->valid[bank] = true;
    f->active = (int)bank;
    for (unsigned i = 0; i < FS3_FILES; ++i)
        if (f->dirty[i])
            f->verified[i] = true;
    f->transaction = false;
    f->transaction_client = 0;
    return 0;
}
static void discard_file_cache(struct fs3 *f, const uint8_t *r) {
    uint32_t start = get32(r + 20), blocks = get32(r + 24);
    if (f->cache_block >= start && f->cache_block - start < blocks) {
        f->cache_dirty = false;
        f->cache_block = UINT32_MAX;
    }
}
static int operation(struct fs3 *f, const struct fs_message *q, struct fs_message *out) {
    if (q->op == FS_LIST) {
        for (unsigned i = q->offset; i < FS3_FILES; ++i) {
            const uint8_t *r = const_record(f->draft, i);
            if (!r[0])
                continue;
            copy(out->name, r, 16);
            out->offset = i + 1;
            out->size = get32(r + 16);
            return 0;
        }
        return -2;
    }
    if (!valid_name(q->name))
        return -22;
    unsigned index = FS3_FILES, empty = FS3_FILES;
    for (unsigned i = 0; i < FS3_FILES; ++i) {
        uint8_t *r = record(f->draft, i);
        if (!r[0]) {
            if (empty == FS3_FILES)
                empty = i;
        } else if (same_name(r, q->name))
            index = i;
    }
    if (q->op == FS_CREATE) {
        if (index != FS3_FILES)
            return -17;
        if (empty == FS3_FILES)
            return -28;
        uint8_t *r = record(f->draft, empty);
        zero(r, 32);
        copy(r, q->name, 16);
        f->dirty[empty] = true;
        return 0;
    }
    if (index == FS3_FILES)
        return -2;
    uint8_t *r = record(f->draft, index);
    uint32_t size = get32(r + 16);
    if (q->op == FS_STAT) {
        out->size = size;
        return 0;
    }
    if (q->op == FS_TRUNCATE || q->op == FS_DELETE) {
        discard_file_cache(f, r);
        zero(r + 16, 16);
        if (q->op == FS_DELETE)
            zero(r, 16);
        f->dirty[index] = true;
        return 0;
    }
    if (q->op == FS_WRITE) {
        if (q->offset > FS3_FILE_LIMIT || q->size > FS3_FILE_LIMIT - q->offset)
            return -27;
        if (!q->size)
            return 0;
        unsigned end = q->offset + q->size;
        int e = prepare_write(f, index, end);
        if (e)
            return e;
        for (unsigned done = 0; done < q->size;) {
            unsigned pos = q->offset + done, offset = pos % FS3_BLOCK, n = FS3_BLOCK - offset;
            if (n > q->size - done)
                n = q->size - done;
            uint8_t *data = cache_get(f, get32(r + 20) + pos / FS3_BLOCK);
            if (!data)
                return -5;
            copy(data + offset, q->data + done, n);
            f->cache_dirty = true;
            done += n;
        }
        if (end > size)
            put32(r + 16, end);
        out->size = q->size;
        return 0;
    }
    if (q->op == FS_READ) {
        if (!f->dirty[index] && !f->verified[index]) {
            uint32_t c;
            if (!data_crc(f, r, &c))
                return -5;
            if (c != get32(r + 28)) {
                f->poisoned = true;
                return -5;
            }
            f->verified[index] = true;
        }
        unsigned bytes = q->offset < size ? size - q->offset : 0;
        if (bytes > q->size)
            bytes = q->size;
        for (unsigned done = 0; done < bytes;) {
            unsigned pos = q->offset + done, offset = pos % FS3_BLOCK, n = FS3_BLOCK - offset;
            if (n > bytes - done)
                n = bytes - done;
            uint8_t *data = cache_get(f, get32(r + 20) + pos / FS3_BLOCK);
            if (!data)
                return -5;
            copy(out->data + done, data + offset, n);
            done += n;
        }
        out->size = bytes;
        return 0;
    }
    return -22;
}
static void handle(struct fs3 *f, const struct fs_message *q, struct fs_message *out) {
    *out = (struct fs_message){.op = q->op, .status = -22};
    if (f->poisoned) {
        out->status = -5;
        return;
    }
    if (q->size > 32)
        return;
    if (q->op == FS_BEGIN) {
        out->status = begin(f);
        return;
    }
    if (q->op == FS_COMMIT) {
        out->status = commit(f);
        return;
    }
    if (q->op == FS_ABORT) {
        out->status = f->transaction ? 0 : -22;
        if (f->transaction)
            abort_transaction(f);
        return;
    }
    if (q->op == FS_QUIT) {
        out->status = 0;
        return;
    }
    if (q->op < FS_CREATE || q->op > FS_TRUNCATE)
        return;
    bool mutation =
        q->op == FS_CREATE || q->op == FS_WRITE || q->op == FS_DELETE || q->op == FS_TRUNCATE;
    bool automatic = mutation && !f->transaction;
    if (automatic)
        (void)begin(f);
    out->status = operation(f, q, out);
    if (automatic) {
        if (out->status)
            abort_transaction(f);
        else {
            out->status = commit(f);
            if (out->status && !f->poisoned)
                abort_transaction(f);
        }
    }
}
void fs3_handle_client(struct fs3 *f, uint64_t client, const struct fs_message *q,
                       struct fs_message *out) {
    if (f->transaction && f->transaction_client != client) {
        *out = (struct fs_message){.op = q->op, .status = f->poisoned ? -5 : -16};
        return;
    }
    handle(f, q, out);
    f->transaction_client = f->transaction ? client : 0;
}
void fs3_handle(struct fs3 *f, const struct fs_message *q, struct fs_message *out) {
    fs3_handle_client(f, 0, q, out);
}
bool fs3_client_closed(struct fs3 *f, uint64_t client) {
    if (!f->transaction || f->transaction_client != client)
        return false;
    abort_transaction(f);
    return true;
}
int fs3_import_legacy(struct fs3 *f, const uint8_t *payload, size_t bytes) {
    if (f->poisoned)
        return -5;
    if (f->active >= 0 || f->transaction)
        return -16;
    if (!payload || (bytes != 131264 && bytes != 8384))
        return -22;
    unsigned stride = bytes == 8384 ? 1048 : 16408, limit = stride - 24;
    /* Validate every record before any IO or live state mutation. */
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *r = payload + i * stride;
        unsigned used = get32(r + 16), length = get32(r + 20);
        if (used > 1 || length > limit)
            return -22;
        if (!used)
            continue;
        if (!valid_name((const char *)r))
            return -22;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *previous = payload + j * stride;
            if (get32(previous + 16) && same_name(previous, (const char *)r))
                return -22;
        }
    }
    (void)begin(f);
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *r = payload + i * stride;
        if (!get32(r + 16))
            continue;
        struct fs_message q = {.op = FS_CREATE}, out;
        copy(q.name, r, 16);
        int e = operation(f, &q, &out);
        if (e) {
            abort_transaction(f);
            return e;
        }
        q.op = FS_WRITE;
        unsigned length = get32(r + 20);
        for (unsigned offset = 0; offset < length; offset += 32) {
            q.offset = offset;
            q.size = length - offset;
            if (q.size > 32)
                q.size = 32;
            copy(q.data, r + 24 + offset, q.size);
            e = operation(f, &q, &out);
            if (e) {
                abort_transaction(f);
                return e;
            }
        }
    }
    int e = commit(f);
    if (e && !f->poisoned)
        abort_transaction(f);
    return e;
}
