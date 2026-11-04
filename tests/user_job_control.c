/* Fixed trusted test desktop: exercise job handles directly, without a model. */
#include "../user/abi.h"
#include "../user/desktop.h"
#include "../user/events.h"
#include "../user/file.h"
static struct native_job_info info;
static struct nos_event_query events;
static unsigned checks;
#define STR_INNER(x) #x
#define STR(x) STR_INNER(x)
#define CHECK(test)                                                                                \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(test)) {                                                                             \
            say("JOB CONTROL FAIL AT " STR(__LINE__) "\n");                                        \
            return checks;                                                                         \
        }                                                                                          \
    } while (0)
static void text(char *out, const char *in) {
    while ((*out++ = *in++)) {
    }
}
static int contains(const char *s, const char *part) {
    for (; *s; ++s) {
        unsigned i = 0;
        while (part[i] && s[i] == part[i])
            ++i;
        if (!part[i])
            return 1;
    }
    return 0;
}
static long start(const char *name, const char *args, unsigned flags) {
    struct desktop_launch_request q = {.flags = flags};
    text(q.name, name);
    text(q.arguments, args);
    return call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q));
}
static long poll(uint64_t id) {
    info.id = id;
    return call3(20, NOS_JOB_POLL, (uintptr_t)&info, sizeof(info));
}
static long read_events(uint64_t after) {
    events = (struct nos_event_query){
        .version = NOS_EVENT_VERSION, .capacity = NOS_EVENT_BATCH, .after = after};
    return call3(21, (uintptr_t)&events, sizeof(events), 0);
}
static int wait_finished(uint64_t id) {
    uint64_t deadline = (uint64_t)call3(5, 0, 0, 0) + 500;
    do {
        if (poll(id))
            return 0;
        if (info.state == NOS_JOB_FINISHED)
            return 1;
        (void)call3(12, 1, 0, 0);
    } while ((uint64_t)call3(5, 0, 0, 0) < deadline);
    return 0;
}
int user_main(void) {
    say("DESKTOP READY\n");
    CHECK(read_events(0) == 0 && !events.count && !events.next);
    CHECK(call3(21, (uintptr_t)&events, sizeof(events) - 1, 0) == -22);
    CHECK(call3(21, 0x400000, sizeof(events), 0) == -14);
    CHECK(call3(21, (uintptr_t)&events, sizeof(events), 1) == -22);
    events.version = 2;
    CHECK(call3(21, (uintptr_t)&events, sizeof(events), 0) == -22 && events.version == 2);
    events.version = 1;
    events.capacity = 0;
    CHECK(call3(21, (uintptr_t)&events, sizeof(events), 0) == -22);
    CHECK(read_events(1) == -22);
    struct desktop_launch_request q = {0};
    CHECK(call3(20, 99, 0, 0) == -22);
    CHECK(call3(20, NOS_JOB_START, 0, sizeof(q)) == -14);
    CHECK(call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q)) == -22);
    text(q.name, "hello.nos");
    q.flags = 2;
    CHECK(call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q)) == -22);
    q.flags = 0;
    q.arguments[79] = 'x';
    CHECK(call3(20, NOS_JOB_START, (uintptr_t)&q, sizeof(q)) == -22);
    CHECK(call3(20, NOS_JOB_POLL, (uintptr_t)&info, 1) == -22);
    CHECK(call3(20, NOS_JOB_POLL, 0x400000, sizeof(info)) == -14);
    CHECK(poll(0) == -2);
    CHECK(call3(20, NOS_JOB_CANCEL, 1, 0) == -2);
    CHECK(call3(20, NOS_JOB_REAP, 1, 0) == -2);
    long id = start("spin.nos", "", 0);
    CHECK(id > 0);
    uint64_t first_id = (uint64_t)id;
    CHECK(call3(12, 3, 0, 0) == 0);
    CHECK(start("hello.nos", "", 0) == -16);
    CHECK(poll((uint64_t)id + 1) == -2);
    CHECK(poll((uint64_t)id) == 0 && info.state == NOS_JOB_RUNNING);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == -16);
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id + 1, 0) == -2);
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id, 1) == -22);
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id, 0) == 0);
    CHECK(wait_finished((uint64_t)id) && info.result.kind == NOS_LAUNCH_CANCELLED);
    CHECK(call3(20, NOS_JOB_POLL, 0x400000, sizeof(info)) == -14);
    CHECK(poll((uint64_t)id) == 0 && info.state == NOS_JOB_FINISHED);
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id, 0) == -16);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id + 1, 0) == -2);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    CHECK(poll((uint64_t)id) == -2);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == -2);
    long other = start("hello.nos", "CONTROL", 0);
    CHECK(other > id);
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id, 0) == -2);
    CHECK(poll((uint64_t)id) == -2);
    CHECK(wait_finished((uint64_t)other) && info.result.kind == NOS_LAUNCH_EXITED &&
          info.result.exit_code == 42);
    CHECK(contains(info.result.output, "ARGUMENT: CONTROL"));
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)other, 0) == 0);
    id = start("filedemo.nos", "job", NOS_LAUNCH_FILES);
    CHECK(id > other);
    uint64_t draft_id = (uint64_t)id;
    uint64_t deadline = (uint64_t)call3(5, 0, 0, 0) + 300;
    do {
        CHECK(poll((uint64_t)id) == 0 && info.state == NOS_JOB_RUNNING);
        if (contains(info.result.output, "JOB PRIVATE TRANSACTION OPEN"))
            break;
        (void)call3(12, 1, 0, 0);
    } while ((uint64_t)call3(5, 0, 0, 0) < deadline);
    CHECK(contains(info.result.output, "JOB PRIVATE TRANSACTION OPEN"));
    CHECK(nos_file_size("job-draft.tmp") == -16);
    struct fs_message request = {.op = FS_COMMIT}, reply;
    CHECK(nos_fs_request(&request, &reply) == -16);
    request.op = FS_ABORT;
    CHECK(nos_fs_request(&request, &reply) == -16);
    request.op = FS_CLIENT_CLOSED;
    for (unsigned i = 0; i < 8; ++i)
        request.data[i] = (char)((uint64_t)id >> (8 * i));
    CHECK(nos_fs_request(&request, &reply) == -1); /* Desktop is not the kernel broker. */
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id, 0) == 0);
    CHECK(wait_finished((uint64_t)id) && !info.cleanup_error &&
          info.result.kind == NOS_LAUNCH_CANCELLED);
    CHECK(nos_file_size("job-draft.tmp") == -2);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    id = start("filedemo.nos", "read", NOS_LAUNCH_FILES);
    CHECK(id > 0);
    unsigned simultaneous = 0;
    do {
        CHECK(poll((uint64_t)id) == 0);
        if (info.state == NOS_JOB_FINISHED)
            break;
        char data[4];
        CHECK(nos_file_size("sentinel.txt") == 4);
        CHECK(nos_file_read("sentinel.txt", 0, data, 4) == 4);
        CHECK(data[0] == 's' && data[1] == 'e' && data[2] == 'e' && data[3] == 'd');
        ++simultaneous;
        (void)call3(12, 1, 0, 0);
    } while (simultaneous < 300);
    CHECK(simultaneous > 0 && info.state == NOS_JOB_FINISHED);
    CHECK(info.result.kind == NOS_LAUNCH_EXITED && !info.result.exit_code && !info.cleanup_error);
    CHECK(contains(info.result.output, "JOB IPC LOOP PASS"));
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    id = start("bogus.nos", "", 0);
    CHECK(id > 0 && wait_finished((uint64_t)id) && info.result.kind == NOS_LAUNCH_LOAD_ERROR);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    id = start("filedemo.nos", "heap", 0);
    CHECK(id > 0 && wait_finished((uint64_t)id) && info.result.kind == NOS_LAUNCH_EXITED &&
          !info.result.exit_code);
    uint64_t heap_id = (uint64_t)id;
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    id = start("filedemo.nos", "death", 0);
    CHECK(id > 0 && wait_finished((uint64_t)id) && info.result.kind == NOS_LAUNCH_FAULTED);
    uint64_t fault_id = (uint64_t)id;
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    id = start("spin.nos", "", 0);
    CHECK(id > 0);
    uint64_t limit_id = (uint64_t)id;
    deadline = (uint64_t)call3(5, 0, 0, 0) + 1500;
    do {
        CHECK(poll((uint64_t)id) == 0);
        if (info.state == NOS_JOB_FINISHED)
            break;
        (void)call3(12, 10, 0, 0);
    } while ((uint64_t)call3(5, 0, 0, 0) < deadline);
    CHECK(info.state == NOS_JOB_FINISHED && info.result.kind == NOS_LAUNCH_TIMED_OUT);
    CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    CHECK(read_events(0) == 0 && events.count && !events.lost);
    uint64_t initial_next = events.next;
    CHECK(read_events(0) == 0 && events.next == initial_next); /* Independent reader. */
    uint64_t cursor = 0, ticks = 0, owner = 0;
    unsigned observed = 0;
    do {
        CHECK(read_events(cursor) == 0 && !events.lost);
        for (unsigned i = 0; i < events.count; ++i) {
            const struct nos_event *event = &events.events[i];
            CHECK(event->sequence == cursor + 1 && event->ticks >= ticks && event->owner);
            if (!owner)
                owner = event->owner;
            CHECK(event->owner == owner);
            cursor = event->sequence;
            ticks = event->ticks;
            if (event->kind == NOS_EVENT_STARTED) {
                CHECK(event->pages > 0);
                observed |= 1;
            }
            if (event->kind == NOS_EVENT_CANCELLED && event->job == first_id) {
                CHECK(event->cpu_ticks > 0 && event->pages > 0);
                observed |= 2;
            }
            if (event->kind == NOS_EVENT_EXITED && event->job == (uint64_t)other) {
                CHECK(event->value == 42);
                observed |= 4;
            }
            if (event->kind == NOS_EVENT_FS_CLOSED && event->job == draft_id) {
                CHECK(event->value == 1 && (event->flags & NOS_LAUNCH_FILES));
                observed |= 8;
            }
            if (event->kind == NOS_EVENT_LOAD_FAILED) {
                CHECK(event->value == (uint64_t)-8);
                observed |= 16;
            }
            if (event->kind == NOS_EVENT_EXITED && event->job == heap_id) {
                CHECK(event->pages > 400);
                observed |= 32;
            }
            if (event->kind == NOS_EVENT_FAULTED && event->job == fault_id) {
                CHECK(event->value == 14 && event->pages > 400);
                observed |= 64;
            }
            if (event->kind == NOS_EVENT_TIMED_OUT && event->job == limit_id) {
                CHECK(event->cpu_ticks == 1000 && event->pages > 0);
                observed |= 128;
            }
        }
    } while (events.count);
    CHECK(observed == 255);
    for (unsigned i = 0; i < 20; ++i) {
        id = start("hello.nos", "overflow", 0);
        CHECK(id > 0 && wait_finished((uint64_t)id));
        CHECK(call3(20, NOS_JOB_REAP, (uint64_t)id, 0) == 0);
    }
    CHECK(read_events(0) == 0 && events.lost > 0 && events.oldest == events.lost + 1);
    CHECK(events.count == NOS_EVENT_BATCH && events.events[0].sequence == events.oldest);
    say("JOB EVENT TEST PASS\n");
    say("JOB CONTROL TEST PASS\n");
    return 0;
}
