#include "process.h"
#include "../user/fs.h"
#include "alloc.h"
#include "exceptions.h"
#include "fpu.h"
#include "io.h"
#include "paging.h"
#include "shell.h"
#include <stddef.h>
_Static_assert(offsetof(struct cpu_context, vector) == 120, "ASM registers");
_Static_assert(offsetof(struct cpu_context, cs) == 144, "ASM CS offset");
_Static_assert(sizeof(struct cpu_context) == 176, "ASM full frame");
#define TASKS 9
#define SEND 1u
#define RECEIVE 2u
enum state { READY, EXITED, FAULTED, EXPIRED };
struct capability {
    unsigned endpoint, rights;
};
struct task {
    struct fpu_state fp;
    struct cpu_context context;
    struct user_space space;
    struct capability caps[4];
    enum state state;
    unsigned ticks;
    uint64_t fault, error, address, status;
};
static struct task tasks[TASKS];
static uint8_t stacks[TASKS][16384] __attribute__((aligned(16)));
static bool running, mailbox_full;
static unsigned current, denied, bad_pointers, unknown_calls, switches, sent, received;
static bool receiver_after_fault;
static uint64_t mailbox, saved_kernel_rsp;
static unsigned task_count = TASKS, budget = 8;
static bool interactive;
static struct task parked_server;
void process_filesystem_inject_fault(void) {
    if (parked_server.space.root)
        parked_server.context.rip = 0;
}
static const void *file_client;
static uint64_t file_client_size;
struct message {
    uint8_t data[64];
    uint64_t size, sender;
};
struct endpoint {
    struct message queue[8];
    unsigned head, count, owner, producer;
};
static struct endpoint endpoints[2];
extern char user_program_start[], user_program_end[];
extern void process_enter(struct cpu_context *, uint64_t *);
extern __attribute__((noreturn)) void process_leave(uint64_t);
extern void timer_irq(void);

