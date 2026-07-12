#pragma once

// 无符号整型
#define ui8 __UINT8_TYPE__
#define ui16 __UINT16_TYPE__
#define ui32 __UINT32_TYPE__
#define ui64 __UINT64_TYPE__

// 有符号整型
#define i8 __INT8_TYPE__
#define i16 __INT16_TYPE__
#define i32 __INT32_TYPE__
#define i64 __INT64_TYPE__

// 其他类型
#define uip __UINTPTR_TYPE__

#define asm __asm__ volatile
#define halt() asm("cli; hlt");