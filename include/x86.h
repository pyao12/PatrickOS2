#pragma once

#include <common.h>

void              x86_init();
void              x86_set_interrupt_handler(ui8 vector, void *handler);
bool              x86_enter_user(ui64 entry, ui64 stack, ui64 argument, ui64 page_table);
[[noreturn]] void x86_leave_user(bool success);
