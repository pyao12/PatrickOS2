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
pdpt_tables:
.skip 1048576

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
    # Map the complete 128 TiB low canonical address range. Every PDPT
    # entry is a 1 GiB page, so no per-2 MiB page-directory tables are needed.
    xor %ecx, %ecx
    mov $pml4_table, %edi
    mov $pdpt_tables, %edx

map_pml4_entry:
    mov %edx, %eax
    or $0x00000003, %eax
    mov %eax, (%edi)
    movl $0x00000000, 4(%edi)
    add $4096, %edx
    add $8, %edi
    inc %ecx
    cmp $256, %ecx
    jne map_pml4_entry

    xor %ecx, %ecx
    mov $pdpt_tables, %edi

map_pdpt_entry:
    mov %ecx, %eax
    shl $30, %eax
    or $0x00000083, %eax
    mov %eax, (%edi)
    mov %ecx, %eax
    shr $2, %eax
    mov %eax, 4(%edi)
    add $8, %edi
    inc %ecx
    cmp $131072, %ecx
    jne map_pdpt_entry
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
