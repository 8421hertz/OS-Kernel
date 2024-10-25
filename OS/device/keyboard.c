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
#define shift_l_make 0x2a
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
    /* 0x00 */ {0, 0}, // 没有通码为 0 的键，因此数组第 0 个一维数组 keymap[0][]，咱们为其定义两个 0 值
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
 * @brief 键盘中断处理程序（每次只能处理一个字节，所以当扫描码中是多字节或者有组合键时，要定义额外的全局变量来记录它们曾经被按下过）
 *
 * 每收到一个中断，就调用 inb(KBD_BUF_PORT) 读取 8042 的输出缓冲区寄存器
 *
 * ctrl_status、shift_status 和 caps_lock_status 是定义在代码 10-12-1 结尾处的三个全局变量，它们分别记录 <ctrl>、<shift> 和 <caps lock> 三个键的状态，
 * 值为 true 表示按下，值为 false 表示弹起。每次这三个键被按下或被弹起时，都将记录在这些变量中，
 * 对这三个键的处理是在 intr_keyboard_handler 中对通码和断码各自的处理代码块的结尾处
 * 最多支持 <ctrl>+<shift>+<alt> 三个控制键形式的组合键
 *
 * 该函数处理键盘中断事件，通过处理扫描码识别按键状态并输出字符。
 * 根据接收到的扫描码判断按键是否为控制键（如 Ctrl、Shift、Alt）或字符键，并根据 Shift 和 Caps Lock 状态将字符转换为对应的大小写输出。
 *
 * @return void
 */
