#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
// int _start(void)

void k_thread_a(void *);

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

    thread_start("k_thread_a", 31, k_thread_a, "argA ");

    while (1)
        ;

    return 0;
}

/* 在线程中运行的函数 */
void k_thread_a(void *arg)
{
    // 用 void* 来表示参数，被调用的函数知道自己需要什么类型的参数，自己转换再用
    char *para = (char *)arg;
    while (1)
    {
        put_str(para);
    }
}
