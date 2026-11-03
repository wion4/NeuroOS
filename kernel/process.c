#include "process.h"
#include "../user/ai.h"
#include "../user/desktop.h"
#include "../user/fs.h"
#include "alloc.h"
#include "display.h"
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
enum state { READY, EXITED, FAULTED, EXPIRED, SUSPENDED, SLEEPING, WAITING };
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
    bool ipc_idle, desktop;
    uint64_t fault, error, address, status;
    uint64_t wake_tick;
};
static struct task tasks[TASKS];
static uint8_t stacks[TASKS][16384] __attribute__((aligned(16)));
static bool running, mailbox_full;
static unsigned current, denied, bad_pointers, unknown_calls, switches, sent, received;
static bool receiver_after_fault;
static uint64_t mailbox, saved_kernel_rsp;
static unsigned task_count = TASKS, budget = 8;
static bool interactive;
static bool desktop_launch;
static struct task suspended_desktop;
static struct desktop_launch_request launch_request;
static struct desktop_launch_result launch_result;
static struct desktop_launch_result *captured_output;
static uint64_t launch_destination;
static bool launch_pending;
static bool pending_ai;
static struct ai_session desktop_ai;
static struct task parked_server;
static struct task parked_ai;
static const void *ai_server_code, *ai_client_code, *ai_model;
static uint64_t ai_server_bytes, ai_client_bytes, ai_model_bytes;
static unsigned ai_restarts;
static bool ai_started;
static bool ai_job_active;
static struct ai_request ai_job_request;
static long desktop_ai_job(struct task *desktop, uint64_t op, uint64_t address, uint64_t bytes);
static void discard_ai_job(void);
void process_filesystem_inject_fault(void) {
    if (parked_server.space.root)
        parked_server.context.rip = 0;
}
static const void *file_client, *file_server;
static uint64_t file_client_size, file_server_size;
static unsigned automatic_restarts, fault_after_io;
static bool restart_limit_reported;
void process_filesystem_fault_after_io(unsigned operations) { fault_after_io = operations; }
struct message {
    uint8_t data[64];
    uint64_t size, sender;
};
struct endpoint {
    struct message queue[8];
    unsigned head, count, owner, producer;
};
/* Foreground FS uses 0/1. Background inference has a separate 2/3 pair. */
static struct endpoint endpoints[4];
extern char user_program_start[], user_program_end[];
extern void process_enter(struct cpu_context *, uint64_t *);
extern __attribute__((noreturn)) void process_leave(uint64_t);
extern void timer_irq(void);

