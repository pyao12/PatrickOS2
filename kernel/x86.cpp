#include <x86.h>

namespace {

constexpr ui16 kernel_code_selector = 0x08;
constexpr ui16 tss_selector         = 0x28;
constexpr ui8  interrupt_gate       = 0x8e;
constexpr ui8  user_interrupt_gate  = 0xee;

struct table_descriptor_t {
    ui16 limit;
    ui64 base;
} __attribute__((packed));

struct idt_entry_t {
    ui16 offset_low;
    ui16 selector;
    ui8  ist;
    ui8  attributes;
    ui16 offset_middle;
    ui32 offset_high;
    ui32 reserved;
} __attribute__((packed));

struct tss_t {
    ui32 reserved0;
    ui64 rsp[3];
    ui64 reserved1;
    ui64 ist[7];
    ui64 reserved2;
    ui16 reserved3;
    ui16 io_bitmap_offset;
} __attribute__((packed));

alignas(16) ui64 gdt[7];
alignas(16) idt_entry_t idt[256];
alignas(16) ui8 double_fault_stack[16384];

extern "C" void             *x86_exception_stubs[32];
extern "C" void              x86_syscall_stub(); // 在 user.s 中定义
extern "C" void              x86_load_tables(const table_descriptor_t *,
                                             const table_descriptor_t *, ui16);
extern "C" bool              x86_enter_user_asm(ui64, ui64, ui64, ui64);
extern "C" [[noreturn]] void x86_leave_user_asm(bool); // user.s

void set_idt_entry(ui8 vector, void *handler, ui8 attributes, ui8 ist = 0) {
    ui64 address              = (ui64)(uip)handler;
    idt[vector].offset_low    = (ui16)address;
    idt[vector].selector      = kernel_code_selector;
    idt[vector].ist           = ist;
    idt[vector].attributes    = attributes;
    idt[vector].offset_middle = (ui16)(address >> 16);
    idt[vector].offset_high   = (ui32)(address >> 32);
    idt[vector].reserved      = 0;
}

} // namespace

extern "C" tss_t x86_tss;
tss_t            x86_tss;

void x86_init() {
    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00cf92000000ffffULL;
    gdt[3] = 0x00cff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;

    ui64 tss_address = (ui64)(uip)&x86_tss;
    ui64 tss_limit   = sizeof(x86_tss) - 1;
    gdt[5]           = (tss_limit & 0xffff) | ((tss_address & 0xffffff) << 16) |
             (0x89ULL << 40) | (((tss_limit >> 16) & 0xf) << 48) |
             (((tss_address >> 24) & 0xff) << 56);
    gdt[6] = tss_address >> 32;

    x86_tss.ist[0] =
        (ui64)(uip)(double_fault_stack + sizeof(double_fault_stack));
    x86_tss.io_bitmap_offset = sizeof(x86_tss);

    for (ui8 vector = 0; vector < 32; vector++) {
        set_idt_entry(vector, x86_exception_stubs[vector], interrupt_gate,
                      vector == 8 ? 1 : 0);
    }
    set_idt_entry(0x80, (void *)x86_syscall_stub, user_interrupt_gate);

    table_descriptor_t gdt_descriptor = {(ui16)(sizeof(gdt) - 1),
                                         (ui64)(uip)gdt};
    table_descriptor_t idt_descriptor = {(ui16)(sizeof(idt) - 1),
                                         (ui64)(uip)idt};
    x86_load_tables(&gdt_descriptor, &idt_descriptor, tss_selector);

    ui32 low;
    ui32 high;
    asm("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080));
    low |= 1u << 11;
    asm("wrmsr" : : "a"(low), "d"(high), "c"(0xc0000080));
    asm("mov %%cr0, %%rax; or $0x10000, %%rax; mov %%rax, %%cr0"
        :
        :
        : "rax", "memory");
}

bool x86_enter_user(ui64 entry, ui64 stack, ui64 argument, ui64 page_table) {
    return x86_enter_user_asm(entry, stack, argument, page_table);
}

[[noreturn]] void x86_leave_user(bool success) { x86_leave_user_asm(success); }
