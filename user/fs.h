#ifndef NEUROOS_USER_FS_H
#define NEUROOS_USER_FS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FS_FILE_LIMIT 1048576u
#define FS_FILE_COUNT 128u
/* Frozen legacy payload: used only by the importer and RAM diagnostics. */
#define FS_LEGACY_FILE_LIMIT 16384u
#define FS_RECORD_SIZE (24u + FS_LEGACY_FILE_LIMIT)
#define FS_SNAPSHOT_SIZE (8u * FS_RECORD_SIZE)
#define FS_BEGIN 10u
#define FS_COMMIT 11u
#define FS_ABORT 12u
enum { FS_CREATE = 1, FS_WRITE, FS_READ, FS_DELETE, FS_LIST, FS_STAT, FS_QUIT, FS_TRUNCATE };
struct fs_message {
    uint32_t op, offset;
    char name[16];
    uint32_t size;
    int32_t status;
    char data[32];
};
_Static_assert(sizeof(struct fs_message) == 64, "IPC filesystem message ABI");
void fs_reset(void);
bool fs_export(uint8_t *out, size_t bytes);
bool fs_import(const uint8_t *in, size_t bytes);
int storage_load(void);
bool storage_save(void);
bool storage_read_sector(uint32_t sector, void *buffer);
bool storage_write_sector(uint32_t sector, const void *buffer);
bool storage_flush(void);
bool storage_reset(void);
void fs_handle(const struct fs_message *request, struct fs_message *response);
#endif
