/* Exercise every callee-saved GPR independently of the C optimizer. */
.text
.global test_jump_registers
.type test_jump_registers,@function
test_jump_registers:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $72,%rsp
    movabsq $0x1234567800000001,%rbx
    movabsq $0x1234567800000002,%rbp
    movabsq $0x1234567800000003,%r12
    movabsq $0x1234567800000004,%r13
    movabsq $0x1234567800000005,%r14
    movabsq $0x1234567800000006,%r15
    movq %rsp,%rdi
    call setjmp
    testl %eax,%eax
    jnz resumed
    xorl %ebx,%ebx
    xorl %ebp,%ebp
    xorl %r12d,%r12d
    xorl %r13d,%r13d
    xorl %r14d,%r14d
    xorl %r15d,%r15d
    movq %rsp,%rdi
    movl $17,%esi
    call longjmp
    ud2
resumed:
    cmpl $17,%eax
    jne bad
.macro check register,value
    movabsq $\value,%rdx
    cmpq %rdx,%\register
    jne bad
.endm
    check rbx,0x1234567800000001
    check rbp,0x1234567800000002
    check r12,0x1234567800000003
    check r13,0x1234567800000004
    check r14,0x1234567800000005
    check r15,0x1234567800000006
    xorl %eax,%eax
    jmp done
bad:
    movl $1,%eax
done:
    addq $72,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
.size test_jump_registers,.-test_jump_registers
.section .note.GNU-stack,"",@progbits
