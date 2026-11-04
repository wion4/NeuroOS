/* Test controller owns real loading jobs; reports numeric recovery evidence. */
#include "../user/abi.h"
#include "../user/desktop.h"
#include "../user/events.h"
#include "../user/file.h"
static void check(int ok) {
    if (!ok) {
        say("LOADER FAILURE TEST FAIL\n");
        finish(99);
    }
}
int user_main(void) {
    say("DESKTOP READY\n");
    check(call3(22, 0, 0, 0) == -1);
    /* Gives the harness time to arm the existing kernel PIO fault counter. */
    for (unsigned i = 0; i < 20; ++i)
        (void)call3(12, 10, 0, 0);
    struct desktop_launch_request q = {.name = "hello.nos"};
    long id = call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q));
    check(id > 0);
    struct native_job_info info = {.id = (uint64_t)id};
    uint64_t deadline = (uint64_t)call3(5, 0, 0, 0) + 7000;
    for (;;) {
        check(call3(20, NOS_JOB_POLL, (uintptr_t)&info, sizeof(info)) == 0);
        if (info.state == NOS_JOB_FINISHED)
            break;
        check(info.state == NOS_JOB_LOADING || info.state == NOS_JOB_CLOSING);
        check((uint64_t)call3(5, 0, 0, 0) < deadline);
        (void)call3(12, 10, 0, 0);
    }
    check(info.result.kind == NOS_LAUNCH_LOAD_ERROR && !info.cleanup_error);
    struct nos_event_query events = {.version = NOS_EVENT_VERSION, .capacity = NOS_EVENT_BATCH};
    check(call3(21, (uintptr_t)&events, sizeof(events), 0) == 0 && !events.lost);
    unsigned load_failed = 0, closed = 0, recovered = 0;
    for (unsigned i = 0; i < events.count; ++i) {
        const struct nos_event *e = &events.events[i];
        check(e->job == (uint64_t)id && e->kind != NOS_EVENT_STARTED && !e->cpu_ticks && !e->pages);
        if (e->kind == NOS_EVENT_LOAD_FAILED) {
            check((int64_t)e->value == EXPECTED_ERROR);
            ++load_failed;
        } else if (e->kind == NOS_EVENT_FS_CLOSED) {
            check(e->value == 1);
            ++closed;
        } else if (e->kind == NOS_EVENT_FS_RECOVERED)
            ++recovered;
    }
    check(load_failed == 1 && closed == EXPECTED_CLOSE && recovered == EXPECTED_RECOVERY);
    check(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    check(nos_file_size("sentinel.txt") == 4);
    check(nos_file_write("after.txt", "after", 5) == 5);
    say("LOADER FAILURE TEST PASS\n");
    return 0;
}
