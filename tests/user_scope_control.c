#include "../user/abi.h"
#include "../user/desktop.h"
#include "../user/events.h"
#include "../user/file.h"
#define S1(x) #x
#define S(x) S1(x)
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            say("SCOPE CONTROL FAIL " S(__LINE__) "\n");                                           \
            finish(99);                                                                            \
        }                                                                                          \
    } while (0)
static struct native_job_info info;
static void text(char *out, const char *in) {
    while ((*out++ = *in++)) {
    }
}
static int contains(const char *s, const char *part) {
    for (; *s; ++s) {
        unsigned n = 0;
        while (part[n] && s[n] == part[n])
            ++n;
        if (!part[n])
            return 1;
    }
    return 0;
}
static struct nos_scoped_launch request(const char *args, const char *name, unsigned rights) {
    struct nos_scoped_launch q = {.files.count = 1};
    text(q.launch.name, "scope.nos");
    text(q.launch.arguments, args);
    text(q.files.grants[0].name, name);
    q.files.grants[0].rights = rights;
    return q;
}
static long start(struct nos_scoped_launch *q) {
    return call3(20, NOS_JOB_START_SCOPED, (uintptr_t)q, sizeof(*q));
}
static void poll(uint64_t id) {
    info.id = id;
    CHECK(call3(20, NOS_JOB_POLL, (uintptr_t)&info, sizeof(info)) == 0);
}
static void done(uint64_t id, unsigned kind) {
    uint64_t end = (uint64_t)call3(5, 0, 0, 0) + 500;
    do {
        poll(id);
        if (info.state == NOS_JOB_FINISHED)
            break;
        (void)call3(12, 1, 0, 0);
    } while ((uint64_t)call3(5, 0, 0, 0) < end);
    CHECK(info.state == NOS_JOB_FINISHED && info.result.kind == kind && !info.cleanup_error);
    if (kind == NOS_LAUNCH_EXITED)
        CHECK(info.result.exit_code == 0 && contains(info.result.output, "SCOPED APP PASS"));
    CHECK(call3(20, NOS_JOB_REAP, id, 0) == 0);
}
static void remove_file(const char *name) {
    struct fs_message q = {.op = FS_DELETE}, r;
    text(q.name, name);
    CHECK(nos_fs_request(&q, &r) == 0);
}
static void bytes(const char *name, const char *want, unsigned n) {
    char data[16];
    CHECK(n <= sizeof(data));
    CHECK(nos_file_size(name) == (long)n);
    CHECK(nos_file_read(name, 0, data, n) == (long)n);
    for (unsigned i = 0; i < n; ++i)
        CHECK(data[i] == want[i]);
}
int user_main(void) {
    say("DESKTOP READY\n");
    remove_file("bogus.nos"); /* Leave one slot for CREATE on legacy eight-file FS2. */
    struct nos_scoped_launch q = request("read", "sentinel.txt", NOS_FILE_READ);
    CHECK(call3(20, NOS_JOB_START_SCOPED, 0, sizeof(q)) == -14);
    CHECK(call3(20, NOS_JOB_START_SCOPED, 0x80fff0, sizeof(q)) == -14);
    CHECK(call3(20, NOS_JOB_START_SCOPED, (uintptr_t)&q, sizeof(q) - 1) == -22);
    q.files.count = 0;
    CHECK(start(&q) == -22);
    q.files.count = 0xffffffff;
    CHECK(start(&q) == -22);
    q.files.count = 1;
    q.files.reserved = 1;
    CHECK(start(&q) == -22);
    q.files.reserved = 0;
    q.launch.flags = NOS_LAUNCH_FILES;
    CHECK(start(&q) == -22);
    q.launch.flags = 0;
    q.files.grants[0].rights = 16;
    CHECK(start(&q) == -22);
    q.files.grants[0].rights = 1;
    q.files.grants[0].name[13] = 'x';
    CHECK(start(&q) == -22);
    q.files.grants[0].name[13] = 0;
    q.files.grants[3].rights = 1;
    CHECK(start(&q) == -22);
    q.files.grants[3].rights = 0;
    q.files.count = 2;
    q.files.grants[1] = q.files.grants[0];
    CHECK(start(&q) == -22);
    q = request("read", "sentinel.txt", NOS_FILE_READ);
    struct nos_event_query events = {.version = 1, .capacity = 8};
    CHECK(call3(21, (uintptr_t)&events, sizeof(events), 0) == 0 && !events.count);
    long id = start(&q);
    CHECK(id > 0);
    q.launch.arguments[0] = 'w';
    q.files.grants[0].rights = 15; /* Snapshot cannot change. */
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    bytes("sentinel.txt", "seed", 4);
    q = request("write", "sentinel.txt", NOS_FILE_WRITE);
    id = start(&q);
    CHECK(id > 0);
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    bytes("sentinel.txt", "exit", 4);
    CHECK(nos_file_write("sentinel.txt", "seed", 4) == 4);
    q = request("legacy", "sentinel.txt", NOS_FILE_WRITE);
    id = start(&q);
    CHECK(id > 0);
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    bytes("sentinel.txt", "old!", 4);
    CHECK(nos_file_write("sentinel.txt", "seed", 4) == 4);
    q = request("none", "absent.txt", NOS_FILE_WRITE);
    id = start(&q);
    CHECK(id > 0);
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    CHECK(nos_file_size("absent.txt") == -2);
    q = request("create", "created.txt", NOS_FILE_CREATE);
    id = start(&q);
    CHECK(id > 0);
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    CHECK(nos_file_size("created.txt") == 0);
    remove_file("created.txt");
    q = request("delete", "victim.txt", NOS_FILE_DELETE);
    id = start(&q);
    CHECK(id > 0);
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    CHECK(nos_file_size("victim.txt") == -2);
    CHECK(nos_file_write("victim.txt", "victim", 6) == 6);
    q = request("mixed", "sentinel.txt", NOS_FILE_READ);
    q.files.count = 4;
    text(q.files.grants[1].name, "write.txt");
    q.files.grants[1].rights = NOS_FILE_WRITE;
    text(q.files.grants[2].name, "created.txt");
    q.files.grants[2].rights = NOS_FILE_CREATE;
    text(q.files.grants[3].name, "victim.txt");
    q.files.grants[3].rights = NOS_FILE_DELETE;
    id = start(&q);
    CHECK(id > 0);
    done((uint64_t)id, NOS_LAUNCH_EXITED);
    bytes("sentinel.txt", "seed", 4);
    bytes("write.txt", "edit", 4);
    CHECK(nos_file_size("created.txt") == 0 && nos_file_size("victim.txt") == -2);
    remove_file("created.txt");
    CHECK(nos_file_write("victim.txt", "victim", 6) == 6);
    CHECK(nos_file_write("write.txt", "seed", 4) == 4);
    q = request("hold", "allowed.txt", NOS_FILE_RIGHTS);
    id = start(&q);
    CHECK(id > 0);
    uint64_t end = (uint64_t)call3(5, 0, 0, 0) + 300;
    do {
        poll((uint64_t)id);
        if (contains(info.result.output, "SCOPED PRIVATE DRAFT HELD"))
            break;
        (void)call3(12, 1, 0, 0);
    } while ((uint64_t)call3(5, 0, 0, 0) < end);
    CHECK(contains(info.result.output, "SCOPED PRIVATE DRAFT HELD"));
    CHECK(nos_file_size("allowed.txt") == -16);
    CHECK(call3(20, NOS_JOB_CANCEL, (uint64_t)id, 0) == 0);
    done((uint64_t)id, NOS_LAUNCH_CANCELLED);
    CHECK(nos_file_size("allowed.txt") == -2 && nos_file_size("forbidden.txt") == -2);
    unsigned rollback = 0, observed = 0;
    events = (struct nos_event_query){.version = 1, .capacity = 8};
    do {
        CHECK(call3(21, (uintptr_t)&events, sizeof(events), 0) == 0 && !events.lost);
        for (unsigned i = 0; i < events.count; ++i) {
            CHECK(events.events[i].flags == (NOS_EVENT_FILES | NOS_EVENT_SCOPED_FILES));
            ++observed;
            if (events.events[i].kind == NOS_EVENT_FS_CLOSED &&
                events.events[i].job == (uint64_t)id)
                rollback += (events.events[i].value == 1);
        }
        events.after = events.next;
    } while (events.count);
    CHECK(observed > 20 && rollback == 1);
    CHECK(nos_file_write("bogus.nos", "not an ELF", 10) == 10);
    say("SCOPED FILE TEST PASS\n");
    return 0;
}
