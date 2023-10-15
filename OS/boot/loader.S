%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
    LOADER_STACK_TOP equ LOADER_BASE_ADDR   ;loader在实模式下的栈指针地址
    jmp loader_start

;构建GDT机器内部的描述符
    GDT_BASE:       dd 0x00000000       ;GDT_BASE为GDT的起始地址(GDT表中的低0个描述符不可用)
                    dd 0x00000000
    
    CODE_DESC:      dd 0x0000FFFF       ;低16位是段界限，高16位是段基址
                    dd DESC_CODE_HIGH4
    
    DATA_STACK_DESC dd 0x0000FFFF
                    dd DESC_DATA_HIGH4  ;DATA_STACK_DESC是数据段和栈段的段描述符，一个描述符只能定义一种扩展方向(type字段)，此扩展方向是用来配合段界限的，CPU在检查段内偏移地址的合法性时，就需要结合扩展方向和段界限来判断(段描述符的扩展方向和栈esp向下缩小无关)。

    VIDEO_DESC      dd 0x80000007       ;显存段不采用平摊模型。我们直接把段基址置为文本模式的起始地址0xb8000，段大小为0xbffff-0xb8000=0x7fff，段粒度为4K，因而段界限limit等于0x7fff/4k=7
                    dd DESC_VIDEO_HIGH4 ;此时dpl为0

    GDT_SIZE equ $ - GDT_BASE
    GDT_LIMIT equ GDT_SIZE - 1  ;GDT界限
    times 60 dq 0   ;此处预留60个描述符的空位

    ;构建代码段、数据/栈段、显存段的段选择子
    SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0     ;相当于(CODE_BASE - GDT_BASE) / 8 + TI_GDT + RPL0
    SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0
    SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

    ;以下是gdt的指针，前2字节是GDT界限，后4字节是GDT起始地址 —— 此指针是lgdt加载到GDT到gdtr寄存器时用的
    gdt_ptr  dw  GDT_LIMIT
             dd  GDT_BASE      

    loadermsg   db   '2 loader in real.'


    loader_start:
;------------------------------------------------------------------------------------------
;INT 0x10   功能号:0x13 功能描述：打印字符
;------------------------------------------------------------------------------------------
;输入:
;AH 子功能号=13H
;BH = 页码
;BL = 属性(若AL = 00H 或 01H)
;CX = 字符串长度
;(DH, DL) = 坐标(行、列)
;ES:BP = 字符串地址
;AL = 显示输出方式
;   0 —— 字符串只含有显示字符，其显示属性在BL中；显示后，光标位置不变
;   1 —— 字符串中只含有显示字符，其显示属性在BL中；显示后，光标位置改变
;   2 —— 字符串中含显示字符和显示属性。显示后，光标位置不变
;   3 —— 字符串中含有显示字符和显示属性。显示后，光标位置改变
;无返回值
    mov sp, LOADER_STACK_TOP
    mov bp, loadermsg           ;ES:BP = 字符串地址
    mov cx, 17                  ;CX = 字符串长度
    mov ax, 0x1301
    mov bx, 0x001f              ;页号为0(BH = 0) 蓝底粉红字(BL = 1fh)
    mov dx, 0x1800              ;24行，0列
    int 0x10                    ;10h号中断

;--------------------------- 准备进入保护模式 ---------------------------
;1.打开A20
;2.加载GDT
;3.将cr0的pe为置1
    ;------------- 打开A20 -------------
    in al, 0x92
    or al, 0000_0010b
    out 0x92, al

    ;------------- 加载GDT -------------
    lgdt [gdt_ptr]


    ;-------------将cr0寄存器的第0位(PE位)置1 -------------
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start    ;刷新流水线 —— 当前处于16位保护模式下，为了能使用32位偏移地址，所以添加了伪指令dword

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA   ;用选择子初始化各段寄存器
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    mov byte [gs:160], 'p'          ;往显存第80个字符的位置写入字符'P' —— 默认的显示模式是80*25，即每行都是80个字符(0~79)，每个字符占两字节，故传入偏移地址是80*2=160(显存中每个字符的低字节是字符的ASCII码，高字节是属性位)

    jmp $                           ;通过死循环，使程序悬停在此




    ;输出背景色为草绿色，前景色为红色，并且跳动的字符串"2 LOADER"
    ; mov byte [gs:0x00], '2'
    ; mov byte [gs:0x01], 0xA4        ;A表示绿色背景闪烁，4表示前景色为红色

    ; mov byte [gs:0x02], ' '
    ; mov byte [gs:0x03], 0xA4

    ; mov byte [gs:0x04], 'L'
    ; mov byte [gs:0x05], 0xA4

    ; mov byte [gs:0x06], 'O'
    ; mov byte [gs:0x07], 0xA4

    ; mov byte [gs:0x08], 'A'
    ; mov byte [gs:0x09], 0xA4

    ; mov byte [gs:0x0A], 'D'
    ; mov byte [gs:0x0B], 0xA4

    ; mov byte [gs:0x0C], 'E'
    ; mov byte [gs:0x0D], 0xA4

    ; mov byte [gs:0x0E], 'R'
    ; mov byte [gs:0x0F], 0xA4

    
