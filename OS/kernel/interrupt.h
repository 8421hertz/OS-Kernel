#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"

typedef void *intr_handler;


static void pic_init(void);
static void idt_desc_init(void);
void idt_init();

#endif






