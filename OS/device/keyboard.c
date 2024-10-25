#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

typedef int bool;
#define true 1
#define false 0

/**
 * ① 当需要把数据从处理器发到 8042 时（数据传送尚未发生时），0x60 端口的作用是输入缓冲区，此时应该用 out 指令写入 0x60 端口。
 * ② 当数据已从 8048 发到 8042 时，0x60 端口的作用是输出缓冲区，此时 CPU 应该用 in 指令从 8042 的 0x60 端口（输出缓冲区寄存器）读取 8048 的输出结果。
 */
#define KBD_BUF_PORT 0x60 // 键盘 buffer 缓冲区寄存器端口号为 0x60（8042 输入和输出缓冲区寄存器的端口）

/**
 * 预定义的按键 ASCII 码（用转义字符定义部分控制字符）
 *
 * 由于以下都是不可见字符，它们都是用转义字符的形式定义的
 */
#define esc '\033' // 八进制表示字符 ASCII 码，也可以用十六进制 '\x1b'（esc 和 delete 没有一般转义字符的形式，所以只能用八进制或十六进制形式）
#define backspace '\b'
#define tab '\t'
#define enter '\r'
#define delete '\177' // 十六进制为 '\x7f'

/**
 * 预定义的操作控制键的 "ASCII" 码（以下不可见字符一律定义为 0）
 *
 * 注意：在控制键中只有字符控制键才有 ASCII 码，由于后面定义的数组中要用到 ASCII 码，为了与数组保持格式上的一致，所以将其统一定义为 0，相当于占位用的
 */
#define char_invisible 0
#define ctrl_l_char char_invisible
#define ctrl_r_char char_invisible
#define shift_l_char char_invisible
#define shift_r_char char_invisible
#define alt_l_char char_invisible
#define alt_r_char char_invisible
#define caps_locak_char char_invisible

/**
 * 预定义操作控制键的扫描码（控制字符的通码和断码）
 *
 * 提到过的 "两阶段" 中的第一阶段，用于组合键中的判断（比如，按下了 Shift 时再按字母键，就表示输入的是大写字母）
 *      操作控制键与其他案件配合时是先被按下的，因此，每次在接收一个按键时，需要查看上一次是否有按下相关的操作控制键。
 *      所以得记录操作控制键在之前是否被按下了，也就是将操作控制键的当前状态记录在某个全局变量中
 */
#define shift_1_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define ctrl_r_break 0xe09d
#define caps_locak_make 0x3a

/* 定义以下 变量操作控制键状态全局变量，表示是否是按下的状态（true 表示按下；false 表示弹起），ext_scancode 用于记录 makecode 是否以 0xe0 开头（扩展字符扫描码） */
static bool ctrl_status, shift_status, alt_status, caps_lock_status, ext_scancode;





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
