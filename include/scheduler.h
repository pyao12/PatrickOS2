#pragma once

#include <common.h>

constexpr int  scheduler_max_tasks  = 8;
constexpr ui32 scheduler_stack_size = 16 * 1024;

typedef void (*scheduler_task_fn)(void *arg);

struct scheduler_context_t {
    ui64 rbx;
    ui64 rbp;
    ui64 r12;
    ui64 r13;
    ui64 r14;
    ui64 r15;
    ui64 rsp;
};

struct scheduler_task_t {
    scheduler_context_t context;
    scheduler_task_fn   entry;
    void               *arg;
    bool                active;
    bool                finished;
};

void scheduler_init();
int  scheduler_create_task(scheduler_task_fn entry, void *arg);
void scheduler_run();
void scheduler_yield();
void scheduler_task_exit();
