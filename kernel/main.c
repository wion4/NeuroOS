#include "../user/ai.h"
#include "../user/fs.h"
#include "alloc.h"
#include "display.h"
#include "exceptions.h"
#include "io.h"
#include "limine.h"
#include "memory.h"
#include "paging.h"
#include "process.h"
#include "shell.h"
#include "version.h"
#include <stdbool.h>
#include <stdint.h>

__attribute__((used,
               section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER
    __attribute__((used, section(".limine_requests"))) static volatile LIMINE_BASE_REVISION(3)
        __attribute__((
            used, section(".limine_requests"))) static volatile struct limine_firmware_type_request
    firmware = {.id = LIMINE_FIRMWARE_TYPE_REQUEST, .revision = 0, .response = 0};
__attribute__((
    used, section(".limine_requests"))) static volatile struct limine_framebuffer_request video = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0, .response = 0};
__attribute__((
    used, section(".limine_requests"))) static volatile struct limine_memmap_request memory_map = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0, .response = 0};
__attribute__((used,
               section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0, .response = 0};
__attribute__((
    used, section(".limine_requests"))) static volatile struct limine_module_request modules = {
    .id = LIMINE_MODULE_REQUEST, .revision = 0, .response = 0};
__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER

#include "screen.h"
    static struct limine_framebuffer *active_screen;
static bool graphics_active;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static void serial_init(void) {
    out8(0x3f9, 0x00); /* No UART interrupts. */
    out8(0x3fb, 0x80); /* Divisor latch. */
    out8(0x3f8, 0x01); /* 115200 baud. */
    out8(0x3f9, 0x00);
    out8(0x3fb, 0x03); /* Eight bits, no parity, one stop bit. */
    out8(0x3fa, 0xc7); /* Enable and clear FIFOs. */
    out8(0x3fc, 0x03); /* DTR and RTS. */
}

/* Bounded polling avoids an infinite spin if the UART is absent. */
static bool serial_putc(char ch) {
    for (unsigned i = 0; i < 1000000; ++i) {
        if (in8(0x3fd) & 0x20) {
            out8(0x3f8, (uint8_t)ch);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool serial_write(const char *text) {
    while (*text) {
        if (*text == '\n' && !serial_putc('\r'))
            return false;
        if (!serial_putc(*text++))
            return false;
    }
    return true;
}

static void hex(uint64_t value) {
    const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 15]);
}

static void number(uint64_t value) {
    char text[21];
    unsigned n = 0;
    do {
        text[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        serial_putc(text[--n]);
}

static void field(const char *name, uint64_t value) {
    serial_write(name);
    hex(value);
    serial_write("\n");
}

void console_char(char c) {
    if (c == '\n')
        serial_write("\n");
    else if (c == '\b')
        serial_write("\b \b");
    else
        serial_putc(c);
    if (!graphics_active)
        terminal_putc(c);
}
void console_write(const char *text) {
    while (*text)
        console_char(*text++);
}
void console_number(uint64_t value) {
    char text[21];
    unsigned n = 0;
    do {
        text[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        console_char(text[--n]);
}
void console_clear(void) {
    terminal_clear();
    serial_write("\n");
}
__attribute__((noreturn)) void kernel_halt(void) {
    for (;;)
        __asm__ volatile("cli; hlt");
}

bool kernel_selftest(void) {
    uint64_t before = pmm_available();
    size_t heap_before = heap_available();
    uint64_t pages = pmm_alloc(2);
    if (!pages)
        return false;
    volatile uint64_t *memory = (volatile uint64_t *)(pages + hhdm.response->offset);
    for (unsigned i = 0; i < 1024; ++i)
        memory[i] = 0xa55a0000ull + i;
    bool ok = true;
    for (unsigned i = 0; i < 1024; ++i)
        if (memory[i] != 0xa55a0000ull + i)
            ok = false;
    ok = pmm_free(pages, 2) && ok;
    ok = !pmm_free(pages, 2) && ok;
    uint8_t *a = heap_alloc(33), *b = heap_alloc(96);
    if (a && b) {
        for (unsigned i = 0; i < 33; ++i)
            a[i] = (uint8_t)i;
        for (unsigned i = 0; i < 96; ++i)
            b[i] = (uint8_t)(i + 40);
        for (unsigned i = 0; i < 33; ++i)
            if (a[i] != i)
                ok = false;
        for (unsigned i = 0; i < 96; ++i)
            if (b[i] != i + 40)
                ok = false;
    } else
        ok = false;
    if (a)
        ok = heap_free(a) && ok;
    if (b)
        ok = heap_free(b) && ok;
    ok = paging_selftest() && ok;
    return ok && pmm_available() == before && heap_available() == heap_before;
}
bool kernel_services(void) {
    struct limine_module_response *r = modules.response;
    if (!r || r->module_count < 2 || !r->modules || !r->modules[0] || !r->modules[1]) {
        console_write("ERROR: two executable boot modules required\n");
        return false;
    }
    return process_services(r->modules[0]->address, r->modules[0]->size, r->modules[1]->address,
                            r->modules[1]->size);
}
bool kernel_service_recovery_test(void) {
    struct fs_message q = {.op = 9}, state, before, after, original = {0};
    if (!process_file_request(&q, &state) || state.size != 1) {
        console_write("ERROR: recovery test requires persistent storage\n");
        return false;
    }
    q.op = FS_LIST;
    if (!process_file_request(&q, &before))
        return false;
    bool has_file = before.status == 0;
    if (has_file) {
        q = (struct fs_message){.op = FS_READ, .size = 32};
        for (unsigned i = 0; i < 16; ++i)
            q.name[i] = before.name[i];
        if (!process_file_request(&q, &original) || original.status)
            return false;
    }
    q = (struct fs_message){.op = FS_BEGIN};
    if (!process_file_request(&q, &state) || state.status)
        return false;
    if (has_file) {
        q = (struct fs_message){.op = FS_WRITE, .size = 1, .data = {'!'}};
        for (unsigned i = 0; i < 16; ++i)
            q.name[i] = before.name[i];
        if (!process_file_request(&q, &state) || state.status)
            return false;
    }
    q = (struct fs_message){.op = FS_LIST};
    process_filesystem_inject_fault();
    if (process_file_request(&q, &after))
        return false;
    if (!process_file_request(&q, &after))
        return false;
    const uint8_t *x = (const uint8_t *)&before, *y = (const uint8_t *)&after;
    for (unsigned i = 0; i < sizeof(before); ++i)
        if (x[i] != y[i])
            return false;
    if (has_file) {
        q = (struct fs_message){.op = FS_READ, .size = 32};
        for (unsigned i = 0; i < 16; ++i)
            q.name[i] = before.name[i];
        if (!process_file_request(&q, &after) || after.status || after.size != original.size)
            return false;
        for (unsigned i = 0; i < original.size; ++i)
            if (after.data[i] != original.data[i])
                return false;
    }
    console_write("TRANSACTION CRASH ROLLBACK PASS\nSERVICE RECOVERY PASS - KERNEL ALIVE\n");
    return true;
}
bool kernel_restart_filesystem(void) {
    struct fs_message q = {.op = FS_LIST}, r;
    bool ok = process_filesystem_restart() && process_file_request(&q, &r);
    console_write(ok ? "FILESYSTEM SERVICE RESTARTED\n" : "ERROR: filesystem restart failed\n");
    return ok;
}
bool kernel_service_io_recovery_test(void) {
    struct fs_message q = {.op = FS_LIST}, before, original, after;
    if (!process_file_request(&q, &before) || before.status)
        return false;
    q = (struct fs_message){.op = FS_READ, .size = 32};
    for (unsigned i = 0; i < 16; ++i)
        q.name[i] = before.name[i];
    if (!process_file_request(&q, &original) || original.status)
        return false;
    q.op = FS_BEGIN;
    q.size = 0;
    if (!process_file_request(&q, &after) || after.status)
        return false;
    q.op = FS_WRITE;
    q.size = 1;
    q.data[0] = '!';
    process_filesystem_fault_after_io(100);
    bool failed = !process_file_request(&q, &after);
    process_filesystem_fault_after_io(0);
    if (!failed) {
        q.op = FS_ABORT;
        q.size = 0;
        (void)process_file_request(&q, &after);
        return false;
    }
    q.op = FS_READ;
    q.size = 32;
    if (!process_file_request(&q, &after) || after.status || after.size != original.size)
        return false;
    for (unsigned i = 0; i < after.size; ++i)
        if (after.data[i] != original.data[i])
            return false;
    console_write("IO SERVICE RECOVERY PASS - DATA INTACT\n");
    return true;
}
bool kernel_boot_module(unsigned index, const void **address, uint64_t *bytes) {
    if (!modules.response || !modules.response->modules || index >= modules.response->module_count)
        return false;
    struct limine_file *file = modules.response->modules[index];
    if (!file || !file->address || !file->size)
        return false;
    *address = file->address;
    *bytes = file->size;
    return true;
}
bool kernel_compile(const char *arguments) {
    if (!modules.response || modules.response->module_count <= 6 || !modules.response->modules[6]) {
        console_write("ERROR: compiler boot module unavailable\n");
        return false;
    }
    struct limine_file *compiler = modules.response->modules[6];
    return process_run_file(compiler->address, compiler->size, arguments, true);
}
bool kernel_fs3_test(const char *arguments) {
#if NEUROOS_FS3_TEST
    if (modules.response && modules.response->module_count > 9) {
        struct limine_file *program = modules.response->modules[9];
        if (program)
            return process_run_file(program->address, program->size, arguments, true);
    }
#else
    (void)arguments;
#endif
    console_write("ERROR: experimental FS3 test module unavailable\n");
    return false;
}
bool kernel_desktop(void) {
    const void *program;
    uint64_t bytes;
    if (!kernel_boot_module(8, &program, &bytes) || !display_init(active_screen)) {
        console_write("DESKTOP UNAVAILABLE: DISPLAY OR MODULE\n");
        return false;
    }
    graphics_active = true;
    console_write(mouse_init() ? "MOUSE READY\n" : "MOUSE UNAVAILABLE\n");
    bool ok = process_run_desktop(program, bytes);
    console_write(ok ? "DESKTOP EXIT CLEAN\n" : "DESKTOP SESSION FAILED\n");
    graphics_active = false;
    console_clear();
    return ok;
}
bool kernel_ai(const char *prompt, bool trace) {
    struct ai_session session = {.request = {.op = AI_GENERATE, .steps = 48}};
    unsigned length = 0;
    while (prompt[length] && length < 55) {
        session.request.prompt[length] = prompt[length];
        ++length;
    }
    if (prompt[length]) {
        console_write("AI DEMO: maximum prompt is 55 ASCII bytes\n");
        return false;
    }
    if (!process_ai_request(&session)) {
        console_write("AI DEMO UNAVAILABLE OR SERVICE FAILED\n");
        return false;
    }
    if (session.status) {
        console_write("AI REQUEST REJECTED\n");
        return false;
    }
    console_write("AI TEXT: ");
    console_write(session.text);
    console_write("\n");
    if (trace) {
        console_write("AI TOKENS:");
        for (unsigned i = 0; i < session.trace_count; ++i) {
            console_char(' ');
            console_number(session.trace[i]);
        }
        console_write("\n");
    }
    console_write("AI DEMO COMPLETE\n");
    return true;
}
bool kernel_install_program(const char *name) {
    const char *expected = "hello.nos";
    unsigned module = 2, n = 0;
    if (name[0] == 'f') {
        expected = "filedemo.nos";
        module = 3;
    }
    if (name[0] == 'l') {
        expected = "libcdemo.nos";
        module = 4;
    }
    if (name[0] == 'f' && name[1] == 'l') {
        expected = "floatdemo.nos";
        module = 5;
    }
    if (name[0] == 'c') {
        expected = "compiler.nos";
        module = 6;
    }
    while (name[n] && expected[n] && name[n] == expected[n])
        ++n;
    if (name[n] != expected[n] || !modules.response || modules.response->module_count <= module) {
        console_write("ERROR: installation source not found\n");
        return false;
    }
    struct limine_file *source = modules.response->modules[module];
    if (!source || !source->address || source->size > FS_FILE_LIMIT) {
        console_write("ERROR: installation source exceeds file limit or is unavailable\n");
        return false;
    }
    struct fs_message q = {.op = FS_BEGIN}, r;
    for (unsigned i = 0; i <= n; ++i)
        q.name[i] = expected[i];
    if (!process_file_request(&q, &r) || r.status)
        return false;
    q.op = FS_CREATE;
    if (!process_file_request(&q, &r) || r.status) {
        console_write("ERROR: file exists or cannot be created\n");
        goto abort;
    }
    q.op = FS_WRITE;
    for (q.offset = 0; q.offset < source->size;) {
        q.size = (uint32_t)(source->size - q.offset);
        if (q.size > 32)
            q.size = 32;
        for (unsigned i = 0; i < q.size; ++i)
            q.data[i] = ((char *)source->address)[q.offset + i];
        if (!process_file_request(&q, &r) || r.status)
            goto abort;
        q.offset += q.size;
    }
    q.op = FS_COMMIT;
    q.size = 0;
    if (!process_file_request(&q, &r) || r.status)
        goto abort;
    console_write("INSTALLED ");
    console_write(expected);
    console_write("\n");
    return true;
abort:
    q.op = FS_ABORT;
    q.size = 0;
    (void)process_file_request(&q, &r);
    return false;
}

__attribute__((noreturn)) static void stop(bool success) {
#ifdef NEUROOS_BOOT_TEST
    /* QEMU isa-debug-exit: host exit status = (value << 1) | 1. */
    uint32_t value = success ? 0x10 : 0x11;
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"((uint16_t)0xf4));
#else
    (void)success;
#endif
    for (;;)
        __asm__ volatile("cli; hlt");
}

__attribute__((noreturn)) void exception_panic(const struct exception_frame *f, uint64_t cr2) {
    serial_write("PANIC: CPU EXCEPTION\n");
    field("VECTOR=", f->vector);
    field("ERROR=", f->error);
    field("RIP=", f->rip);
    field("CS=", f->cs);
    field("RFLAGS=", f->flags);
    field("RSP=", f->rsp);
    field("CR2=", cr2);
    if (f->vector == 8)
        serial_write(exception_on_df_stack(f) ? "DF IST: OK\n" : "DF IST: BAD\n");
    if (active_screen)
        screen_text(active_screen, 32, 350, "PANIC CPU EXCEPTION", 2,
                    255u << active_screen->red_mask_shift);
#if NEUROOS_FAULT_TEST
    bool expected = f->vector == NEUROOS_FAULT_TEST && f->cs == 8;
#if NEUROOS_FAULT_TEST == 13
    expected = expected && f->error == 0x38;
#else
    expected = expected && f->error == 0;
#endif
#if NEUROOS_FAULT_TEST == 14
    expected = expected && cr2 == VM_TEST_ADDRESS;
#endif
#if NEUROOS_FAULT_TEST == 8
    expected = expected && exception_on_df_stack(f);
#else
    extern char test_fault_rip[];
    expected = expected && f->rip == (uint64_t)test_fault_rip;
#endif
    if (expected) {
        serial_write("SELFTEST: exception PASS\n");
        __asm__ volatile("outl %0, %1" : : "a"(0x12u), "Nd"((uint16_t)0xf4));
        for (;;)
            __asm__ volatile("cli; hlt");
    }
#endif
    stop(false);
}

__attribute__((noreturn)) void kmain(void) {
    __asm__ volatile("cli; cld" : : : "memory");
    serial_init();
    exceptions_init();
    serial_write("CPU: GDT TSS IDT ready\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        serial_write("BOOT ERROR: unsupported boot protocol revision\n");
        stop(false);
    }
    if (!firmware.response || firmware.response->firmware_type != LIMINE_FIRMWARE_TYPE_UEFI64) {
        serial_write("BOOT ERROR: UEFI64 required\n");
        stop(false);
    }
    if (!serial_write("Firmware: UEFI64\n"))
        stop(false);
    struct memory_summary summary;
    if (!memory_analyze(memory_map.response, &summary)) {
        serial_write("BOOT ERROR: invalid physical memory map\n");
        stop(false);
    }
    for (uint64_t i = 0; i < memory_map.response->entry_count; ++i) {
        struct limine_memmap_entry *e = memory_map.response->entries[i];
        serial_write("MEM base=");
        hex(e->base);
        serial_write(" length=");
        hex(e->length);
        serial_write(" type=");
        number(e->type);
        serial_write("\n");
    }
    serial_write("MEMORY: usable_bytes=");
    number(summary.usable_bytes);
    serial_write(" regions=");
    number(summary.usable_regions);
    serial_write("\n");
    if (!hhdm.response || !pmm_init(memory_map.response)) {
        serial_write("BOOT ERROR: physical allocator init\n");
        stop(false);
    }
    if (!paging_init(hhdm.response->offset)) {
        serial_write("BOOT ERROR: page table init\n");
        stop(false);
    }
    uint64_t arena = pmm_alloc(256);
    if (!arena || !heap_init((void *)(arena + hhdm.response->offset), 256 * PAGE_SIZE)) {
        serial_write("BOOT ERROR: heap init\n");
        stop(false);
    }
    if (!kernel_selftest()) {
        serial_write("BOOT ERROR: memory selftest\n");
        stop(false);
    }
    serial_write("ALLOC: physical pages and heap PASS\nPAGING: own tables and mapping test PASS\n");
    /* UTF-8 em dash, independent of the host compiler's source encoding. */
    if (!serial_write("NeuroOS v" NEUROOS_VERSION " \xe2\x80\x94 Kernel boot successful\n"))
        stop(false);
    if (video.response && video.response->framebuffer_count) {
        if (screen_boot(video.response->framebuffers[0], summary.usable_bytes / (1024 * 1024))) {
            active_screen = video.response->framebuffers[0];
            serial_write("Display: boot status rendered\n");
        }
    }
    exception_test_trigger();
#if NEUROOS_FAULT_TEST
    serial_write("BOOT ERROR: exception test returned\n");
    stop(false);
#endif
    terminal_init(active_screen);
#ifdef NEUROOS_BOOT_TEST
    timer_init();
    if (!process_demo()) {
        serial_write("BOOT ERROR: process isolation test\n");
        stop(false);
    }
    if (!kernel_services()) {
        serial_write("BOOT ERROR: user services test\n");
        stop(false);
    }
    stop(true);
#else
    bool keyboard = keyboard_init();
    console_write(keyboard ? "KEYBOARD READY\n" : "KEYBOARD UNAVAILABLE - SERIAL INPUT ONLY\n");
    timer_init();
    bool processes_ok = process_demo();
    bool services_ok = kernel_services();
    console_clear();
    console_write(processes_ok && services_ok ? "STARTUP SELFTEST PASS\n"
                                              : "STARTUP SELFTEST FAIL - SEE SERIAL LOG\n");
    if (modules.response && modules.response->module_count >= 2 && modules.response->modules &&
        modules.response->modules[0] && modules.response->modules[1]) {
        struct limine_file **m = modules.response->modules;
        bool fs_ready =
            process_filesystem_init(m[0]->address, m[0]->size, m[1]->address, m[1]->size);
        struct fs_message probe = {.op = FS_LIST}, reply;
        fs_ready = fs_ready && process_file_request(&probe, &reply);
        console_write(fs_ready ? "FILE SERVICE AVAILABLE\n"
                               : "FILE SERVICE INITIALIZATION FAILED\n");
#if NEUROOS_BOOTSTRAP_SDK
        probe = (struct fs_message){.op = 9};
        if (fs_ready && process_file_request(&probe, &reply) && reply.size == 1) {
            probe = (struct fs_message){.op = FS_STAT, .name = "libnos.a"};
            if (process_file_request(&probe, &reply)) {
                if (reply.status == -2)
                    (void)kernel_install_sdk();
                else if (!reply.status)
                    console_write("SDK AVAILABLE\n");
            }
#if NEUROOS_DESKTOP
            probe = (struct fs_message){.op = FS_STAT, .name = "hello.nos"};
            if (process_file_request(&probe, &reply) && reply.status == -2)
                (void)kernel_install_program("hello.nos");
            probe = (struct fs_message){.op = FS_STAT, .name = "compiler.nos"};
            if (process_file_request(&probe, &reply) && reply.status == -2)
                (void)kernel_install_program("compiler.nos");
#endif
        }
#endif
    }
    __asm__ volatile("sti" : : : "memory");
#if NEUROOS_AI_DEMO
    const void *ai_server, *ai_client, *ai_pack;
    uint64_t ai_server_size, ai_client_size, ai_pack_size;
    bool ai_available = kernel_boot_module(9, &ai_server, &ai_server_size) &&
                        kernel_boot_module(10, &ai_client, &ai_client_size) &&
                        kernel_boot_module(11, &ai_pack, &ai_pack_size) &&
                        process_ai_config(ai_server, ai_server_size, ai_client, ai_client_size,
                                          ai_pack, ai_pack_size);
    console_write(ai_available ? "AI DEMO MODULES AVAILABLE\n" : "AI DEMO MODULES UNAVAILABLE\n");
#endif
#if NEUROOS_DESKTOP
    (void)kernel_desktop();
#endif
    shell_run();
#endif
}
