#ifndef NEUROOS_PROCESS_H
#define NEUROOS_PROCESS_H
#include <stdbool.h>
#include <stdint.h>
struct cpu_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbx, rbp, rdi, rsi, rdx, rcx, rax;
    uint64_t vector, error, rip, cs, flags, rsp, ss;
};
struct cpu_context *process_trap(struct cpu_context *frame, uint64_t cr2);
bool process_demo(void);
bool process_services(const void *server, uint64_t server_size, const void *client,
                      uint64_t client_size);
bool process_filesystem_init(const void *server, uint64_t server_size, const void *client,
                             uint64_t client_size);
bool process_file_request(const void *request, void *response);
void process_filesystem_inject_fault(void);
void process_filesystem_fault_after_io(unsigned operations);
bool process_filesystem_restart(void);
bool process_run_file(const void *file, uint64_t size, const char *arguments, bool file_access);
bool process_run_desktop(const void *file, uint64_t size);
#endif
