#ifndef NEUROOS_EXCEPTIONS_H
#define NEUROOS_EXCEPTIONS_H
#include <stdint.h>
struct exception_frame { uint64_t vector, error, rip, cs, flags, rsp, ss; };
void exceptions_init(void);
void exceptions_kernel_stack(uint64_t top);
void exception_test_trigger(void);
int exception_on_df_stack(const struct exception_frame *frame);
__attribute__((noreturn)) void exception_panic(const struct exception_frame *frame, uint64_t cr2);
#endif
