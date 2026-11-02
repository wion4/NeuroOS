#ifndef NEUROOS_SHELL_H
#define NEUROOS_SHELL_H
#include <stdbool.h>
#include <stdint.h>
void console_write(const char *text);
void console_char(char c);
void console_number(uint64_t value);
void console_clear(void);
bool kernel_selftest(void);
bool kernel_services(void);
bool filesystem_command(const char *command, char *arguments);
__attribute__((noreturn)) void kernel_halt(void);
__attribute__((noreturn)) void shell_run(void);
bool kernel_service_recovery_test(void);
bool kernel_install_program(const char *name);
bool kernel_compile(const char *arguments);
bool kernel_fs3_test(const char *arguments);
bool kernel_boot_module(unsigned index,const void **address,uint64_t *bytes);
bool kernel_install_sdk(void);
bool filesystem_store(const char *name,const void *data,unsigned bytes);
void editor_run(const char *name);
#endif
