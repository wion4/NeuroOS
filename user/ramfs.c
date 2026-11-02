#include "abi.h"
#include "fs.h"
static uint8_t backup[FS_SNAPSHOT_SIZE];
static bool transaction;
int user_main(void) {
    fs_reset();
    int disk = storage_load();
    if (disk == -2) {
        say("STORAGE CORRUPT - WRITES DISABLED\n");
        return 3;
    }
    bool persistent = disk >= 0;
    say(persistent ? "PERSISTENT FILE SERVICE READY\n" : "RAMFS SERVICE READY\n");
    for (;;) {
        struct fs_message request, response;
        long n = receive_message(1, &request, sizeof(request));
        if (n == -11) {
            yield();
            continue;
        }
        if (n != sizeof(request))
            return 1;
        if (request.op == 9)
            response = (struct fs_message){.op = 9, .size = persistent ? 1 : 0};
        else if (request.op == FS_BEGIN) {
            response = (struct fs_message){.op = FS_BEGIN, .status = transaction ? -16 : 0};
            if (!transaction) {
                if (!fs_export(backup, sizeof(backup)))
                    return 5;
                transaction = true;
            }
        } else if (request.op == FS_COMMIT) {
            response = (struct fs_message){.op = FS_COMMIT, .status = transaction ? 0 : -22};
            if (transaction) {
                if (persistent && !storage_save())
                    return 4;
                transaction = false;
            }
        } else if (request.op == FS_ABORT) {
            response = (struct fs_message){.op = FS_ABORT, .status = transaction ? 0 : -22};
            if (transaction) {
                if (!fs_import(backup, sizeof(backup)))
                    return 6;
                transaction = false;
            }
        } else
            fs_handle(&request, &response);
        if (persistent && !transaction && !response.status &&
            (request.op == FS_CREATE || request.op == FS_WRITE || request.op == FS_DELETE ||
             request.op == FS_TRUNCATE) &&
            !storage_save()) {
            say("STORAGE COMMIT FAILED\n");
            return 4;
        }
        while ((n = send_message(2, &response, sizeof(response))) == -11)
            yield();
        if (n < 0)
            return 2;
        if (request.op == FS_QUIT && response.status == 0)
            return 0;
    }
}
