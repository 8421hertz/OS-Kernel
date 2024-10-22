#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
// int _start(void)

void k_thread_a(void *);
void k_thread_b(void *arg);

int main(void)
{
    put_str("I am kernel\n");
    init_all();

    // 测试 ASSERT
    // asm volatile("sti");        // 为演示中断处理，在此临时开中断 (sti 会将标志寄存器 eflags 中的 IF 位置 1 ，这样来自中断代理 8259A 的中断信号便被处理器受理了。)
    // ASSERT(1==2);

    // 测试内存管理管理系统
    // void *addr = get_kernel_pages(3); // 申请 3 个物理页
    // put_str("\nget_kernel_page start vaddr is ");
    // put_int((uint32_t)addr);
    // put_str("\n");

    // thread_start("k_thread_a", 31, k_thread_a, "argA ");
    // // 按理说，屏幕上打印的字符串“argA”的数量大约为“argB”的 4 倍
    // thread_start("k_thread_b", 8, k_thread_b, "argB ");

    intr_enable(); // 打开中断, 使时钟中断起作用（通过 intr_enable 将中断打开，目前我们在 8259A 中只打开了时钟中断，因此，时钟中断对应的中断处理程序会引发调度。）

    // 已经将 main 函数在 thread_init 中通过 make_main_thread 封装为线程，其优先级为 31，因此 main 中第 17 行的循环打印“Main”也会不断被调度
    while (1)
    {
        intr_disable();
        put_str("Main ");
        intr_enable();
    }

    return 0;
}

/* 在线程中运行的函数 */
void k_thread_a(void *arg)
{
    // 用 void* 来表示参数，被调用的函数知道自己需要什么类型的参数，自己转换再用
    char *para = (char *)arg;
    while (1)
    {
        intr_disable();
        put_str(para);
        intr_enable();
    }
}

/* 在线程中运行的函数 */
void k_thread_b(void *arg)
{
    // 用 void* 来表示参数，被调用的函数知道自己需要什么类型的参数，自己转换再用
    char *para = (char *)arg;
    while (1)
    {
        intr_disable();
        put_str(para);
        intr_enable();
    }
}
