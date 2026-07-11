.section .multiboot, "a"
.align 4
.long 0x1BADB002
.long 0x00000007
.long -(0x1BADB002 + 0x00000007)
.long 0
.long 0
.long 0
.long 0
.long 0
.long 0
.long 1280
.long 768
.long 32

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.align 4096
pml4_table:
.skip 4096

.align 4096
pdpt_table:
.skip 4096

.align 4096
pd_table0:
.skip 4096

.align 4096
pd_table1:
.skip 4096

.align 4096
pd_table2:
.skip 4096

.align 4096
pd_table3:
.skip 4096

.section .rodata
.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdt64_end:

gdt64_descriptor:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.equ CODE64_SEL, 0x08
.equ DATA64_SEL, 0x10
.equ MSR_EFER, 0xC0000080

.section .text
.code32
.global _start
.type _start, @function
.extern kernel_main

_start:
    cli
    mov $stack_top, %esp

    call setup_page_tables

    lgdt gdt64_descriptor

    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    mov $pml4_table, %eax
    mov %eax, %cr3

    mov $MSR_EFER, %ecx
    rdmsr
    or $0x00000100, %eax
    wrmsr

    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    ljmp $CODE64_SEL, $long_mode_entry

setup_page_tables:
    movl $(pdpt_table + 0x00000003), pml4_table
    movl $0x00000000, pml4_table + 4

    movl $(pd_table0 + 0x00000003), pdpt_table
    movl $0x00000000, pdpt_table + 4
    movl $(pd_table1 + 0x00000003), pdpt_table + 8
    movl $0x00000000, pdpt_table + 12
    movl $(pd_table2 + 0x00000003), pdpt_table + 16
    movl $0x00000000, pdpt_table + 20
    movl $(pd_table3 + 0x00000003), pdpt_table + 24
    movl $0x00000000, pdpt_table + 28

    xor %ecx, %ecx
    mov $pd_table0, %edi
    call map_pd_table
    mov $pd_table1, %edi
    call map_pd_table
    mov $pd_table2, %edi
    call map_pd_table
    mov $pd_table3, %edi
    call map_pd_table
    ret

map_pd_table:
    xor %esi, %esi

map_pd_entry:
    mov %ecx, %eax
    shl $21, %eax
    or $0x00000083, %eax
    mov %eax, (%edi, %esi, 8)
    mov $0x00000000, %eax
    mov %eax, 4(%edi, %esi, 8)
    inc %ecx
    inc %esi
    cmp $512, %esi
    jne map_pd_entry
    ret

.code64
long_mode_entry:
    mov $DATA64_SEL, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    mov $stack_top, %rsp
    xor %rbp, %rbp

    mov %ebx, %edi
    call kernel_main

hang:
    cli
    hlt
    jmp hang
