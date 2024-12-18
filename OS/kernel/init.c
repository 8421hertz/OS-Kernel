#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "time.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"

/* 负责初始化所有模块 */
void init_all()
{
    put_str("init_all\n");
    idt_init();      // 初始化中断
    mem_init();      // 初始化内存管理系统
    thread_init();   // 初始化线程先关结构
    timer_init();    // 初始化 PIT8253
    console_init();  // 初始化中断控制台（最好放在开中断之前）
    keyboard_init(); // 初始化键盘中断处理程序
}
