#include "interrupt.h"
#include "stdint.h"
#include "global.h"

#define IDT_DESC_CNT 0x21 // 目前总共支持的中断数 ( 33 个中断处理程序 )

/* 中断门描述符结构体 */
struct gate_desc
{
    uint16_t func_offset_low_word;  // 函数偏移地址的低 16 位
    uint16_t selector;              // 代码段选择子
    uint8_t dcount;                 // 此项为双字计数字段，是门描述符中的第 4 字节 ( 固定为 0, 不用考虑 )
    uint8_t attribute;              // 中断门的属性
    uint16_t func_offset_high_word; // 函数偏移地址的高 16 位
};

// 静态函数声明，非必须
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);

static struct gate_desc idt[IDT_DESC_CNT]; // idt 中断描述符表，本质上是一个中断门描述符数组

extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 声明引用定义在 kernel.S 中的中断处理函数入口地址数组
                                                    // 中断描述符地址数组 ( 仅仅表明地址，用于修饰 intr_entry_table，定义在 interrupt.h 中 )
                                                    // 中断向量号 * 8 + IDTR = 中断门描述符地址

/**
 * @brief 创建中断门描述符（在中断描述符表 IDT 中填充中断门描述符）
 *
 * 该函数通过初始化门描述符来设置一个 IDT 条目，使用给定的属性和中断处理函数。
 *
 * @param p_gdesc 指向要初始化的门描述符 gate_desc 的指针。该描述符将被配置为处理指定的中断。
 * @param attr 门描述符的属性，例如中断门的类型、特权级别和存在位。
 * @param function 当触发与该描述符对应的中断时，将执行的中断处理函数。
 */
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function) // typedef void* intr_handler;     // 地址指针
{
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF; // 由于是低 16 位有效，所以需要与 0x0000FFFF 做与运算
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/**
 * @brief 初始化中断描述符表（在中断描述符表 IDT 中填充中断门描述符）
 *
 * 该函数遍历中断描述符表的每个条目，并使用给定的属性和中断处理函数为每个条目初始化对应的描述符。
 *
 * @param void 该函数不接受任何参数。
 *
 * @return void 该函数不返回任何值。
 *
 * 函数内部流程：
 * 1. 遍历中断描述符表的所有条目。
 * 2. 对每个条目调用 `make_idt_desc` 函数，使用特权级 0 (DPL0) 和对应的中断处理程序进行初始化。
 * 3. 在完成所有条目的初始化后，打印一条完成信息。
 */
static void idt_desc_init(void)
{
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    put_str("   idt_desc_init done\n");
}

/**
 * @brief 完成与中断有关的所有初始化工作 ———— 中断初始化的主函数
 *
 * 该函数负责初始化中断描述符表 (IDT) 和 8259A 可编程中断控制器 (PIC)，然后将 IDT 加载到 CPU 的 IDTR 寄存器中。
 *
 * @param void 该函数不接受任何参数。
 *
 * @return void 该函数不返回任何值。
 *
 * 函数内部流程：
 * 1. 打印初始化开始的信息。
 * 2. 调用 `idt_desc_init()` 函数，初始化中断描述符表。
 * 3. 调用 `pic_init()` 函数，初始化 8259A PIC。
 * 4. 使用 `lidt` 指令将 IDT 的地址和大小加载到 CPU 的 IDTR 寄存器中。
 * 5. 打印初始化完成的信息。
 */
void idt_init()
{
    put_str("idt_init start\n");
    idt_desc_init(); // 初始化中断描述符表 IDT
    pic_init();      // 初始化 8259A

    /* 加载 IDT */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0"
                 :
                 : "m"(idt_operand));

    put_str("idt_init done\n");
}
