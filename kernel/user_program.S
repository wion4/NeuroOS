# Position-independent ring-3 test program. Identical bytes in private address spaces.
# int 0x80: RAX=call, RDI=arg1, RSI=arg2; result RAX, IPC payload RDX.
.section .rodata
.balign 16
.global user_program_start, user_program_end
user_program_start:
    movq %rdi, %r12
    cmpq $0, %r12
    je sender
    cmpq $1, %r12
    je receiver
    cmpq $2, %r12
    je spinner
    cmpq $3, %r12
    je readonly_fault
    cmpq $4, %r12
    je nx_fault
    cmpq $5, %r12
    je privilege_fault
    cmpq $6, %r12
    je guard_fault
    cmpq $7, %r12
    je simd_fault
    cmpq $8, %r12
    je io_fault
    jmp failed
sender:
    movq $9, %rax
    movq $3, %rdi
    movq $0x1f7, %rsi
    int $0x80
    cmpq $-1, %rax
    jne failed
    movq $0x1111, 0x600000
    movq $2, %rax
    movq $99, %rdi
    int $0x80
    cmpq $-1, %rax
    jne failed
    movq $3, %rax
    movq $1, %rdi
    int $0x80
    cmpq $-1, %rax
    jne failed
    movq $6, %rax
    movabs $0xffffffff80000000, %rdi
    movq $16, %rsi
    int $0x80
    cmpq $-14, %rax
    jne failed
    movq $6, %rax
    movq $0x800ff0, %rdi
    movq $32, %rsi
    int $0x80
    cmpq $-14, %rax
    jne failed
    movq $6, %rax
    movq $-16, %rdi
    movq $32, %rsi
    int $0x80
    cmpq $-14, %rax
    jne failed
    movq $999, %rax
    int $0x80
    cmpq $-38, %rax
    jne failed
    movq $6, %rax
    leaq sender_message(%rip), %rdi
    movq $(sender_end-sender_message), %rsi
    int $0x80
    movq $2, %rax
    movq $1, %rdi
    movq $0x4e455552, %rsi
    int $0x80
    testq %rax, %rax
    jne failed
    movabs $0xffffffff80000000, %rax
    movq (%rax), %rax
    jmp failed
receiver:
    pmovmskb %xmm0, %eax
    testl %eax,%eax
    jne failed
    fld1
    cmpq $0x101, 0x600000
    jne failed
receive_again:
    movq $3, %rax
    movq $1, %rdi
    int $0x80
    cmpq $-11, %rax
    jne received
    xorq %rax, %rax
    int $0x80
    jmp receive_again
received:
    testq %rax, %rax
    jne failed
    cmpq $0x4e455552, %rdx
    jne failed
    cmpq $0x101, 0x600000
    jne failed
    movq $5, %rax
    int $0x80
    movq %rax, %r13
wait_for_peer:
    xorq %rax, %rax
    int $0x80
    cmpq $1, %r12
    jne failed
    movq $5, %rax
    int $0x80
    subq %r13, %rax
    cmpq $5, %rax
    jb wait_for_peer
    pmovmskb %xmm0,%eax
    testl %eax,%eax
    jne failed
    fistpl 0x600000
    cmpl $1,0x600000
    jne failed
    movq $6, %rax
    leaq receiver_message(%rip), %rdi
    movq $(receiver_end-receiver_message), %rsi
    int $0x80
    movq $1, %rax
    xorq %rdi, %rdi
    int $0x80
    jmp failed
spinner:
    pause
    jmp spinner
readonly_fault:
    movb $0, 0x400000
    jmp failed
nx_fault:
    movb $0xc3, 0x800000
    movq $0x800000, %rax
    jmp *%rax
privilege_fault:
    cli
    jmp failed
guard_fault:
    movq $0x800000, %rsp
    pushq $0
    jmp failed
simd_fault:
    fld1
    fld1
    faddp
    pcmpeqd %xmm0,%xmm0
    movq $5,%r14
fpu_yield:
    xorq %rax,%rax
    int $0x80
    decq %r14
    jnz fpu_yield
    movq $5,%rax
    int $0x80
    movq %rax,%r15
fpu_preempt:
    movq $5,%rax
    int $0x80
    subq %r15,%rax
    cmpq $3,%rax
    jb fpu_preempt
    pmovmskb %xmm0,%eax
    cmpl $65535,%eax
    jne failed
    fistpl 0x600000
    cmpl $2,0x600000
    jne failed
    movq $1,%rax
    xorq %rdi,%rdi
    int $0x80
    jmp failed
io_fault:
    movw $0x3f8, %dx
    outb %al, %dx
    jmp failed
failed:
    movq $1, %rax
    movq $1, %rdi
    int $0x80
    ud2
sender_message:
    .ascii "RIGHTS AND POINTER CHECKS PASSED\n"
sender_end:
receiver_message:
    .ascii "IPC RECEIVED AND PEER FAILURE SURVIVED\n"
receiver_end:
user_program_end:
