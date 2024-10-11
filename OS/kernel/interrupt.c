#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "interrupt.h"
#include "print.h"

#define EFLAGS_IF 0x00000200 // 定义了当 eflages 寄存器中的 IF 位为 1 时，标志中断已开启的值

// 通过嵌入汇编代码来获取 eflags 寄存器中的中断标志位 IF 的状态
// EFLAG_VAR 用来存储 eflags 的变量
#define GET_EFLAGS(EFLAGS_VAR) asm volatile("pushfl; \
                                             popl %0" : "=g"(EFLAGS_VAR))

#define IDT_DESC_CNT 0x21 // 目前总共支持的中断数 ( 33 个中断处理程序 )

#define PIC_M_CTRL 0x20 // 主片的控制端口是 0x20
#define PIC_M_DATA 0x21 // 主片的数据端口是 0x21
#define PIC_S_CTRL 0xa0 // 从片的控制端口是 0xa0
#define PIC_S_DATA 0xa1 // 从片的数据端口是 0xa1

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

char *intr_name[IDT_DESC_CNT]; // 中断异常名数组 ———— 用来记录每一项异常的名字 ( 这是将来在调试时用的，主要是方便咱们自己 )
// idt_table[i] 是一个存储函数指针的数组
intr_handler idt_table[IDT_DESC_CNT]; // 定义中断处理程序数组，在 kernel.S 中定义的 intrXXentry 只是中断处理程序的入口，最终调用的是 ide_table 中的处理程序

extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 声明引用定义在 kernel.S 中的中断处理函数入口地址数组
                                                    // 中断描述符地址数组 ( 仅仅表明地址，用于修饰 intr_entry_table，定义在 interrupt.h 中 )
                                                    // 中断向量号 * 8 + IDTR = 中断门描述符地址

/**
 * @brief 初始化可编程中断控制器 8259A。
 *
 * 该函数通过发送指令字 (ICW) 来初始化主片和从片的 8259A 中断控制器。主片和从片负责管理中断向量，并通过主片的 IR2 引脚进行级联。
 *
 * 初始化步骤包括：
 * 1. 启动主片和从片的边沿触发模式。
 * 2. 设置中断向量号范围。
 * 3. 设定主片和从片之间的级联关系。
 * 4. 打开 IRQ0 ，只接受时钟中断。
 *
 * @param void 该函数不接受任何参数。
 * @return void 该函数无返回值。
 */
static void pic_init(void)
{
    /* 初始化主片 */
    outb(PIC_M_CTRL, 0x11); // ICW1 ：主片边沿出发；级联从片；需要 ICW4
    outb(PIC_M_DATA, 0x20); // ICW2 ：设置主片的起始中断向量号为 0x20 ，代表 IR[0~7] 为 0x20~0x27
    outb(PIC_M_DATA, 0x04); // ICW3 ：使用主片中的 IR2 引脚级联从片
    outb(PIC_M_DATA, 0x01); // ICW4 ：开启 x86 模式；开启 EOI

    /* 初始化从片 */
    outb(PIC_S_CTRL, 0x11);
    outb(PIC_S_DATA, 0x28); // ICW2 ：设置从片的起始中断向量号为 0x28 ，代表 IR[8~15] 为 0x28~0x2F
    outb(PIC_S_DATA, 0x02);
    outb(PIC_S_DATA, 0x01);

    /* 利用 OCW1 设置 IMR 寄存器只放行时钟中断 ( 也就是目前只接收时钟产生的中断 ) */
    outb(PIC_M_DATA, 0xFE);
    outb(PIC_S_DATA, 0xFF);

    put_str("   pic_init done\n");
}

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
 * general_intr_handler - 通用中断处理函数，一般用在异常出现时的处理
 *
 * 该函数用于处理一般的中断，通常在异常情况下调用。
 * 函数会处理特定的中断向量号，如伪中断 ( 由 IRQ7 和 IRQ15 产生 )，这些中断 ( 无法通过 IMR 寄存器屏蔽，这里的处理规则是直接通过 return 返回 ) 不需要进一步处理。
 *      这些伪中断是硬件层面的问题，操作系统不需要进一步处理这些中断，所以处理程序直接返回，跳过任何复杂的处理步骤。这种设计避免了无谓的资源消耗和系统性能的影响。
 *
 * @vec_nr: 中断向量号，指定触发了哪个中断。
 *
 * 函数会专门检查中断向量号 0x27 和 0x2f，这些对应于伪中断 (IRQ7 和 IRQ15)。
 * 如果中断向量号匹配 0x27 或 0x2f，则直接返回，无需进行其他操作。
 */
