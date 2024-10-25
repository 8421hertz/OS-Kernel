[bits 32]
; nop 指令是占位用的，什么都不干（适用于压入错误码的中断）
%define ERROR_CODE nop              ; 若在相关异常中 CPU 已经自动压入了错误码，为保持栈中格式统一，这里不做操作
%define ZERO push 0                 ; 若在相关的异常中 CPU 没有压入错误码，为了统一栈中格式（为了 iret 能够自动返回），就收工压入一个 0

extern idt_table
;extern put_str                      ; 声明外部函数，它告诉 nasm，put_str 定义在别的文件中，链接时可以找到（注意：在 C 代码中只要将符号定义为全局变量便可以被外部引用，引用外部符号时用 extern 声明即可）

section .data
; 中断描述符地址数组（中断处理程序入口的地址数组）
global intr_entry_table             
intr_entry_table:

%macro VECTOR 2                     ; 宏开头
section .text
; 此标号是为了获取中断处理程序的地址（%1 代表此中断的中断向量号）
intr%1entry:                        ; 每个中断处理程序都要压入中断向量号，所以一个中断类型一个中断处理程序，自己知道自己的中断向量号是多少

    ; 在预处理后会根据实际的参数展开为 nop 或 push 0
    %2  ; 中断若有错误码会压在eip后面 
    
    ; 以下是保存上下文环境（因为在此汇编文件中要调用 C 程序，一定会使当前寄存器环境破坏）
    ; ds、 es、 fs 和 gs 这 4 个 16 位段寄存器压栈，虽然是 16 位，但在 32 位下段寄存器压栈有点特殊，入栈后要占用 4 字节（而位于其他寄存器和内存中的值若为 16 位，则入栈后只占 2 字节）
    push ds
    push es
    push fs
    push gs
    pushad                          ; pushad 指令压入 32 位寄存器，其入栈顺序是：EAX->ECX->EDX->EBX->ESP->EBP->ESI-> EDI

    ; 如果是从从片上进入的中断，除了往从片上发送 EOI 外，还要往主片上发送 EOI。目的是为了让 8259A 知道当前中断处理程序已经执行完成
    ; 通过 8259A 的操作控制字 OCW2
    mov al, 0x20                    ; 中断结束命令 EOI
    out 0x20, al                    ; 向从片发送
    out 0xA0, al                    ; 向主片发送

    push %1                         ; 不管 idt_table 中的目标程序是否需要参数，都一律压入中断向量号，调试时很方便
    ; 其中 [idt_table+ %1*4] 是 32 位下的基址变址寻址，由于 idt_table 中的每个元素都是 32 位地址，故占用 4 字节大小，
    ; 所以将向量号乘以 4，再加上 idt_table 数组的起始地址，便得到了下标为中断向量号%1 的数组元素地址，
    ; 再对该地址通过中括号[]取值，便得到了该数组元素中所指向 C 语言编写的中断处理程序，也就是之前在 exception_init 函数中注册过的 general_intr_handler。
    call [idt_table + %1 * 4]       ; 调用 idt_table 中的 C 版本中断处理函数
    jmp intr_exit

section .data
    dd  intr%1entry                 ; 存储各个中断处理程序入口的地址，最终会形成一个 intr_entry_table 数组

%endmacro

section .text
global intr_exit
intr_exit:
; 以下是恢复上下文环境
    add esp, 4                      ; 跳过中断向量号
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4                      ; 跳过 error_code 和 0 占位符
    iretd

VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO 
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO 
VECTOR 0x08,ERROR_CODE
VECTOR 0x09,ZERO
VECTOR 0x0a,ERROR_CODE
VECTOR 0x0b,ERROR_CODE 
VECTOR 0x0c,ZERO
VECTOR 0x0d,ERROR_CODE
VECTOR 0x0e,ERROR_CODE
VECTOR 0x0f,ZERO 
VECTOR 0x10,ZERO
VECTOR 0x11,ERROR_CODE
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO 
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO 
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE 
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE
VECTOR 0x1f,ZERO 
VECTOR  0x20, ZERO  ;时钟中断对应的入口
VECTOR  0x21, ZERO  ;键盘中断对应的入口
VECTOR  0x22, ZERO  ;级联用的
VECTOR  0x23, ZERO  ;串口 2 对应的入口
VECTOR  0x24, ZERO  ;串口 1 对应的入口
VECTOR  0x25, ZERO  ;并口 2 对应的入口
VECTOR  0x26, ZERO  ;软盘对应的入口
VECTOR  0x27, ZERO  ;并口 1 对应的入口
VECTOR  0x28, ZERO  ;实时时钟对应的入口
VECTOR  0x29, ZERO  ;重定向
VECTOR  0x2a, ZERO  ;保留
VECTOR  0x2b, ZERO  ;保留
VECTOR  0x2c, ZERO  ;ps/2 鼠标
VECTOR  0x2d, ZERO  ;fpu 浮点单元异常
VECTOR  0x2e, ZERO  ;硬盘
VECTOR  0x2f, ZERO  ;保留