static bool permits(const struct task *task, uint64_t handle, unsigned rights) {
    return handle < 4 && task->caps[handle].endpoint < 2 &&
           (task->caps[handle].rights & rights) == rights;
}
static void syscall(struct task *task) {
    struct cpu_context *f = &task->context;
    uint64_t call = f->rax;
    f->rax = 0;
    switch (call) {
    case 0:
        break; /* Yield. */
    case 1:
        task->state = EXITED;
        task->status = f->rdi;
        if (current == 1)
            receiver_after_fault = tasks[0].state == FAULTED;
        break;
    case 2:
    case 3: {
        unsigned rights = call == 2 ? SEND : RECEIVE;
        if (!permits(task, f->rdi, rights)) {
            ++denied;
            f->rax = (uint64_t)-1;
            break;
        }
        if (call == 2) {
            if (mailbox_full) {
                f->rax = (uint64_t)-11;
                break;
            }
            mailbox = f->rsi;
            mailbox_full = true;
            ++sent;
        } else {
            if (!mailbox_full) {
                f->rax = (uint64_t)-11;
                break;
            }
            f->rdx = mailbox;
            mailbox_full = false;
            ++received;
        }
        break;
    }
    case 5:
        f->rax = timer_ticks();
        break;
    case 6: {
        char text[129];
        if (f->rsi > 128) {
            f->rax = (uint64_t)-22;
            break;
        }
        if (!user_copy_from(&task->space, text, f->rdi, (size_t)f->rsi)) {
            ++bad_pointers;
            f->rax = (uint64_t)-14;
            break;
        }
        text[f->rsi] = 0;
        console_write("USER ");
        console_number(current);
        console_write(": ");
        /* No terminal control sequences from a user process. */
        for (uint64_t i = 0; i < f->rsi; ++i)
            if ((text[i] >= 32 && text[i] <= 126) || text[i] == '\n')
                console_char(text[i]);
        f->rax = f->rsi;
        break;
    }
    case 7:
    case 8: {
        bool send = call == 7;
        if (!permits(task, f->rdi, send ? SEND : RECEIVE)) {
            ++denied;
            f->rax = (uint64_t)-1;
            break;
        }
        struct endpoint *ep = &endpoints[task->caps[f->rdi].endpoint];
        if (f->rdx > 64) {
            f->rax = (uint64_t)-22;
            break;
        }
        if (send) {
            if (tasks[ep->owner].state != READY) {
                f->rax = (uint64_t)-32;
                break;
            }
            if (ep->count == 8) {
                f->rax = (uint64_t)-11;
                break;
            }
            struct message *m = &ep->queue[(ep->head + ep->count) % 8];
            if (!user_copy_from(&task->space, m->data, f->rsi, (size_t)f->rdx)) {
                ++bad_pointers;
                f->rax = (uint64_t)-14;
                break;
            }
            m->size = f->rdx;
            m->sender = current;
            ++ep->count;
            ++sent;
        } else {
            if (!ep->count) {
                f->rax = (uint64_t)(tasks[ep->producer].state == READY ? -11 : -32);
                break;
            }
            struct message *m = &ep->queue[ep->head];
            if (f->rdx < m->size) {
                f->rax = (uint64_t)-90;
                break;
            }
            if (!user_copy_to(&task->space, f->rsi, m->data, (size_t)m->size)) {
                ++bad_pointers;
                f->rax = (uint64_t)-14;
                break;
            }
            f->rax = m->size;
            f->rdx = m->sender;
            ep->head = (ep->head + 1) % 8;
            --ep->count;
            ++received;
        }
        break;
    }
    case 9: {
        if (f->rdi != 3 || task->caps[3].endpoint != 2 || task->caps[3].rights != 4) {
            f->rax = (uint64_t)-1;
            break;
        }
        unsigned port = (unsigned)(f->rsi & 65535), op = (unsigned)(f->rsi >> 16);
        if ((port < 0x1f0 || port > 0x1f7) && port != 0x3f6) {
            f->rax = (uint64_t)-1;
            break;
        }
        if (op > 3 || (op % 2 && port != 0x1f0)) {
            f->rax = (uint64_t)-22;
            break;
        }
        if (op == 0)
            f->rax = io_in((uint16_t)port);
        else if (op == 1) {
            uint16_t v;
            __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"((uint16_t)port));
            f->rax = v;
        } else if (op == 2)
            io_out((uint16_t)port, (uint8_t)f->rdx);
        else
            __asm__ volatile("outw %0,%1" ::"a"((uint16_t)f->rdx), "Nd"((uint16_t)port));
        break;
    }
    case 10:
        if (f->rdi > 1 || (f->rdi == 1 && f->rsi > USER_HEAP_PAGES))
            f->rax = (uint64_t)-22;
        else if (f->rdi == 1 && !user_heap_resize(&task->space, (unsigned)f->rsi))
            f->rax = (uint64_t)-12;
        else
            f->rax = USER_HEAP + (uint64_t)task->space.heap_pages * 4096;
        break;
    default:
        ++unknown_calls;
        f->rax = (uint64_t)-38;
        break;
    }
}
static struct cpu_context *choose_next(void) {
    if (interactive && tasks[1].state != READY) {
        running = false;
        paging_switch(paging_kernel_root());
        process_leave(saved_kernel_rsp);
    }
    for (unsigned i = 1; i <= task_count; ++i) {
        unsigned next = (current + i) % task_count;
        if (tasks[next].state != READY)
            continue;
        if (next != current)
            ++switches;
        current = next;
        exceptions_kernel_stack((uint64_t)&stacks[next][sizeof(stacks[next])]);
        paging_switch(tasks[next].space.root);
        fpu_restore(&tasks[next].fp);
        return &tasks[next].context;
    }
    running = false;
    paging_switch(paging_kernel_root());
    process_leave(saved_kernel_rsp);
}
struct cpu_context *process_trap(struct cpu_context *frame, uint64_t cr2) {
    if (frame->vector == 32)
        timer_irq();
    if (!(frame->cs & 3)) {
        if (frame->vector == 32)
            return frame;
        exception_panic((const struct exception_frame *)&frame->vector, cr2);
    }
    if (!running)
        exception_panic((const struct exception_frame *)&frame->vector, cr2);
    struct task *task = &tasks[current];
    fpu_save(&task->fp);
    task->context = *frame;
    if (frame->vector == 128) {
        uint64_t call = frame->rax;
        syscall(task);
        if (task->state == READY && call != 0) {
            fpu_restore(&task->fp);
            return &task->context;
        }
    } else if (frame->vector == 32) {
        if (++task->ticks >= budget) {
            task->state = EXPIRED;
            console_write("TASK ");
            console_number(current);
            console_write(" CPU BUDGET EXPIRED\n");
        }
    } else {
        task->state = FAULTED;
        task->fault = frame->vector;
        task->error = frame->error;
        task->address = cr2;
        console_write("TASK ");
        console_number(current);
        console_write(" FAULT ");
        console_number(frame->vector);
        console_write(" CONTAINED\n");
    }
    return choose_next();
}