static void general_intr_handler(uint8_t vec_nr)
{
    if (vec_nr == 0x27 || vec_nr == 0x2f) // 0x27 和 0x2f 中断向量号对应的 IRQ7 和 IRQ15
    {
        return;
    }

    put_str("int vector: 0x");
    put_int(vec_nr);
    put_char('\n');
}

/**
 * exception_init - 初始化异常处理程序 ( 通用的中断处理函数，一般用在异常出现时的处理 )
 *
 * 该函数完成了异常处理函数的注册以及为每个中断设置名称。
 * 它将所有的 IDT 表项初始化为通用的中断处理程序，并为常见的 CPU 异常分配人类可读的名称。
 *
 *
 * 在之后的处理中，实际的异常处理程序可以通过调用 register_handler 来注册。
 */
static void exception_init(void)
{
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        /* idt_table 数组中的函数是在进入中断后根据中断向量号调用的，见 kernel/kernel.S 的 call [idt_table + %1*4] */
        idt_table[i] = general_intr_handler; // 将所有的 IDT 表项初始化为通用的中断处理程序 general_intr_handler( 地址 )
                                             // 以后会用 register_handler 来注册具体处理函数
        intr_name[i] = "unknown";            // 每个异常名字先统一赋值为 unknown
    }

    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第 15 项是 intel 保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
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
    idt_desc_init();  // 初始化中断描述符表 IDT
    exception_init(); // 异常名初始化并注册通常的中断处理函数
    pic_init();       // 初始化 8259A

    /* 加载 IDT，开启中断 */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16)); // (uint32_t)idt：先将指针地址强制转换为 32 位的无符号整数
                                                                                  // (uint64_t)(uint32_t)idt：将 32 位的地址再转换为 64 位的无符号整数 uint64_t( 这一步是为后续的位移操作提供足够的空间。)，转换后，高 32 位是 0 ，低 32 位存储的是 idt 的 32 位地址
                                                                                  // <<16 ：将这个 64 位整数左移 16 位
    asm volatile("lidt %0"
                 :
                 : "m"(idt_operand));

    put_str("idt_init done\n");
}

/**
 * @brief 获取当前的中断状态
 *
 * 通过获取 eflags 寄存器中的 IF 位来判断当前是否是开中断状态，
 * 如果 IF 为 1，则表示开中断，否则为关中断。
 *
 * @return enum intr_status 函数返回中断状态，INTR_ON 表示开中断，INTR_OFF 表示关中断。
 */
enum intr_status intr_get_status()
{
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    // 检测 eflags 中的 IF 位是否为 1
    // EFLAGS_IF 的值是 0x00000200，它对应的二进制形式是 0000 0000 0000 0000 0000 0010 0000 0000，这意味着只检测第 9 位
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

/**
 * @brief 开启中断，并返回之前的中断状态
 *
 * 该函数负责将中断打开，即执行 sti 指令将 eflags 中的 IF 位置 1，
 * 同时返回执行该操作前的中断状态。
 *
 * @return enum intr_status 函数返回执行 sti 前的中断状态。
 */
enum intr_status intr_enable()
{
    enum intr_status old_status;
    if (INTR_ON == intr_get_status())
    {
        old_status = INTR_ON;
        return old_status;
    }
    else
    {
        old_status = INTR_OFF;
        asm volatile("sti"); // 开中断，sti 指令将 IF 位置 1
        return old_status;
    }
}

/**
 * @brief 关闭中断，并返回之前的中断状态
 *
 * 该函数负责将中断关闭，即执行 cli 指令将 eflags 中的 IF 位置 0，
 * 同时返回执行该操作前的中断状态。
 *
 * @param void 该函数不接收任何参数。
 *
 * @return enum intr_status 函数返回执行 cli 前的中断状态。
 */
enum intr_status intr_disable()
{
    enum intr_status old_status;
    if (INTR_ON == intr_get_status())
    {
        old_status = INTR_ON;
        asm volatile("cli" : : : "memory"); // 关中断，cli 指令将 IF 位置 0
        return old_status;
    }
    else
    {
        old_status = INTR_OFF;
        return old_status;
    }
}

/**
 * @brief 设置中断状态
 * 
 * 该函数负责根据参数 status 的值设置中断状态，
 * 如果 status 为 INTR_ON 则打开中断，否则关闭中断。
 * 
 * @param enum intr_status status 指定要设置的中断状态。
 * 
 * @return enum intr_status 函数返回设置后的中断状态。
 */
enum intr_status intr_set_status(enum intr_status status)
{
    return (status & INTR_ON) ? intr_enable() : intr_disable();
}
