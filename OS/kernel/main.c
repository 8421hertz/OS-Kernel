#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
// int _start(void)
int main(void)
{
    put_str("I am kernel\n");
    init_all();
    // asm volatile("sti");        // 为演示中断处理，在此临时开中断 (sti 会将标志寄存器 eflags 中的 IF 位置 1 ，这样来自中断代理 8259A 的中断信号便被处理器受理了。)
    // ASSERT(1==2);
    void *addr = get_kernel_pages(3); // 申请 3 个物理页
    put_str("\nget_kernel_page start vaddr is ");
    put_int((uint32_t)addr);
    put_str("\n");

    while (1)
        ;

    return 0;
}