bool process_demo(void) {
    uint64_t flags, old_cr0;
    __asm__ volatile("pushfq; popq %0; cli; mov %%cr0,%1"
                     : "=r"(flags), "=r"(old_cr0)
                     :
                     : "memory");
    if (running) {
        if (flags & 512)
            __asm__ volatile("sti" : : : "memory");
        return false;
    }
    uint64_t before = pmm_available();
    task_count = TASKS;
    budget = 8;
    interactive = false;
    sent = received = denied = bad_pointers = unknown_calls = switches = 0;
    receiver_after_fault = mailbox_full = false;
    for (unsigned i = 0; i < TASKS; ++i)
        tasks[i] = (struct task){0};
    bool created = true;
    for (unsigned i = 0; i < TASKS; ++i) {
        if (!user_space_create(&tasks[i].space, user_program_start,
                               (size_t)(user_program_end - user_program_start), 0x100 + i)) {
            created = false;
            break;
        }
        fpu_init(&tasks[i].fp);
        tasks[i].context = (struct cpu_context){.rdi = i,
                                                .rip = USER_CODE,
                                                .cs = 0x33,
                                                .ss = 0x2b,
                                                .flags = 0x202,
                                                .rsp = USER_STACK + 4096 - 16};
    }
    if (created) {
        tasks[0].caps[1] = (struct capability){0, SEND};
        tasks[1].caps[1] = (struct capability){0, RECEIVE};
        current = 0;
        running = true;
        console_write("PROCESS TEST: 9 ISOLATED RING3 TASKS\n");
        fpu_begin();
        fpu_restore(&tasks[0].fp);
        exceptions_kernel_stack((uint64_t)&stacks[0][sizeof(stacks[0])]);
        paging_switch(tasks[0].space.root);
        process_enter(&tasks[0].context, &saved_kernel_rsp);
        fpu_end();
    }
    bool ok = created && tasks[0].state == FAULTED && tasks[0].fault == 14 && tasks[0].error == 5 &&
              tasks[0].address == 0xffffffff80000000ull && tasks[1].state == EXITED &&
              tasks[1].status == 0 && receiver_after_fault && tasks[2].state == EXPIRED &&
              tasks[2].ticks == 8 && switches > 0 && tasks[3].state == FAULTED &&
              tasks[3].fault == 14 && tasks[3].error == 7 && tasks[3].address == USER_CODE &&
              tasks[4].state == FAULTED && tasks[4].fault == 14 && tasks[4].error == 21 &&
              tasks[4].address == USER_STACK && tasks[5].state == FAULTED && tasks[5].fault == 13 &&
              tasks[5].error == 0 && tasks[6].state == FAULTED && tasks[6].fault == 14 &&
              tasks[6].error == 6 && tasks[6].address == USER_STACK - 8 &&
              tasks[7].state == EXITED && tasks[7].status == 0 && tasks[8].state == FAULTED &&
              tasks[8].fault == 13 && sent == 1 && received == 1 && denied == 2 &&
              bad_pointers == 3 && unknown_calls == 1;
    for (unsigned i = 0; i < TASKS; ++i)
        user_space_destroy(&tasks[i].space);
    ok = ok && pmm_available() == before;
    console_write(ok ? "PROCESS TEST PASS\n" : "PROCESS TEST FAIL\n");
    console_write("IPC SENT ");
    console_number(sent);
    console_write(" RECEIVED ");
    console_number(received);
    console_write(" DENIED ");
    console_number(denied);
    console_write(" BAD POINTERS ");
    console_number(bad_pointers);
    console_write(" SWITCHES ");
    console_number(switches);
    console_write("\n");
    if (flags & 512)
        __asm__ volatile("sti" : : : "memory");
    return ok;
}

