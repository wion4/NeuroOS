#include "../user/abi.h"
#include "../user/desktop.h"
#include "../user/file.h"
static void check(int ok) {
    if (!ok) {
        say("LOADER BUSY FS TEST FAIL\n");
        finish(99);
    }
}
int user_main(void) {
    say("DESKTOP READY\n");
    struct desktop_launch_request q = {.name = "hello.nos"};
    long id = call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q));
    check(id > 0);
    struct native_job_info info = {.id = (uint64_t)id};
    uint64_t end = (uint64_t)call3(5, 0, 0, 0) + 500;
    do {
        long size = nos_file_size("sentinel.txt");
        check(size == 4 || size == -16);
        check(call3(20, NOS_JOB_POLL, (uintptr_t)&info, sizeof(info)) == 0);
        if (info.state == NOS_JOB_FINISHED)
            break;
        (void)call3(12, 1, 0, 0);
    } while ((uint64_t)call3(5, 0, 0, 0) < end);
    check(info.state == NOS_JOB_FINISHED && info.result.kind == NOS_LAUNCH_EXITED &&
          info.result.exit_code == 42);
    check(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    say("LOADER BUSY FS TEST PASS\n");
    return 0;
}