static bool permits(const struct task *task, uint64_t handle, unsigned rights) {
    return handle < 4 && task->caps[handle].endpoint < 4 &&
           (task->caps[handle].rights & rights) == rights;
}
static bool task_alive(const struct task *task) {
    return task->state == READY || task->state == SLEEPING || task->state == SUSPENDED ||
           task->state == WAITING;
}
static void resume_wait(struct task *task) {
    if (task->state == WAITING) {
        task->state = READY;
        task->context.rax = (uint64_t)-11; /* A parked receive is retried by the service. */
    }
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
            if ((text[i] >= 32 && text[i] <= 126) || text[i] == '\n') {
                console_char(text[i]);
                if (captured_output && !task->caps[3].rights) {
                    if (captured_output->bytes + 1 < sizeof(captured_output->output))
                        captured_output->output[captured_output->bytes++] = text[i];
                    else
                        captured_output->truncated = 1;
                }
            }
        f->rax = f->rsi;
        break;
    }
    case 7:
    case 8:
    case 16: {
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
        if (call == 16 && !user_check_write(&task->space, f->rsi, (size_t)f->rdx)) {
            ++bad_pointers;
            f->rax = (uint64_t)-14;
            break;
        }
        if (send) {
            if (!task_alive(&tasks[ep->owner])) {
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
            if (task->caps[3].endpoint == 2 && task->caps[3].rights == 4 &&
                task->caps[f->rdi].endpoint == 1 && f->rdx == 64)
                task->ipc_idle = true; /* The trusted server has finished this request. */
        } else {
            if (!ep->count) {
                f->rax = (uint64_t)(task_alive(&tasks[ep->producer]) ? -11 : -32);
                if (call == 16 && (long)f->rax == -11)
                    task->state = WAITING;
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
            if (task->caps[3].endpoint == 2 && task->caps[3].rights == 4)
                task->ipc_idle = false;
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
        if ((op == 1 || op == 3) && fault_after_io && !--fault_after_io)
            f->rip = 0; /* Real user #PF after a word transfer, while DRQ is pending. */
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
    case 11: {
        if (!task->desktop) {
            f->rax = (uint64_t)-1;
            break;
        }
        if (f->rdi == 0) {
            struct desktop_input input;
            desktop_input_poll(&input);
            if (!user_copy_to(&task->space, f->rsi, &input, sizeof(input)))
                f->rax = (uint64_t)-14;
        } else if (f->rdi == 1)
            f->rax = (uint64_t)display_present(&task->space, f->rsi, f->rdx);
        else
            f->rax = (uint64_t)-22;
        break;
    }
    case 12: {
        if (!task->desktop) {
            f->rax = (uint64_t)-1;
            break;
        }
        /* Sleep this task, rather than the kernel, so other user tasks run. */
        if (f->rdi > 10) {
            f->rax = (uint64_t)-22;
            break;
        }
        task->ticks = 0;
        if (tasks[0].ipc_idle)
            tasks[0].ticks = 0;
        task->wake_tick = timer_ticks() + f->rdi;
        task->state = SLEEPING;
        break;
    }
    case 13: {
        if (!task->desktop) {
            f->rax = (uint64_t)-1;
            break;
        }
        if (ai_job_active) {
            f->rax = (uint64_t)-16;
            break;
        }
        if (f->rdx != sizeof(launch_request) ||
            !user_copy_from(&task->space, &launch_request, f->rdi, sizeof(launch_request))) {
            f->rax = (uint64_t)-14;
            break;
        }
        unsigned n = 0;
        while (n < 16 && launch_request.name[n])
            ++n;
        if (n < 5 || n > 15 || launch_request.name[n - 4] != '.' ||
            launch_request.name[n - 3] != 'n' || launch_request.name[n - 2] != 'o' ||
            launch_request.name[n - 1] != 's' || launch_request.arguments[79] ||
            launch_request.flags & ~NOS_LAUNCH_FILES) {
            f->rax = (uint64_t)-22;
            break;
        }
        if (current != 1 || (tasks[0].state != READY && tasks[0].state != WAITING) ||
            !tasks[0].ipc_idle || endpoints[0].count || endpoints[1].count) {
            f->rax = (uint64_t)-16;
            break;
        }
        launch_result = (struct desktop_launch_result){.kind = NOS_LAUNCH_LOAD_ERROR};
        if (!user_copy_to(&task->space, f->rsi, &launch_result, sizeof(launch_result))) {
            f->rax = (uint64_t)-14;
            break;
        }
        launch_destination = f->rsi;
        launch_pending = true;
        pending_ai = false;
        task->state = SUSPENDED;
        break;
    }
    case 14: {
        if (!task->desktop) {
            f->rax = (uint64_t)-1;
            break;
        }
        if (ai_job_active) {
            f->rax = (uint64_t)-16;
            break;
        }
        struct ai_request request;
        if (f->rdx != sizeof(request) ||
            !user_copy_from(&task->space, &request, f->rdi, sizeof(request))) {
            f->rax = (uint64_t)-14;
            break;
        }
        if ((request.op != AI_GENERATE && request.op != AI_UNLOAD) || request.prompt[55] ||
            (request.op == AI_GENERATE && (!request.steps || request.steps > AI_STEPS))) {
            f->rax = (uint64_t)-22;
            break;
        }
        if (current != 1 || endpoints[0].count || endpoints[1].count ||
            (tasks[0].state == READY && !tasks[0].ipc_idle)) {
            f->rax = (uint64_t)-16;
            break;
        }
        desktop_ai = (struct ai_session){.request = request};
        if (!user_copy_to(&task->space, f->rsi, &desktop_ai, sizeof(desktop_ai))) {
            f->rax = (uint64_t)-14;
            break;
        }
        launch_destination = f->rsi;
        launch_pending = true;
        pending_ai = true;
        task->state = SUSPENDED;
        break;
    }
    case 15:
        f->rax = (uint64_t)desktop_ai_job(task, f->rdi, f->rsi, f->rdx);
        break;
    case 17: {
        if (f->rdi != 3 || task->caps[3].endpoint != 3 || task->caps[3].rights != 16 || !ai_model) {
            f->rax = (uint64_t)-1;
            break;
        }
        if (f->rsi == ~0ull) {
            f->rax = ai_model_bytes;
            break;
        }
        if (f->rsi > ai_model_bytes) {
            f->rax = (uint64_t)-22;
            break;
        }
        uint64_t n = ai_model_bytes - f->rsi;
        if (n > 4096)
            n = 4096;
        f->rax = user_copy_to(&task->space, f->rdx, (const uint8_t *)ai_model + f->rsi, (size_t)n)
                     ? n
                     : (uint64_t)-14;
        break;
    }
    default:
        ++unknown_calls;
        f->rax = (uint64_t)-38;
        break;
    }
}
static struct cpu_context *choose_next(void) {
    /* A completed background client must not leave its server spinning while
       the desktop polls the result. Keep the server's resumable READY state. */
    bool job_done = ai_job_active && !task_alive(&tasks[3]);
    if (interactive && tasks[1].state != READY && tasks[1].state != SLEEPING &&
        tasks[1].state != WAITING) {
        running = false;
        paging_switch(paging_kernel_root());
        process_leave(saved_kernel_rsp);
    }
    for (;;) {
        bool sleeping = false;
        for (unsigned i = 0; i < task_count; ++i) {
            if (tasks[i].state == WAITING && !(job_done && i == 2)) {
                struct endpoint *ep = &endpoints[tasks[i].caps[tasks[i].context.rdi].endpoint];
                if (ep->count || !task_alive(&tasks[ep->producer])) {
                    tasks[i].state = READY;
                    tasks[i].context.rax = 16;
                    syscall(&tasks[i]);
                }
            }
            if (tasks[i].state == SLEEPING) {
                if (timer_ticks() >= tasks[i].wake_tick)
                    tasks[i].state = READY;
                else
                    sleeping = true;
            }
        }
        for (unsigned i = 1; i <= task_count; ++i) {
            unsigned next = (current + i) % task_count;
            if (tasks[next].state != READY || (job_done && next == 2))
                continue;
            if (next != current)
                ++switches;
            current = next;
            exceptions_kernel_stack((uint64_t)&stacks[next][sizeof(stacks[next])]);
            paging_switch(tasks[next].space.root);
            fpu_restore(&tasks[next].fp);
            return &tasks[next].context;
        }
        if (!sleeping)
            break;
        __asm__ volatile("sti; hlt; cli" ::: "memory");
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

static bool create_filesystem(void) {
    parked_server = (struct task){0};
    uint64_t entry;
    if (!user_space_elf(&parked_server.space, file_server, file_server_size, &entry))
        return false;
    fpu_init(&parked_server.fp);
    parked_server.context = (struct cpu_context){
        .rip = entry, .cs = 0x33, .ss = 0x2b, .flags = 0x202, .rsp = USER_ELF_STACK_TOP - 16};
    parked_server.caps[1] = (struct capability){0, RECEIVE};
    parked_server.caps[2] = (struct capability){1, SEND};
    parked_server.caps[3] = (struct capability){2, 4};
    return true;
}
bool process_filesystem_init(const void *server, uint64_t server_size, const void *client,
                             uint64_t client_size) {
    if (parked_server.space.root)
        return true;
    file_server = server;
    file_server_size = server_size;
    file_client = client;
    file_client_size = client_size;
    return create_filesystem();
}
bool process_filesystem_restart(void) {
    if (running || !file_server)
        return false;
    user_space_destroy(&parked_server.space);
    automatic_restarts = fault_after_io = 0;
    restart_limit_reported = false;
    return create_filesystem();
}
static void park_filesystem(void) {
    parked_server = tasks[0];
    resume_wait(&parked_server);
    if (parked_server.state == READY && parked_server.ipc_idle && !endpoints[0].count &&
        !endpoints[1].count)
        return;
    user_space_destroy(&parked_server.space);
    fault_after_io = 0;
    console_write(parked_server.state == READY ? "FILESYSTEM CLIENT SESSION DISCARDED\n"
                                               : "ERROR: FILESYSTEM SERVICE STOPPED\n");
}
bool process_file_request(const void *request, void *response) {
    uint64_t flags, old_cr0;
    __asm__ volatile("pushfq;popq %0;cli;mov %%cr0,%1" : "=r"(flags), "=r"(old_cr0)::"memory");
    bool ok = false;
    if (running)
        goto done;
    bool restarted = false;
    if (!parked_server.space.root) {
        if (!file_server)
            goto done;
        if (automatic_restarts >= 3) {
            if (!restart_limit_reported)
                console_write("FILESYSTEM RESTART LIMIT - USE restartfs\n");
            restart_limit_reported = true;
            goto done;
        }
        console_write("FILESYSTEM AUTO RESTART ATTEMPT ");
        console_number(++automatic_restarts);
        console_write(" OF 3\n");
        if (!create_filesystem())
            goto done;
        restarted = true;
    }
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
    resume_wait(&tasks[0]);
    ok = tasks[1].state == EXITED && !tasks[1].status && tasks[0].state == READY &&
         tasks[0].ipc_idle && !endpoints[0].count && !endpoints[1].count &&
         user_copy_from(&tasks[1].space, response, boot, 64);
    park_filesystem();
    user_space_destroy(&tasks[1].space);
    if (ok && restarted)
        console_write("FILESYSTEM SERVICE RECOVERED\n");
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
    uint64_t ai_pages_before = parked_ai.space.count + parked_ai.space.heap_pages;
    unsigned app = file_access ? 1 : 0;
    task_count = app + 1;
    budget = 1000;
    interactive = file_access;
    if (file_access) {
        tasks[0] = parked_server;
        tasks[0].ticks = 0;
    }
    tasks[app] = (struct task){0};
    tasks[app].desktop = desktop_launch;
    if (!user_space_elf(&tasks[app].space, file, size, &entry)) {
        console_write("ERROR: invalid or unsupported NOS executable\n");
        goto done;
    }
    unsigned n = 0;
    while (n < 80 && arguments[n])
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
    for (;;) {
        current = tasks[0].space.root && tasks[0].state == READY ? 0 : app;
        running = true;
        fpu_begin();
        fpu_restore(&tasks[current].fp);
        exceptions_kernel_stack((uint64_t)&stacks[current][sizeof(stacks[current])]);
        paging_switch(tasks[current].space.root);
        process_enter(&tasks[current].context, &saved_kernel_rsp);
        fpu_end();
        if (!tasks[app].desktop || tasks[app].state != SUSPENDED || !launch_pending)
            break;
        suspended_desktop = tasks[app];
        park_filesystem();
        /* Preserve the desktop address space and FPU state. The bounded child
           runs as an ordinary task with only explicitly requested FS rights. */
        launch_pending = false;
        desktop_launch = false;
        if (pending_ai) {
            if (desktop_ai.request.op == AI_UNLOAD)
                desktop_ai.status = process_ai_reset() ? 0 : -16;
            else if (!process_ai_request(&desktop_ai))
                desktop_ai.status = -5;
        } else {
            captured_output = &launch_result;
            (void)filesystem_execute(launch_request.name, launch_request.arguments,
                                     (launch_request.flags & NOS_LAUNCH_FILES) != 0);
            captured_output = 0;
        }
        desktop_launch = true;
        tasks[1] = suspended_desktop;
        suspended_desktop = (struct task){0};
        tasks[1].state = READY;
        tasks[1].ticks = 0;
        tasks[1].context.rax =
            user_copy_to(&tasks[1].space, launch_destination,
                         pending_ai ? (const void *)&desktop_ai : (const void *)&launch_result,
                         pending_ai ? sizeof(desktop_ai) : sizeof(launch_result))
                ? 0
                : (uint64_t)-14;
        tasks[0] = parked_server;
        tasks[0].ticks = 0;
        task_count = 2;
        budget = 1000;
        interactive = true;
        for (unsigned i = 0; i < 2; ++i)
            endpoints[i] = (struct endpoint){.owner = i, .producer = 1 - i};
        if (!tasks[0].space.root) {
            /* Recovery is a new request, never a replay of a child write. */
            struct fs_message q = {.op = FS_LIST}, r;
            struct task saved_desktop = tasks[1];
            (void)process_file_request(&q, &r);
            tasks[1] = saved_desktop;
            tasks[0] = parked_server;
            task_count = 2;
            budget = 1000;
            interactive = true;
            for (unsigned i = 0; i < 2; ++i)
                endpoints[i] = (struct endpoint){.owner = i, .producer = 1 - i};
        }
        if (!tasks[0].space.root)
            tasks[0].state = FAULTED;
        console_write("DESKTOP CHILD FINISHED\n");
    }
    ok = tasks[app].state == EXITED;
    if (tasks[app].desktop && ai_job_active) {
        discard_ai_job();
        ai_started = false;
        ai_restarts = 0;
    }
    if (captured_output) {
        captured_output->kind = tasks[app].state == EXITED    ? NOS_LAUNCH_EXITED
                                : tasks[app].state == EXPIRED ? NOS_LAUNCH_TIMED_OUT
                                                              : NOS_LAUNCH_FAULTED;
        captured_output->exit_code = tasks[app].status;
        captured_output->fault = (uint32_t)tasks[app].fault;
    }
    if (ok) {
        console_write("EXIT CODE ");
        console_number(tasks[app].status);
        console_write("\n");
    } else
        console_write("PROGRAM TERMINATED - KERNEL ALIVE\n");
    if (file_access) {
        park_filesystem();
    }
    user_space_destroy(&tasks[app].space);
    if (file_access && file_server) {
        struct fs_message q = {.op = FS_ABORT}, r;
        /* A dead client cannot leave an open transaction or stale reply behind. */
        (void)process_file_request(&q, &r);
    }
    uint64_t ai_pages_after = parked_ai.space.count + parked_ai.space.heap_pages;
    ok = ok && pmm_available() + ai_pages_after == before + ai_pages_before;
done:
    interactive = false;
    if (flags & 512)
        __asm__ volatile("sti" ::: "memory");
    return ok;
}
bool process_run_desktop(const void *file, uint64_t size) {
    desktop_launch = true;
    bool ok = process_run_file(file, size, "", true);
    desktop_launch = false;
    return ok;
}
bool process_ai_config(const void *server, uint64_t server_bytes, const void *client,
                       uint64_t client_bytes, const void *model, uint64_t model_bytes) {
    if (running || parked_ai.space.root || !server || !client || !model || !server_bytes ||
        !client_bytes || !model_bytes || model_bytes > 2 * 1024 * 1024)
        return false;
    ai_server_code = server;
    ai_server_bytes = server_bytes;
    ai_client_code = client;
    ai_client_bytes = client_bytes;
    ai_model = model;
    ai_model_bytes = model_bytes;
    return true;
}
bool process_ai_reset(void) {
    if (running)
        return false;
    user_space_destroy(&parked_ai.space);
    parked_ai = (struct task){0};
    ai_restarts = 0;
    ai_started = false;
    return true;
}
void process_ai_inject_fault(void) {
    if (parked_ai.space.root)
        parked_ai.context.rip = 0;
}
static bool create_ai(void) {
    if (!ai_server_code)
        return false;
    if (ai_started) {
        if (ai_restarts >= 3) {
            console_write("AI RESTART LIMIT - USE ai-reset\n");
            return false;
        }
        ++ai_restarts;
    }
    ai_started = true;
    uint64_t entry;
    parked_ai = (struct task){0};
    if (!user_space_elf(&parked_ai.space, ai_server_code, ai_server_bytes, &entry))
        return false;
    fpu_init(&parked_ai.fp);
    parked_ai.context = (struct cpu_context){
        .rip = entry, .cs = 0x33, .ss = 0x2b, .flags = 0x202, .rsp = USER_ELF_STACK_TOP - 16};
    parked_ai.caps[1] = (struct capability){0, RECEIVE};
    parked_ai.caps[2] = (struct capability){1, SEND};
    parked_ai.caps[3] = (struct capability){3, 16};
    return true;
}
static void discard_ai_job(void) {
    if (!ai_job_active)
        return;
    /* Called only on the desktop/kernel stack, never on either freed task. */
    user_space_destroy(&tasks[2].space);
    user_space_destroy(&tasks[3].space);
    tasks[2] = tasks[3] = (struct task){0};
    endpoints[2] = endpoints[3] = (struct endpoint){0};
    ai_job_active = false;
    task_count = 2;
}
static long desktop_ai_job(struct task *desktop, uint64_t op, uint64_t address, uint64_t bytes) {
    if (!desktop->desktop || current != 1)
        return -1;
    if (!ai_server_code)
        return -19;
    if (op == AI_JOB_START) {
        if (ai_job_active || task_count != 2)
            return -16;
        struct ai_request request;
        if (bytes != sizeof(request) ||
            !user_copy_from(&desktop->space, &request, address, sizeof(request)))
            return -14;
        if (request.op != AI_GENERATE || !request.steps || request.steps > AI_STEPS ||
            request.prompt[55])
            return -22;
        uint64_t entry;
        tasks[3] = (struct task){0};
        if (!user_space_elf(&tasks[3].space, ai_client_code, ai_client_bytes, &entry))
            return -12;
        desktop_ai = (struct ai_session){.request = request};
        if (!user_copy_to(&tasks[3].space, USER_STACK + 0x100, &desktop_ai, sizeof(desktop_ai)) ||
            (!parked_ai.space.root && !create_ai())) {
            user_space_destroy(&tasks[3].space);
            return -5;
        }
        tasks[2] = parked_ai;
        parked_ai = (struct task){0}; /* Transfer ownership, not another address space. */
        tasks[2].ticks = 0;
        tasks[2].caps[1] = (struct capability){2, RECEIVE};
        tasks[2].caps[2] = (struct capability){3, SEND};
        fpu_init(&tasks[3].fp);
        tasks[3].context = (struct cpu_context){.rip = entry,
                                                .cs = 0x33,
                                                .ss = 0x2b,
                                                .flags = 0x202,
                                                .rsp = USER_ELF_STACK_TOP - 16,
                                                .rdi = USER_STACK + 0x100};
        tasks[3].caps[1] = (struct capability){2, SEND};
        tasks[3].caps[2] = (struct capability){3, RECEIVE};
        endpoints[2] = (struct endpoint){.owner = 2, .producer = 3};
        endpoints[3] = (struct endpoint){.owner = 3, .producer = 2};
        ai_job_request = request;
        ai_job_active = true;
        task_count = 4;
        return 0;
    }
    if (op == AI_JOB_POLL) {
        if (bytes != sizeof(desktop_ai))
            return -22;
        if (!ai_job_active)
            return -2;
        if (task_alive(&tasks[3]))
            return 0;
        desktop_ai = (struct ai_session){.request = ai_job_request, .status = -5};
        bool ok = tasks[3].state == EXITED && !tasks[3].status &&
                  (tasks[2].state == READY || tasks[2].state == WAITING) && !endpoints[2].count &&
                  !endpoints[3].count;
        if (ok)
            ok = user_copy_from(&tasks[3].space, &desktop_ai, USER_STACK + 0x100,
                                sizeof(desktop_ai));
        /* An invalid destination must not consume the result or free its tasks. */
        if (!user_copy_to(&desktop->space, address, &desktop_ai, sizeof(desktop_ai)))
            return -14;
        if (ok) {
            parked_ai = tasks[2];
            resume_wait(&parked_ai);
            tasks[2] = (struct task){0};
        } else
            console_write("AI SERVICE STOPPED - KERNEL ALIVE\n");
        discard_ai_job();
        return 1;
    }
    if (op == AI_JOB_CANCEL || op == AI_JOB_UNLOAD) {
        if (bytes || address)
            return -22;
        discard_ai_job();
        user_space_destroy(&parked_ai.space);
        parked_ai = (struct task){0};
        ai_started = false;
        ai_restarts = 0;
        return 0;
    }
    return -22;
}
bool process_ai_request(struct ai_session *session) {
    uint64_t flags;
    __asm__ volatile("pushfq;popq %0;cli" : "=r"(flags)::"memory");
    bool ok = false;
    if (running || !ai_server_code || (!parked_ai.space.root && !create_ai()))
        goto done;
    tasks[0] = parked_ai;
    tasks[0].ticks = 0;
    tasks[0].caps[1] = (struct capability){0, RECEIVE};
    tasks[0].caps[2] = (struct capability){1, SEND};
    tasks[1] = (struct task){0};
    uint64_t entry;
    if (!user_space_elf(&tasks[1].space, ai_client_code, ai_client_bytes, &entry))
        goto done;
    const uint64_t boot = USER_STACK + 0x100;
    if (!user_copy_to(&tasks[1].space, boot, session, sizeof(*session))) {
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
    task_count = 2;
    budget = 1000;
    interactive = true;
    current = 0;
    running = true;
    fpu_begin();
    fpu_restore(&tasks[0].fp);
    exceptions_kernel_stack((uint64_t)&stacks[0][sizeof(stacks[0])]);
    paging_switch(tasks[0].space.root);
    process_enter(&tasks[0].context, &saved_kernel_rsp);
    fpu_end();
    resume_wait(&tasks[0]);
    ok = tasks[1].state == EXITED && !tasks[1].status && tasks[0].state == READY &&
         !endpoints[0].count && !endpoints[1].count &&
         user_copy_from(&tasks[1].space, session, boot, sizeof(*session));
    parked_ai = tasks[0];
    if (!ok) {
        user_space_destroy(&parked_ai.space);
        console_write("AI SERVICE STOPPED - KERNEL ALIVE\n");
    }
    user_space_destroy(&tasks[1].space);
done:
    interactive = false;
    if (flags & 512)
        __asm__ volatile("sti" ::: "memory");
    return ok;
}