bool process_services(const void *server, uint64_t server_size, const void *client,
                      uint64_t client_size) {
    uint64_t flags, old_cr0;
    __asm__ volatile("pushfq; popq %0; cli; mov %%cr0,%1" : "=r"(flags), "=r"(old_cr0)::"memory");
    if (running) {
        if (flags & 512)
            __asm__ volatile("sti");
        return false;
    }
    uint64_t before = pmm_available();
    task_count = 2;
    budget = 1000;
    interactive = false;
    for (unsigned i = 0; i < TASKS; ++i)
        tasks[i] = (struct task){0};
    for (unsigned i = 0; i < 2; ++i)
        endpoints[i] = (struct endpoint){.owner = i, .producer = 1 - i};
    sent = received = denied = bad_pointers = unknown_calls = switches = 0;
    uint64_t entry[2];
    bool created = user_space_elf(&tasks[0].space, server, server_size, &entry[0]) &&
                   user_space_elf(&tasks[1].space, client, client_size, &entry[1]);
    if (created) {
        for (unsigned i = 0; i < 2; ++i) {
            fpu_init(&tasks[i].fp);
            tasks[i].context = (struct cpu_context){.rip = entry[i],
                                                    .cs = 0x33,
                                                    .ss = 0x2b,
                                                    .flags = 0x202,
                                                    .rsp = USER_ELF_STACK_TOP - 16};
        }
        tasks[0].caps[1] = (struct capability){0, RECEIVE};
        tasks[0].caps[2] = (struct capability){1, SEND};
        tasks[1].caps[1] = (struct capability){0, SEND};
        tasks[1].caps[2] = (struct capability){1, RECEIVE};
        current = 0;
        running = true;
        console_write("SERVICES: STARTING EXTERNAL ELF PROGRAMS\n");
        fpu_begin();
        fpu_restore(&tasks[0].fp);
        exceptions_kernel_stack((uint64_t)&stacks[0][sizeof(stacks[0])]);
        paging_switch(tasks[0].space.root);
        process_enter(&tasks[0].context, &saved_kernel_rsp);
        fpu_end();
    }
    bool ok = created && tasks[0].state == EXITED && tasks[1].state == EXITED && !tasks[0].status &&
              !tasks[1].status && sent > 0 && sent == received && denied == 1 && bad_pointers == 3;
    for (unsigned i = 0; i < 2; ++i)
        user_space_destroy(&tasks[i].space);
    ok = ok && before == pmm_available();
    console_write(ok ? "SERVICE TEST PASS\n" : "SERVICE TEST FAIL\n");
    if (!ok) {
        console_write("SERVER STATUS ");
        console_number(tasks[0].status);
        console_write(" CLIENT STATUS ");
        console_number(tasks[1].status);
        console_write("\n");
    }
    if (flags & 512)
        __asm__ volatile("sti" ::: "memory");
    return ok;
}

