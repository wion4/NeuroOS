#include "shell.h"
#include "alloc.h"
#include "io.h"
#include "process.h"
#include "version.h"
struct allocation {
    uint64_t address, pages;
    void *heap;
};
static struct allocation slots[16];
static bool same(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static bool decimal(const char *s, uint64_t *value) {
    uint64_t n = 0;
    if (!*s)
        return false;
    while (*s) {
        if (*s < '0' || *s > '9' || n > (UINT64_MAX - (uint64_t)(*s - '0')) / 10)
            return false;
        n = n * 10 + (uint64_t)(*s++ - '0');
    }
    *value = n;
    return true;
}
static void execute(char *line) {
    while (*line == ' ')
        ++line;
    char *arg = line;
    while (*arg && *arg != ' ')
        ++arg;
    if (*arg)
        *arg++ = 0;
    while (*arg == ' ')
        ++arg;
    char *end = arg;
    while (*end)
        ++end;
    while (end > arg && end[-1] == ' ')
        *--end = 0;
    if (!*line)
        return;
    if (same(line, "edit")) {
        editor_run(arg);
        return;
    }
    if (same(line, "desktop")) {
        (void)kernel_desktop();
        return;
    }
    if (same(line, "cc")) {
        (void)kernel_compile(arg);
        return;
    }
    if (same(line, "fs3test")) {
        (void)kernel_fs3_test(arg);
        return;
    }
    if (filesystem_command(line, arg))
        return;
    if (same(line, "echo")) {
        console_write(arg);
        console_write("\n");
        return;
    }
    if (same(line, "alloc") || same(line, "heap")) {
        uint64_t n;
        if (!decimal(arg, &n) || !n) {
            console_write("ERROR: positive size required\n");
            return;
        }
        unsigned slot = 0;
        while (slot < 16 && (slots[slot].address || slots[slot].heap))
            ++slot;
        if (slot == 16) {
            console_write("ERROR: all 16 slots busy\n");
            return;
        }
        if (same(line, "alloc")) {
            slots[slot].address = pmm_alloc(n);
            slots[slot].pages = n;
        } else
            slots[slot].heap = heap_alloc(n);
        if (!slots[slot].address && !slots[slot].heap) {
            console_write("ERROR: out of memory\n");
            return;
        }
        console_write("ALLOCATED ID ");
        console_number(slot);
        console_write("\n");
        return;
    }
    if (same(line, "free")) {
        uint64_t id;
        if (!decimal(arg, &id) || id >= 16 || (!slots[id].address && !slots[id].heap)) {
            console_write("ERROR: invalid allocation ID\n");
            return;
        }
        bool ok = slots[id].heap ? heap_free(slots[id].heap)
                                 : pmm_free(slots[id].address, slots[id].pages);
        if (ok) {
            slots[id] = (struct allocation){0, 0, 0};
            console_write("FREED\n");
        } else
            console_write("ERROR: allocation state invalid\n");
        return;
    }
    if (*arg) {
        console_write("ERROR: unexpected argument\n");
        return;
    }
    if (same(line, "help")) {
        console_write("help info mem uptime test proctest fstest clear echo TEXT\nalloc PAGES  "
                      "heap BYTES  free ID  halt\n");
        console_write("ls touch NAME write NAME TEXT cat NAME stat NAME rm NAME\n");
        console_write("install hello.nos  run NAME.nos ARGUMENTS\n");
        console_write("runfs NAME.nos - GRANT FILE SERVICE ACCESS\n");
        console_write("edit NAME - multiline source editor\n");
        console_write("cc SOURCE.c OUTPUT.nos - native C compiler\n");
        console_write("install-sdk - install C headers and static library\n");
        console_write("desktop - open the graphical desktop\n");
    } else if (same(line, "info")) {
        console_write("NeuroOS " NEUROOS_VERSION " - native x86-64 kernel\nRing3 tasks / timer scheduler / "
                      "restricted IPC\nUser file service / persistent snapshots / no AI yet\n");
    } else if (same(line, "install-sdk")) {
        (void)kernel_install_sdk();
    } else if (same(line, "restartfs")) {
        (void)kernel_restart_filesystem();
    } else if (same(line, "iofaulttest")) {
        if (!kernel_service_io_recovery_test())
            console_write("IO SERVICE RECOVERY FAIL\n");
    } else if (same(line, "mem")) {
        console_write("PAGES FREE ");
        console_number(pmm_available());
        console_write(" TOTAL ");
        console_number(pmm_total());
        console_write("\nHEAP FREE BYTES ");
        console_number(heap_available());
        console_write("\n");
    } else if (same(line, "uptime")) {
        console_write("TICKS ");
        console_number(timer_ticks());
        console_write(" AT APPROX 100 HZ\n");
    } else if (same(line, "test")) {
        console_write(kernel_selftest() ? "SELFTEST PASS\n" : "SELFTEST FAIL\n");
    } else if (same(line, "proctest")) {
        (void)process_demo();
    } else if (same(line, "fstest")) {
        (void)kernel_services();
    } else if (same(line, "recoverytest")) {
        if (!kernel_service_recovery_test())
            console_write("SERVICE RECOVERY FAIL\n");
    } else if (same(line, "clear"))
        console_clear();
    else if (same(line, "halt")) {
        console_write("HALTED\n");
        kernel_halt();
    } else
        console_write("ERROR: unknown command - type help\n");
}

__attribute__((noreturn)) void shell_run(void) {
    char line[96];
    unsigned length = 0;
    bool overflow = false, was_cr = false;
    console_write("SHELL READY\nType help. Click inside the VM to type.\n> ");
    for (;;) {
        char c = keyboard_poll();
        if (!c && (io_in(0x3fd) & 1))
            c = (char)io_in(0x3f8);
        if (c) {
            if (c == '\n' && was_cr) {
                was_cr = false;
                continue;
            }
            was_cr = c == '\r';
            if (c == '\n' || c == '\r') {
                console_write("\n");
                line[length] = 0;
                if (overflow)
                    console_write("ERROR: line too long\n");
                else
                    execute(line);
                length = 0;
                overflow = false;
                console_write("> ");
            } else if ((c == '\b' || c == 127) && !overflow) {
                if (length) {
                    --length;
                    console_char('\b');
                }
            } else if (c >= 32 && c <= 126 && !overflow) {
                if (length + 1 < sizeof(line)) {
                    line[length++] = c;
                    console_char(c);
                } else
                    overflow = true;
            }
        } else
            __asm__ volatile("sti; hlt" : : : "memory");
    }
}
