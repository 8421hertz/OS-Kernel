[bits 32]
; nop 指令是占位用的，什么都不干（适用于压入错误码的中断）
%define ERROR_CODE nop              ; 若在相关异常中 CPU 已经自动压入了错误码，为保持栈中格式统一，这里不做操作
%define ZERO push 0                 ; 若在相关的异常中 CPU 没有压入错误码，为了统一栈中格式（为了 iret 能够自动返回），就收工压入一个 0

extern put_str                      ; 声明外部函数，它告诉 nasm， put_str 定义在别的文件中，链接时可以找到（注意：在 C 代码中只要将符号定义为全局变量便可以被外部引用，引用外部符号时用 extern 声明即可）

section .data
intr_str db "interrupt occur!", 0xa, 0
; 中断描述符地址数组（中断处理程序入口的地址数组）
global intr_entry_table             
intr_entry_table:

%macro VECTOR 2                     ; 宏开头
section .text
; 此标号是为了获取中断处理程序的地址（%1 代表此中断的中断向量号）
intr%1entry                         ; 每个中断处理程序都要压入中断向量号，所以一个中断类型一个中断处理程序，自己知道自己的中断向量号是多少

    ; 在预处理后会根据实际的参数展开为 nop 或 push 0
    %2
    push intr_str
    call put_str
    add esp, 4                      ; 函数调用后回收空间

    ; 如果是从从片上进入的中断，除了往从片上发送 EOI 外，还要往主片上发送 EOI。目的是为了让 8259A 知道当前中断处理程序已经执行完成
    ; 通过 8259A 的操作控制字 OCW2
    mov al, 0x20                    ; 中断结束命令 EOI
    in 0x20, al                     ; 向从片发送
    in 0xA0, al                     ; 向主片发送

    add esp, 4                      ; 跳过 error_code 或 0 占位符
    iret                            ; 从中断返回， 32 位下等同于 ireted

section .data
    dd  intr%1entry                 ; 存储各个中断处理程序入口的地址，最终会形成一个 intr_entry_table 数组

%endmacro

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
VECTOR 0x20,ZERO






































