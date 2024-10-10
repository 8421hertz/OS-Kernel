#include "time.h"
#include "io.h"
#include "print.h"

#define IRQ0_FREQUENCY 100      // 时钟中断频率，设为 100Hz
#define INPUT_FERQUENCY 1193180 // 计数器 0 的工作脉冲信号频率
// 1193180 / 中断信号的频率 = 计数器 0 的初值
#define COUNTER0_VALUE INPUT_FERQUENCY / IRQ0_FREQUENCY // 计数器 0 的初始值，根据频率计算
#define COUNTER0_PORT 0x40                              // 计数器 0 的端口号
#define COUNTER_NO 0                                    // 选择计数器的号码，0 代表计数器 0
#define COUNTER_MODE 2                                  // 工作方式 2，比率发生器模式
#define READ_WRITE_LATCH 3                              // 读写方式，先写低 8 位，再写高 8 位
#define PIT_CONTROL_PORT 0x43                           // 控制器寄存器的端口

/**
 * frequency_set - 设置计数器的初始值和工作模式
 *
 * 该函数用于设置计数器的控制字，并写入计数初值以设置 8253 的工作频率。
 * 控制字包括计数器编号、读写方式和计数器工作模式，并根据指定的初值设定计数器。
 * 该函数会按照 8253 的初始化步骤，先写入计数器初值的低 8 位，然后再写高 8 位。
 *
 * @counter_port: 计数器的端口号，用来指定初值 counter_value 的目的端口号。
 * @counter_no: 指定使用的计数器编号，对应控制字中的 SC1 和 SC2 位。
 * @rwl: 设置计数器的读/写/锁存方式，对应控制字中的 RW1 和 RW0 位。
 * @counter_mode: 设置计数器的工作方式，对应控制字中的 M2～M0 位。
 * @counter_value: 设置计数器的计数初值，16 位值，先写低 8 位再写高 8 位。
 */
static void frequency_set(uint8_t counter_port,
                          uint8_t counter_no,
                          uint8_t rwl,
                          uint8_t counter_mode,
                          uint16_t counter_value)
{
    /*
        往控制字寄存器端口 0x43 中写入控制字
        控制字格式：SC1, SC0（选择计数器）, RW1, RW0（读/写方式）, M2~M0（工作模式）
    */
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));

    // 先写入计数初值的低 8 位到计数器的端口
    outb(counter_port, (uint8_t)counter_value);
    // 再写入技术初值的高 8 位到计数器的端口
    outb(counter_port, (uint8_t)counter_value >> 8);
}

/**
 * timer_init - 初始化 8253 定时器
 *
 * 该函数初始化 8253 定时器，使其能够每秒发出 100 次中断。
 * 它通过调用 frequency_set 函数，向计数器 0 写入初始值并设置计数器工作模式。
 * 初始化过程包括向控制字寄存器写入控制字，并依次写入 16 位的计数初值（低 8 位和高 8 位）。
 *
 * 该函数在初始化过程中会输出调试信息，用于监控初始化的开始和结束。
 */
void timer_init()
{
    put_str("timer_init start\n"); // 输出初始化开始信息
    /*
        调用 frequency_set 函数，设置计数器 0 的初始值和工作模式
        参数：计数器端口，计数器编号，读写方式，工作模式，计数器初值
    */
    frequency_set(COUNTER0_PORT,
                  COUNTER_NO,
                  READ_WRITE_LATCH,
                  COUNTER_MODE,
                  COUNTER0_VALUE);

    put_str("timer_init done\n"); // 输出初始化完成信息
}