bool process_filesystem_init(const void *server, uint64_t server_size, const void *client,
                             uint64_t client_size) {
    if (parked_server.space.root)
        return true;
    parked_server = (struct task){0};
    uint64_t entry;
    if (!user_space_elf(&parked_server.space, server, server_size, &entry))
        return false;
    fpu_init(&parked_server.fp);
    parked_server.context = (struct cpu_context){
        .rip = entry, .cs = 0x33, .ss = 0x2b, .flags = 0x202, .rsp = USER_ELF_STACK_TOP - 16};
    parked_server.caps[1] = (struct capability){0, RECEIVE};
    parked_server.caps[2] = (struct capability){1, SEND};
    parked_server.caps[3] = (struct capability){2, 4};
    file_client = client;
    file_client_size = client_size;
    return true;
}
bool process_file_request(const void *request, void *response) {
    uint64_t flags, old_cr0;
    __asm__ volatile("pushfq;popq %0;cli;mov %%cr0,%1" : "=r"(flags), "=r"(old_cr0)::"memory");
    bool ok = false;
    if (running || !parked_server.space.root)
        goto done;
    task_count = 2;
    budget = 1000;
    interactive = true;
    tasks[0] = parked_server;
    tasks[0].ticks = 0;
    tasks[1] = (struct task){0};
    uint64_t entry;
    if (!user_space_elf(&tasks[1].space, file_client, file_client_size, &entry))
        goto done;
    const uint64_t boot = USER_STACK + 0x100;
    if (!user_copy_to(&tasks[1].space, boot, request, 64)) {
        user_space_destroy(&tasks[1].space);
        goto done;
    }
    fpu_init(&tasks[1].fp);
    tasks[1].context = (struct cpu_context){.rip = entry,
                                            .cs = 0x33,
                                            .ss = 0x2b,
                                            .flags = 0x202,
                                            .rsp = USER_ELF_STACK_TOP - 16,
                                            .rdi = boot};
    tasks[1].caps[1] = (struct capability){0, SEND};
    tasks[1].caps[2] = (struct capability){1, RECEIVE};
    for (unsigned i = 0; i < 2; ++i)
        endpoints[i] = (struct endpoint){.owner = i, .producer = 1 - i};
    current = 0;
    running = true;
    fpu_begin();
    fpu_restore(&tasks[0].fp);
    exceptions_kernel_stack((uint64_t)&stacks[0][sizeof(stacks[0])]);
    paging_switch(tasks[0].space.root);
    process_enter(&tasks[0].context, &saved_kernel_rsp);
    fpu_end();
    ok = tasks[1].state == EXITED && !tasks[1].status && tasks[0].state == READY &&
         !endpoints[0].count && !endpoints[1].count &&
         user_copy_from(&tasks[1].space, response, boot, 64);
    parked_server = tasks[0];
    if (parked_server.state != READY) {
        user_space_destroy(&parked_server.space);
        console_write("ERROR: RAMFS SERVICE STOPPED\n");
    }
    user_space_destroy(&tasks[1].space);
done:
    interactive = false;
    if (flags & 512)
        __asm__ volatile("sti" ::: "memory");
    return ok;
}
bool process_run_file(const void *file, uint64_t size, const char *arguments, bool file_access) {
    uint64_t flags, old_cr0;
    __asm__ volatile("pushfq;popq %0;cli;mov %%cr0,%1" : "=r"(flags), "=r"(old_cr0)::"memory");
    bool ok = false;
    if (running || (file_access && !parked_server.space.root))
        goto done;
    uint64_t before = pmm_available(), entry;
    unsigned app = file_access ? 1 : 0;
    task_count = app + 1;
    budget = 1000;
    interactive = file_access;
    if (file_access) {
        tasks[0] = parked_server;
        tasks[0].ticks = 0;
    }
    tasks[app] = (struct task){0};
    if (!user_space_elf(&tasks[app].space, file, size, &entry)) {
        console_write("ERROR: invalid or unsupported NOS executable\n");
        goto done;
    }
    unsigned n = 0;
    while (arguments[n] && n < 80)
        ++n;
    if (n == 80 || !user_copy_to(&tasks[app].space, USER_STACK + 0x100, arguments, n + 1)) {
        user_space_destroy(&tasks[app].space);
        goto done;
    }
    fpu_init(&tasks[app].fp);
    tasks[app].context = (struct cpu_context){.rip = entry,
                                              .cs = 0x33,
                                              .ss = 0x2b,
                                              .flags = 0x202,
                                              .rsp = USER_ELF_STACK_TOP - 16,
                                              .rdi = USER_STACK + 0x100};
    if (file_access) {
        tasks[app].caps[1] = (struct capability){0, SEND};
        tasks[app].caps[2] = (struct capability){1, RECEIVE};
        for (unsigned i = 0; i < 2; ++i)
            endpoints[i] = (struct endpoint){.owner = i, .producer = 1 - i};
    }
    current = 0;
    running = true;
    fpu_begin();
    fpu_restore(&tasks[0].fp);
    exceptions_kernel_stack((uint64_t)&stacks[0][sizeof(stacks[0])]);
    paging_switch(tasks[0].space.root);
    process_enter(&tasks[0].context, &saved_kernel_rsp);
    fpu_end();
    ok = tasks[app].state == EXITED;
    if (ok) {
        console_write("EXIT CODE ");
        console_number(tasks[app].status);
        console_write("\n");
    } else
        console_write("PROGRAM TERMINATED - KERNEL ALIVE\n");
    if (file_access) {
        parked_server = tasks[0];
        if (parked_server.state != READY)
            user_space_destroy(&parked_server.space);
    }
    user_space_destroy(&tasks[app].space);
    ok = ok && pmm_available() == before;
    if (file_access && parked_server.space.root) {
        struct fs_message q = {.op = FS_ABORT}, r;
        /* A dead client cannot leave an open transaction or stale reply behind. */
        (void)process_file_request(&q, &r);
    }
done:
    interactive = false;
    if (flags & 512)
        __asm__ volatile("sti" ::: "memory");
    return ok;
}
