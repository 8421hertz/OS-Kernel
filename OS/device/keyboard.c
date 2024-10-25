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
#define caps_lock_char char_invisible

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
#define caps_lock_make 0x3a

/* 定义以下 变量操作控制键状态全局变量，表示是否是按下的状态（true 表示按下；false 表示弹起），ext_scancode 用于记录 makecode 是否以 0xe0 开头（扩展字符扫描码） */
static bool ctrl_status, shift_status, alt_status, caps_lock_status, ext_scancode;

/**
 * 以通码 make_code 为索引的二维数组
 *
 * 主要定义了与 Shift 组合时的字符效果，数组范围是 0~0x3A（目前所支持的主键盘区的按键范围，图 10-15）
 *
 * 主键盘区主要是与上档键 Shift 配合使用，如果之前已经按下了 Shift 键并且按住不松手：
 * 		① 当在主键盘区中按下数字键时，这表示按键为数字上面的符号，如 '2' 变成了 '@'
 *		② 当在主键盘区中按下字母键时，这表示按键为大写字母，如 'a' 变成了 'A'
 * 有无 Shift 键参与时按键效果是不同的，所以我们主要以这种和 Shift 键配合的情况来建立通码到对应字符的映射（这里的字符就是值字符的 ASCII 码）
 *
 * keymap 中每个数组元素都是一维数组，代表某个按键在有无 Shift 键配合的情况下实现
 * 		如，若不按下 Shift 的情况下按下 'a'，keymap[0x1e][0]='a'；按下 Shift，keymap[0x1e][1]='A'
 */
static char keymap[][2] = {
    /* 扫描码未与 shift 组合 */
    /* ---------------------------------- */
    /* 0x00 */ {0, 0},
    /* 0x01 */ {esc, esc},
    /* 0x02 */ {'1', '!'},
    /* 0x03 */ {'2', '@'},
    /* 0x04 */ {'3', '#'},
    /* 0x05 */ {'4', '$'},
    /* 0x06 */ {'5', '%'},
    /* 0x07 */ {'6', '^'},
    /* 0x08 */ {'7', '&'},
    /* 0x09 */ {'8', '*'},
    /* 0x0A */ {'9', '('},
    /* 0x0B */ {'0', ')'},
    /* 0x0C */ {'-', '_'},
    /* 0x0D */ {'=', '+'},
    /* 0x0E */ {backspace, backspace},
    /* 0x0F */ {tab, tab},
    /* 0x10 */ {'q', 'Q'},
    /* 0x11 */ {'w', 'W'},
    /* 0x12 */ {'e', 'E'},
    /* 0x13 */ {'r', 'R'},
    /* 0x14 */ {'t', 'T'},
    /* 0x15 */ {'y', 'Y'},
    /* 0x16 */ {'u', 'U'},
    /* 0x17 */ {'i', 'I'},
    /* 0x18 */ {'o', 'O'},
    /* 0x19 */ {'p', 'P'},
    /* 0x1A */ {'[', '{'},
    /* 0x1B */ {']', '}'},
    /* 0x1C */ {enter, enter},
    /* 0x1D */ {ctrl_l_char, ctrl_l_char},
    /* 0x1E */ {'a', 'A'},
    /* 0x1F */ {'s', 'S'},
    /* 0x20 */ {'d', 'D'},
    /* 0x21 */ {'f', 'F'},
    /* 0x22 */ {'g', 'G'},
    /* 0x23 */ {'h', 'H'},
    /* 0x24 */ {'j', 'J'},
    /* 0x25 */ {'k', 'K'},
    /* 0x26 */ {'l', 'L'},
    /* 0x27 */ {';', ':'},
    /* 0x28 */ {'\'', '"'},
    /* 0x29 */ {'`', '~'},
    /* 0x2A */ {shift_l_char, shift_l_char},
    /* 0x2B */ {'\\', '|'},
    /* 0x2C */ {'z', 'Z'},
    /* 0x2D */ {'x', 'X'},
    /* 0x2E */ {'c', 'C'},
    /* 0x2F */ {'v', 'V'},
    /* 0x30 */ {'b', 'B'},
    /* 0x31 */ {'n', 'N'},
    /* 0x32 */ {'m', 'M'},
    /* 0x33 */ {',', '<'},
    /* 0x34 */ {'.', '>'},
    /* 0x35 */ {'/', '?'},
    /* 0x36 */ {shift_r_char, shift_r_char},
    /* 0x37 */ {'*', '*'},
    /* 0x38 */ {alt_l_char, alt_l_char},
    /* 0x39 */ {' ', ' '},
    /* 0x3A */ {caps_lock_char, caps_lock_char}
    /* 其他按键暂不处理 */
};

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