static void intr_keyboard_handler(void)
{
    /* 在这次中断发生前的上一次中断，判断以下任意三个键是否被按下并且尚未松开 */
    bool ctrl_down_last = ctrl_status;
    bool shift_down_last = shift_status;
    bool caps_locak_last = caps_lock_status;

    bool break_code; // 用于记录是否为断码

    /* 必须要从键盘缓冲区寄存器 0x60 端口读取键盘扫描码，否则 8042 不再继续响应键盘中断 */
    uint16_t scancode = inb(KBD_BUF_PORT);

    /**
     * 目前只支持主键盘区上的键，因此只存在一个 0xe0 作为扫描码前缀的情况。若扫描码为 0xe0，表示该键的扫描码多于一个字节，将其标记为扩展扫描码
     *
     * 若扫描码 scancode 是以 0xe0 开头的，表示此键的按下将产生多个扫描码，我们向上结构把此码赋给 ext_scancode，等待下一个扫描码进来
     *		只要发现扫描码为 0xe0，就表示此键的扫描码多于一个字节，后面还有扫描码，因此将 ext_scancode 标记置为 true 后执行 return 返回
     */
    if (scancode == 0xe0)
    {
        ext_scancode = true; // 打开 e0 标记，等待下一个后半部分的扩展扫描码
        return;
    }

    /* 若上一个扫描码是以 0xe0 开头的，将此次扫描码与其合并为完整的扫描码 */
    if (ext_scancode)
    {
        scancode = ((0xe000) | scancode); // 合并为完整扫描码
        ext_scancode = false;             // 关闭扩展扫描码 0xe0 标记
    }

    break_code = ((scancode & 0x0080) != 0); // 判断是否为断码

    /* 接下俩要判断此次按键（扫描码）对应的字符是什么，因为我们的扫描码对应的字符定义在二维数组 keymap 中，通码是此数组的索引，所以需要将断码还原成通码 */
    if (break_code) // 如果是断码（按键弹起时产生的扫描码），就进入断码的处理代码块中
    {
        /* 由于 ctrl_r 和 alt_r 的 make_code 和 break_code 都是两字节，所以可用下面的方法获取 make_code，多字节的扫描码都以 0xe0 开头 */
        uint16_t make_code = (scancode &= 0xFF7F); // 清除 break_code 断码的第 7 位，得到通码 make_code（按键按下时的扫描码）
                                                   // 断码的第 7 位为 1，通码的第 7 位为 0

        // 下面用 "通码" 来处理键盘的 "弹起"，加引号的目的是想强调，咱们还是在断码的处理流程中，处理的是按键的弹起，只是用通用码更为方便判断是哪个键

        /**
         * 若是任意以下三个键（左或右）弹起了，将状态置为 false，表示此操作控制键被弹起了（虽然现在是在用通码在判断，但现在处于处理断码的代码块中，此代码块就是判断是哪个键被弹起了）
         *
         * 一般情况下这三个键在键盘上是左右各一个，所以无论按下哪个都表示按下了统一功能的控制键，因此无论弹起哪个也都表示弹起了同一功能的控制键
         *		一般是先松开控制键，再松开字符键，所以这三个键的状态变量 ctrl_status、shift_status 和 alt_status 并不是本次使用，是供下次判断组合键用的，本次只是记录是否松开了它们。
         *		下次在进入键盘中断时，再 intr_keyboard_handler 的开头通过 ctrl_down_last = ctrl_status 获取上一次 ctrl 键是否处于弹起的状态
         */
        if (make_code == ctrl_l_make || make_code == ctrl_r_make)
        {
            ctrl_status = false;
        }
        else if (make_code == shift_l_make || make_code == shift_r_make)
        {
            shift_status = false;
        }
        else if (make_code == alt_l_make || make_code == alt_r_make)
        {
            alt_status = false;
        }

        return; // 直接返回，结束此次的中断处理程序
    }
    /**
     * 来到这里，说明不是断码，而是通码，这里的判断是保证我们只处理这些数组中定义了的键，以及右 alt 和 ctrl。
     *      由于我们最大支持的通码为 0x3a，即只支持到 <caps_locak> 键，因此要防止越界
     *      将来也要支持 ctrl 和 alt 相关的快捷键，但 <R-ctrl> 好 <R-alt> 的通码是以 0xe0 开头的扩展扫描码，范围不在 0x3b 之内，所以加了 "或" 判断
     *
     * 主要根据通码和 shift 键是否按下的情况，再数组 keymap 中找到按键对应的字符
     */
    else if ((scancode > 0x00 && scancode < 0x3b) || (scancode == alt_r_make) || (scancode == ctrl_r_make))
    {
        /* 无论 <capslock> 键和 <shift> 键怎样配合，其最终效果等同于 <shift> 键要么按下，要么松开，所以只要确定 shift 的值便确定了转换结果 */
        // 判断是否与 shift 键组合，用来在二维数组中索引对应的字符
        bool shift = false; // shift 用来索引按键所在一维数组中的两个元素，默认为 false（没有按下）

        if ((scancode < 0x0e) || (scancode == 0x29) ||
            (scancode == 0x1a) || (scancode == 0x1b) ||
            (scancode == 0x2b) || (scancode == 0x27) ||
            (scancode == 0x33) || (scancode == 0x34) || (scancode == 0x35))
        {
            /****** 代表两个字符的键（双字符键） ******
            0x0e 数字'0'~'9', 字符'-', 字符'='
            0x29 字符'`'
            0x1a 字符'['
            0x1b 字符']'
            0x2b 字符'\\'
            0x27 字符';'
            0x33 字符','
            0x34 字符'.'
            0x35 字符'/'
            *****************************/
            if (shift_down_last) // 在这次中断发生前的上一次中断，shift 按键被按下并且未松开
            {
                // 如果同时按下了 shift 键
                shift = true;
            }
        }
        else
        {
            // 默认认为字母键
            if (shift_down_last && caps_locak_last)
            {
                // 如果 shift 和 capslock 同时按下，抵消了大写功能，所以 shift 为 false
                shift = false;
            }
            else if (shift_down_last || caps_locak_last)
            {
                // 如果 shift 和 capslock 任意被按下，大写功能被开启
                shift = true;
            }
            else
            {
                // shift 和 capslock 都没有按下
                shift = false;
            }
        }

        // 将扫描码的高字节置为 0，主要针对高字节是 e0 的扩展扫描码
        uint8_t index = (scancode & 0x00FF);
        char cur_char = keymap[index][shift]; // 在数组中找到对应的字符

        /* 只处理 ASCII 码不为 0 的键（部分控制字符是不可见的，其值为 0，没法显示它们） */
        if (cur_char)
        {
            put_char(cur_char);
            return;
        }

        /* 记录本次是否按下了下面几类控制键之一，供下次键入时判断组合键 */
        // 如果 cur_char 为 0，根据目前 keymap 的定义，表示它们的操作控制键是 <ctrl>、<shift>、<alt>、<capslock> 之一（因为它们的 ASCII 码值为 0），需要判断它们中的哪一个被按下了
        // 按下了就将其状态置为 true，供下次判断组合键（这三个控制键变量不是本次使用，是供下次判断组合键用的，本次只是记录是否按下了它们。下次再进入中断时，在 intr_keyboard_handler 的开头通过 ctrl_down_last = ctrl_status 获取上一次 ctrl 键是否按下）
        if (scancode == ctrl_l_make || scancode == ctrl_r_make)
        {
            ctrl_status = true;
        }
        else if (scancode == shift_l_make || scancode == shift_r_make)
        {
            shift_status = true;
        }
        else if (scancode == alt_l_make || scancode == alt_r_make)
        {
            alt_status = true;
        }
        else if (scancode == caps_lock_make) // <caps locak> 键有些特殊，它不像 <ctrl>、<shift>、<alt> 那样 "按下时" 开启，"弹起时" 关闭
                                             // <caps lock> 生效于 每一次完整的击键操作，即 "按下再弹起" 开启，下一次的 "按下再弹起" 则关闭，相当于状态取反（开启时按下此键是关闭；关闭时按下此键是开启）
        {
            caps_lock_status = !caps_lock_status;
        }
        else
        {
            put_str("unknown key\n"); // 咱们支持的键是有限的，对于哪些通码在 0x3b 以上的按键，在 else 代码中执行 put_str("unknown key\n") 提示是未知按键
        }
    }
}

/* 键盘中断处理程序初始化（将其加入到 idt_table 中） */
void keyboard_init()
{
    put_str("keyboard init start\n");
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done\n");
}
