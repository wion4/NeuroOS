.section .text.start
.global _start
_start:
    xor %ebp,%ebp
    call user_main
    mov %eax,%edi
    mov $1,%eax
    int $0x80
    ud2
