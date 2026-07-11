#pragma once

#include <common.h>

void x86_init();
bool x86_enter_user(ui64 entry, ui64 stack, ui64 argument, ui64 page_table);
[[noreturn]] void x86_leave_user(bool success);
