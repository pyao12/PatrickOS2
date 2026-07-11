#include <scheduler.h>


alignas(16) \
static ui8 scheduler_task_stacks[scheduler_max_tasks][scheduler_stack_size];
static scheduler_task_t scheduler_tasks[scheduler_max_tasks];
static scheduler_context_t scheduler_context;
static int scheduler_current_task = -1;
static int scheduler_task_count = 0;


extern "C" __attribute__ ((naked)) void scheduler_context_switch (
    scheduler_context_t *from, scheduler_context_t *to
) {
    asm (
        "movq %rbx, 0(%rdi)\n"
        "movq %rbp, 8(%rdi)\n"
        "movq %r12, 16(%rdi)\n"
        "movq %r13, 24(%rdi)\n"
        "movq %r14, 32(%rdi)\n"
        "movq %r15, 40(%rdi)\n"
        "movq %rsp, 48(%rdi)\n"
        "movq 0(%rsi), %rbx\n"
        "movq 8(%rsi), %rbp\n"
        "movq 16(%rsi), %r12\n"
        "movq 24(%rsi), %r13\n"
        "movq 32(%rsi), %r14\n"
        "movq 40(%rsi), %r15\n"
        "movq 48(%rsi), %rsp\n"
        "ret\n"
    );
}

static scheduler_task_t* scheduler_current_task_ptr() {
    if (scheduler_current_task < 0 || scheduler_current_task >= scheduler_max_tasks)
        return 0;
    else return &scheduler_tasks[scheduler_current_task];
}

static void scheduler_task_trampoline() {
    scheduler_task_t *task = scheduler_current_task_ptr();
    if (task == 0) halt();

    task->entry(task->arg);
    scheduler_task_exit();
    halt(); // 不应该走到这一步，走到了说明寄了
}

static int scheduler_find_next_task() {
    if (scheduler_task_count == 0) return -1; // 没有下一个任务了！

    int start_index = scheduler_current_task;
    for (int step = 1; step <= scheduler_task_count; step++) {
        int index = (start_index + step) % scheduler_task_count; // 循环
        if (scheduler_tasks[index].active && !scheduler_tasks[index].finished) return index;
    }
    return -1;
}

void scheduler_init() {
    // 全部赋值为0！
    for (int index = 0; index < scheduler_max_tasks; index++) {
        scheduler_tasks[index].context.rbx = 0;
        scheduler_tasks[index].context.rbp = 0;
        scheduler_tasks[index].context.r12 = 0;
        scheduler_tasks[index].context.r13 = 0;
        scheduler_tasks[index].context.r14 = 0;
        scheduler_tasks[index].context.r15 = 0;
        scheduler_tasks[index].context.rsp = 0;
        scheduler_tasks[index].entry       = 0;
        scheduler_tasks[index].arg         = 0;
        scheduler_tasks[index].active = 0;
        scheduler_tasks[index].finished = 0;
    }
}

int scheduler_create_task(scheduler_task_fn entry, void* arg) {
    if (entry == 0) return -1;

    int index = -1;
    // 寻找空闲的任务位置，如果找到了那么就复用这个任务位置作为当前任务
    for (int i = 0; i < scheduler_max_tasks; i++) {
        if (!scheduler_tasks[i].active) {
            index = i;
            break;
        }
    }
    if (index == -1) return -1;

    scheduler_task_t &task = scheduler_tasks[index];
    task.entry = entry;
    task.arg = arg;
    task.active = true;
    task.finished = false;

    task.context.rbx = 0;
    task.context.rbp = 0;
    task.context.r12 = 0;
    task.context.r13 = 0;
    task.context.r14 = 0;
    task.context.r15 = 0;

    uip *stack_top = reinterpret_cast <uip *> (scheduler_task_stacks[index] + scheduler_stack_size);
    *--stack_top = 0;
    *--stack_top = reinterpret_cast <uip> (&scheduler_task_trampoline);
    task.context.rsp = reinterpret_cast <ui64> (stack_top);

    if (index + 1 > scheduler_task_count) scheduler_task_count = index + 1;
    return index;
}

void scheduler_run() {
    while (true) {
        int next_task = scheduler_find_next_task();
        if (next_task < 0) return;
        scheduler_current_task = next_task;
        scheduler_context_switch(&scheduler_context, &scheduler_tasks[next_task].context);
    }
}

void scheduler_yield() {
    scheduler_task_t *task = scheduler_current_task_ptr();
    if (task == 0 || task->finished) return;
    scheduler_context_switch(&task->context, &scheduler_context);
}

void scheduler_task_exit() {
    scheduler_task_t *task = scheduler_current_task_ptr();
    if (task == 0) halt();

    task->finished = true;
    scheduler_context_switch(&task->context, &scheduler_context);
    halt();
}