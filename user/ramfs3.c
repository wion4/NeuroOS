/* Sector-backed server; the no-port diagnostic session uses the legacy RAM backend. */
#include "abi.h"
#include "fs.h"
#include "fs3.h"
static struct fs3 volume;
static uint8_t legacy_snapshot[FS_SNAPSHOT_SIZE];
static bool read_sector(void *context, uint32_t sector, void *buffer) {
    (void)context;
    return storage_read_sector(sector, buffer);
}
static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    (void)context;
    return storage_write_sector(sector, buffer);
}
static bool flush_disk(void *context) {
    (void)context;
    return storage_flush();
}
int user_main(void) {
    fs_reset();
    long device = call3(9, 3, 0x1f7, 0);
    bool persistent = device >= 0 && device != 0 && device != 255, transaction = false;
    uint64_t transaction_client = 0;
    if (persistent) {
        if (!storage_reset()) {
            say("ATA RESET FAILED\n");
            return 4;
        }
        struct fs3_io io = {.read = read_sector, .write = write_sector, .flush = flush_disk};
        int mounted = fs3_mount(&volume, &io);
        if (mounted < 0) {
            say("FS3 STORAGE CORRUPT - WRITES DISABLED\n");
            return 3;
        }
        if (!mounted) {
            /* Only the verified legacy loader may authorize import. These
             * builds target our existing dedicated 64 MiB VM disk, not raw
             * devices or arbitrary partitions. No legacy sector is changed. */
            int legacy = storage_load();
            if (legacy < 0 || !fs_export(legacy_snapshot, sizeof(legacy_snapshot)) ||
                fs3_import_legacy(&volume, legacy_snapshot, sizeof(legacy_snapshot))) {
                say("FS3 LEGACY IMPORT FAILED\n");
                return 4;
            }
            say("STORAGE LEGACY IMPORTED INTO V3\n");
        }
        say("PERSISTENT FS3 FILE SERVICE READY\n");
    } else
        say("RAMFS SERVICE READY\n");
    for (;;) {
        struct fs_message q, r;
        uint64_t sender;
        long n = receive_wait_from(1, &q, sizeof(q), &sender);
        if (n == -11) {
            yield();
            continue;
        }
        if (n < 0)
            return 1;
        bool complete = n == sizeof(q);
        if (!complete)
            r = (struct fs_message){.status = -22};
        else if (q.op == FS_CLIENT_CLOSED) {
            r = (struct fs_message){.op = FS_CLIENT_CLOSED, .status = -1};
            if (!sender) {
                uint64_t client = 0;
                for (unsigned i = 0; i < 8; ++i)
                    client |= (uint64_t)(uint8_t)q.data[i] << (8 * i);
                r.status = 0;
                if (persistent)
                    r.size = fs3_client_closed(&volume, client);
                else if (transaction && transaction_client == client) {
                    if (!fs_import(legacy_snapshot, sizeof(legacy_snapshot)))
                        return 6;
                    transaction = false;
                    r.size = 1;
                }
            }
        } else if (q.op == 9)
            r = (struct fs_message){.op = 9, .size = persistent ? 1 : 0};
        else if (persistent) {
            if (q.op == FS_QUIT)
                r = (struct fs_message){.op = FS_QUIT, .status = -1};
            else
                fs3_handle_client(&volume, sender, &q, &r);
        } else if (transaction && transaction_client != sender)
            r = (struct fs_message){.op = q.op, .status = -16};
        else if (q.op == FS_BEGIN) {
            r = (struct fs_message){.op = FS_BEGIN, .status = transaction ? -16 : 0};
            if (!transaction) {
                if (!fs_export(legacy_snapshot, sizeof(legacy_snapshot)))
                    return 5;
                transaction = true;
                transaction_client = sender;
            }
        } else if (q.op == FS_COMMIT) {
            r = (struct fs_message){.op = FS_COMMIT, .status = transaction ? 0 : -22};
            transaction = false;
        } else if (q.op == FS_ABORT) {
            r = (struct fs_message){.op = FS_ABORT, .status = transaction ? 0 : -22};
            if (transaction) {
                if (!fs_import(legacy_snapshot, sizeof(legacy_snapshot)))
                    return 6;
                transaction = false;
            }
        } else
            fs_handle(&q, &r);
        while ((n = send_message(2, &r, sizeof(r))) == -11)
            yield();
        if (n < 0)
            return 2;
        if (persistent && volume.poisoned)
            return 4;
        if (complete && q.op == FS_QUIT && !r.status)
            return 0;
    }
}
