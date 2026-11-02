/* SysV AMD64, single-thread environment, no signal mask or CET shadow stack.
 * Save callee-saved GPRs and the caller's stack/instruction pointer.
 * The floating-point environment remains as it is at longjmp time (C 7.13.2.1). */
.text
.global setjmp
.type setjmp,@function
setjmp:
    movq %rbx,0(%rdi)
    movq %rbp,8(%rdi)
    movq %r12,16(%rdi)
    movq %r13,24(%rdi)
    movq %r14,32(%rdi)
    movq %r15,40(%rdi)
    leaq 8(%rsp),%rax
    movq %rax,48(%rdi)
    movq (%rsp),%rax
    movq %rax,56(%rdi)
    xorl %eax,%eax
    ret
.size setjmp,.-setjmp
.global longjmp
.type longjmp,@function
longjmp:
    movl %esi,%eax
    testl %eax,%eax
    jnz 1f
    incl %eax
1:
    movq 0(%rdi),%rbx
    movq 8(%rdi),%rbp
    movq 16(%rdi),%r12
    movq 24(%rdi),%r13
    movq 32(%rdi),%r14
    movq 40(%rdi),%r15
    movq 48(%rdi),%rsp
    jmp *56(%rdi)
.size longjmp,.-longjmp
.section .note.GNU-stack,"",@progbits
