#include "print.h"
#include "init.h"
// int _start(void)
int main(void)
{

    put_str("I am kernel\n");
    init_all();
    asm volatile("sti");        // 为演示中断处理，在此临时开中断 (sti 会将标志寄存器 eflags 中的 IF 位置 1 ，这样来自中断代理 8259A 的中断信号便被处理器受理了。)

    while (1)
        ;

    return 0;
}
