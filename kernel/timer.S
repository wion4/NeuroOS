.section .text
.global isr_timer
isr_timer:
    pushq $0
    pushq $32
    jmp isr_common
.global isr_spurious
isr_spurious:
    iretq
