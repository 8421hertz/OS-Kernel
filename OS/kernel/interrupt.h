#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"

typedef void *intr_handler;

/**
 * 定义中断的两种状态：
 * ① INTR_OFF 值为 0，表示关中断
 * ② INTR_ON 值为 1，表示开中断
 */
enum intr_status            // 中断状态
{
    INTR_ON,                // 中断打开
    INTR_OFF                // 中断关闭
};

static void pic_init(void);
static void idt_desc_init(void);
static void general_intr_handler(uint8_t vec_nr);
static void exception_init(void);
void idt_init();
enum intr_status intr_get_status();
enum intr_status intr_enable();
enum intr_status intr_disable();
enum intr_status intr_set_status(enum intr_status status);

#endif






