.section .text
.code64

.equ USER_DATA_SEL, 0x1b
.equ USER_CODE_SEL, 0x23

.extern x86_tss
.extern program_syscall
.extern program_exception

.global x86_load_tables
.type x86_load_tables, @function
x86_load_tables:
    lgdt (%rdi)
    lidt (%rsi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    mov $0x10, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %dx, %ax
    ltr %ax
    ret

.section .bss
.align 8
x86_user_depth:
    .quad 0
x86_kernel_rsp:
    .skip 64
x86_kernel_cr3:
    .skip 64
x86_kernel_rsp0:
    .skip 64

.section .text
.global x86_enter_user_asm
.type x86_enter_user_asm, @function
x86_enter_user_asm:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov x86_user_depth(%rip), %r8
    mov %rsp, x86_kernel_rsp(,%r8,8)
    mov %cr3, %rax
    mov %rax, x86_kernel_cr3(,%r8,8)
    mov x86_tss+4(%rip), %rax
    mov %rax, x86_kernel_rsp0(,%r8,8)
    inc %r8
    mov %r8, x86_user_depth(%rip)
    mov %rsp, x86_tss+4(%rip)
    mov %rcx, %cr3
    pushq $USER_DATA_SEL
    push %rsi
    pushfq
    andq $~0x3000, (%rsp)
    pushq $USER_CODE_SEL
    push %rdi
    mov %rdx, %rdi
    iretq

.global x86_leave_user_asm
.type x86_leave_user_asm, @function
x86_leave_user_asm:
    movzbl %dil, %eax
    mov x86_user_depth(%rip), %r8
    dec %r8
    mov %r8, x86_user_depth(%rip)
    mov x86_kernel_cr3(,%r8,8), %rcx
    mov %rcx, %cr3
    mov x86_kernel_rsp(,%r8,8), %rsp
    mov x86_kernel_rsp0(,%r8,8), %rcx
    mov %rcx, x86_tss+4(%rip)
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret

.global x86_syscall_stub
.type x86_syscall_stub, @function
x86_syscall_stub:
    cld
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rcx, %r8
    mov %rdx, %rcx
    mov %rsi, %rdx
    mov %rdi, %rsi
    mov %rax, %rdi
    call program_syscall
    cmp $0x100, %rax
    je 2f
    cmp $0x101, %rax
    je 3f
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    iretq
2:
    mov $1, %edi
    jmp x86_leave_user_asm
3:
    xor %edi, %edi
    jmp x86_leave_user_asm

.macro ISR_NO_ERROR vector
    .global x86_exception_\vector
x86_exception_\vector:
    pushq $0
    pushq $\vector
    jmp x86_exception_common
.endm

.macro ISR_ERROR vector
    .global x86_exception_\vector
x86_exception_\vector:
    pushq $\vector
    jmp x86_exception_common
.endm

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR 8
ISR_NO_ERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NO_ERROR 31

x86_exception_common:
    cld
    mov (%rsp), %rdi
    mov 8(%rsp), %rsi
    mov 24(%rsp), %rdx
    call program_exception
    cli
4:
    hlt
    jmp 4b

.section .rodata
.align 8
.global x86_exception_stubs
x86_exception_stubs:
    .quad x86_exception_0, x86_exception_1, x86_exception_2, x86_exception_3
    .quad x86_exception_4, x86_exception_5, x86_exception_6, x86_exception_7
    .quad x86_exception_8, x86_exception_9, x86_exception_10, x86_exception_11
    .quad x86_exception_12, x86_exception_13, x86_exception_14, x86_exception_15
    .quad x86_exception_16, x86_exception_17, x86_exception_18, x86_exception_19
    .quad x86_exception_20, x86_exception_21, x86_exception_22, x86_exception_23
    .quad x86_exception_24, x86_exception_25, x86_exception_26, x86_exception_27
    .quad x86_exception_28, x86_exception_29, x86_exception_30, x86_exception_31

.section .text
.extern scheduler_timer_interrupt
.global scheduler_timer_interrupt_stub
.type scheduler_timer_interrupt_stub, @function
scheduler_timer_interrupt_stub:
    cld
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rbx
    andq $-16, %rsp
    call scheduler_timer_interrupt
    mov %rbx, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    iretq
