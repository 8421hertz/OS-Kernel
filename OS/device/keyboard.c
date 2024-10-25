#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

/**
 * ① 当需要把数据从处理器发到 8042 时（数据传送尚未发生时），0x60 端口的作用是输入缓冲区，此时应该用 out 指令写入 0x60 端口。
 * ② 当数据已从 8048 发到 8042 时，0x60 端口的作用是输出缓冲区，此时 CPU 应该用 in 指令从 8042 的 0x60 端口（输出缓冲区寄存器）读取 8048 的输出结果。
 */
#define KBD_BUF_PORT 0x60 // 键盘 buffer 缓冲区寄存器端口号为 0x60（8042 输入和输出缓冲区寄存器的端口）

/**
 * 键盘中断处理程序
 *
 * 每收到一个中断，就通过 put_char('k') 打印字符 'k'，然后再调用 inb(KBD_BUF_PORT) 读取 8042 的输出缓冲区寄存器
 */
static void intr_keyboard_handler(void)
{
    // put_char('k');   // 每击键一次，就会进行一次键盘中断
    /* 必须要读取输出缓冲区寄存器中的键盘扫描码，否则 8042 不再继续响应键盘中断 */
    uint8_t scancode = inb(KBD_BUF_PORT);
    put_int(scancode); // 输出键盘扫描码
    return;
}

/* 键盘中断处理程序初始化（将其加入到 idt_table 中） */
void keyboard_init()
{
    put_str("keyboard init start\n");
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done\n");
}
