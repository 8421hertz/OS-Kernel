#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "time.h"
#include "memory.h"

/* 负责初始化所有模块 */
void init_all()
{
    put_str("init_all\n");
    idt_init();   // 初始化中断
    timer_init(); // 初始化 PIT8253
    mem_init();
}
