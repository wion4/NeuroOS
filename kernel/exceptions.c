#include "exceptions.h"
#include <stddef.h>
#include "paging.h"
struct __attribute__((packed)) descriptor { uint16_t limit; uint64_t base; };
struct __attribute__((packed)) gate {
    uint16_t low, selector;
    uint8_t ist, attributes;
    uint16_t middle;
    uint32_t high, reserved;
};
struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
};
_Static_assert(sizeof(struct gate) == 16, "IDT gate layout");
_Static_assert(sizeof(struct tss64) == 104, "TSS layout");
_Static_assert(offsetof(struct tss64, ist) == 36, "IST offset");
_Static_assert(sizeof(struct exception_frame) == 56, "exception stack layout");
static uint64_t gdt[7] __attribute__((aligned(16))) = {
    0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0, 0,
    0x00cff2000000ffff, 0x00affa000000ffff
};
static struct gate idt[256] __attribute__((aligned(16)));
static struct tss64 tss;
static uint8_t emergency[3][16384] __attribute__((aligned(16)));
extern void *isr_table[256];
extern void isr_timer(void), isr_spurious(void);
extern void gdt_load(const struct descriptor *gdtr);
void exceptions_kernel_stack(uint64_t top) { tss.rsp[0] = top; }

void exceptions_init(void) {
    __asm__ volatile ("cli");
    for (unsigned i = 0; i < 3; ++i)
        tss.ist[i] = (uint64_t)&emergency[i][sizeof(emergency[i])];
    tss.iomap = sizeof(tss); /* No I/O bitmap. User mode is not enabled. */
    uint64_t base = (uint64_t)&tss;
    gdt[3] = (sizeof(tss) - 1) | ((base & 0xffffff) << 16) |
             (0x89ull << 40) | (((base >> 24) & 0xff) << 56);
    gdt[4] = base >> 32;
    struct descriptor gdtr = {sizeof(gdt) - 1, (uint64_t)gdt};
    gdt_load(&gdtr);
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x18) : "memory");
    for (unsigned i = 0; i < 256; ++i) {
        uint64_t addr = (uint64_t)isr_table[i];
        if (i == 32) addr = (uint64_t)isr_timer;
        if (i == 39) addr = (uint64_t)isr_spurious;
        idt[i] = (struct gate){
            (uint16_t)addr, 0x08, i == 8 ? 1 : i == 2 ? 2 : i == 18 ? 3 : 0,
            i == 128 ? 0xee : 0x8e, (uint16_t)(addr >> 16), (uint32_t)(addr >> 32), 0
        };
    }
    struct descriptor idtr = {sizeof(idt) - 1, (uint64_t)idt};
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}

int exception_on_df_stack(const struct exception_frame *frame) {
    uintptr_t p = (uintptr_t)frame;
    return p >= (uintptr_t)emergency[0] &&
           p <= (uintptr_t)&emergency[0][sizeof(emergency[0])] - sizeof(*frame);
}

void exception_test_trigger(void) {
#if NEUROOS_FAULT_TEST == 6
    __asm__ volatile (".global test_fault_rip\ntest_fault_rip: ud2");
#elif NEUROOS_FAULT_TEST == 14
    __asm__ volatile ("movabs $0xffffc00000000000, %%rax\n.global test_fault_rip\ntest_fault_rip: movq (%%rax), %%rax" : : : "rax", "memory");
#elif NEUROOS_FAULT_TEST == 13 || NEUROOS_FAULT_TEST == 8
#if NEUROOS_FAULT_TEST == 8
    /* #GP delivery raises #NP, producing #DF on the dedicated IST stack. */
    idt[13].attributes = 0x0e;
    __asm__ volatile ("" : : : "memory");
#endif
    __asm__ volatile ("movw $0x38, %%ax\n.global test_fault_rip\ntest_fault_rip: movw %%ax, %%ds" : : : "rax", "memory");
#endif
}
