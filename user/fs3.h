#ifndef NEUROOS_FS3_H
#define NEUROOS_FS3_H
#include "fs.h"
#define FS3_BLOCK 4096u
#define FS3_ROOT_BYTES 8192u
#define FS3_FILES 128u
#define FS3_FILE_LIMIT 1048576u
#define FS3_DISK_BLOCKS 16384u
#define FS3_FIRST_DATA 132u
struct fs3_io {
    void *context;
    bool (*read)(void *, uint32_t sector, void *buffer);
    bool (*write)(void *, uint32_t sector, const void *buffer);
    bool (*flush)(void *);
};
struct fs3 {
    struct fs3_io io;
    uint8_t roots[2][FS3_ROOT_BYTES], draft[FS3_ROOT_BYTES];
    uint8_t cache[FS3_BLOCK], scratch[FS3_BLOCK], occupied[FS3_DISK_BLOCKS / 8];
    bool valid[2], dirty[FS3_FILES], verified[FS3_FILES];
    bool transaction, poisoned, cache_dirty, marked;
    int active;
    uint32_t cache_block;
};
/* 1 = mounted, 0 = no committed v3 (caller must check v2/new-disk policy),
 * negative = IO/corruption. Mount never formats the device. */
int fs3_mount(struct fs3 *, const struct fs3_io *);
/* Caller supplies the payload of a verified legacy snapshot; no legacy sector
 * is modified. Returns 0 on committed import, negative on error. */
int fs3_import_legacy(struct fs3 *, const uint8_t *payload, size_t bytes);
void fs3_handle(struct fs3 *, const struct fs_message *, struct fs_message *);
#endif
