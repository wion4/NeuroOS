#include "../user/fs.h"
#include "process.h"
#include "shell.h"
static uint32_t word(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= (uint32_t)p[i] << (i * 8);
    return v;
}
static uint64_t wide(const uint8_t *p) { return word(p) | ((uint64_t)word(p + 4) << 32); }
static uint32_t crc(const uint8_t *p, uint64_t bytes) {
    uint32_t c = ~0u;
    while (bytes--) {
        c ^= *p++;
        for (unsigned i = 0; i < 8; ++i)
            c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}
static bool request(struct fs_message *q, struct fs_message *r) {
    return process_file_request(q, r) && !r->status;
}
static bool valid_name(const uint8_t *name) {
    if (!name[0] || name[0] == '.')
        return false;
    for (unsigned i = 0; i < 16; ++i) {
        unsigned c = name[i];
        if (!c)
            return true;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_'))
            return false;
    }
    return false;
}
bool kernel_install_sdk(void) {
    const void *module;
    uint64_t bytes;
    if (!kernel_boot_module(7, &module, &bytes) || bytes < 32)
        goto invalid;
    const uint8_t *pack = module;
    const char expected[] = "NOSSDK01";
    for (unsigned i = 0; i < 8; ++i)
        if (pack[i] != (uint8_t)expected[i])
            goto invalid;
    unsigned count = word(pack + 8);
    if (!count || count > FS_FILE_COUNT || word(pack + 12) != 1 || wide(pack + 16) != bytes ||
        word(pack + 28) || 32 + (uint64_t)count * 32 > bytes ||
        word(pack + 24) != crc(pack + 32, bytes - 32))
        goto invalid;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *entry = pack + 32 + i * 32;
        uint64_t offset = word(entry + 16), size = word(entry + 20);
        if (!valid_name(entry) || word(entry + 28) || offset < 32 + count * 32 || offset > bytes ||
            size > bytes - offset || size > FS_FILE_LIMIT ||
            word(entry + 24) != crc(pack + offset, size))
            goto invalid;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *other = pack + 32 + j * 32;
            bool same = true;
            for (unsigned k = 0; k < 16; ++k) {
                if (entry[k] != other[k]) {
                    same = false;
                    break;
                }
                if (!entry[k])
                    break;
            }
            if (same)
                goto invalid;
        }
    }
    /* Preflight every name before opening a transaction: existing data is never
     * silently replaced by an SDK installation. All new files commit together. */
    struct fs_message q, r;
    for (unsigned i = 0; i < count; ++i) {
        q = (struct fs_message){.op = FS_STAT};
        for (unsigned j = 0; j < 16; ++j)
            q.name[j] = (char)pack[32 + i * 32 + j];
        if (!process_file_request(&q, &r))
            goto failed;
        if (!r.status) {
            console_write("ERROR: SDK file already exists: ");
            console_write(q.name);
            console_write("\n");
            return false;
        }
        if (r.status != -2)
            goto failed;
    }
    q = (struct fs_message){.op = FS_BEGIN};
    if (!request(&q, &r))
        goto failed;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *entry = pack + 32 + i * 32;
        uint32_t offset = word(entry + 16), size = word(entry + 20);
        q = (struct fs_message){.op = FS_CREATE};
        for (unsigned j = 0; j < 16; ++j)
            q.name[j] = (char)entry[j];
        if (!request(&q, &r))
            goto abort;
        q.op = FS_WRITE;
        while (q.offset < size) {
            q.size = size - q.offset;
            if (q.size > 32)
                q.size = 32;
            for (unsigned j = 0; j < q.size; ++j)
                q.data[j] = (char)pack[(uint64_t)offset + q.offset + j];
            if (!request(&q, &r))
                goto abort;
            q.offset += q.size;
        }
    }
    q = (struct fs_message){.op = FS_COMMIT};
    if (!request(&q, &r))
        goto abort;
    console_write("SDK INSTALLED ");
    console_number(count);
    console_write(" FILES\n");
    return true;
abort:
    q = (struct fs_message){.op = FS_ABORT};
    (void)process_file_request(&q, &r);
failed:
    console_write("ERROR: SDK installation failed\n");
    return false;
invalid:
    console_write("ERROR: invalid or unavailable SDK package\n");
    return false;
}
