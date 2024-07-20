%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
    LOADER_STACK_TOP equ LOADER_BASE_ADDR   ;loader在实模式下的栈指针地址
    ;jmp loader_start

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

    ;total_mem_bytes用于保存内存容量，以字节为单位，此位置比较好标记
    ;当前偏移loader.bin文件头0x200字节(512)
    ;loader.bin的加载地址是0x900
    ;故total_mem_bytes内存中的地址是0xb00
    ;故将来内核中咱们会引用此地址
    total_mem_bytes dd 0
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


    ;以下是gdt的指针，前2字节是GDT界限，后4字节是GDT起始地址 —— 此指针是lgdt加载到GDT到gdtr寄存器时用的
    gdt_ptr  dw  GDT_LIMIT
             dd  GDT_BASE
    
;人工对齐：total_mem_bytes(4) + gdt_ptr(6) + ards_buf(244) + ards_nr(2)，共256字节
    ;ards_buf是提前定义的缓冲区，用于存储BIOS 0x15中断0xe820子功能返回的ARDS结构 —— 每个ARDS结构大小为20字节
    ards_buf times 244 db 0         ;这244字节是凑出来的(实际上用于存储ARDS的空间共200字节)，无实际意义(为了对齐代码凑够0x300)
    ards_nr dw 0                    ;用于记录ARDS结构体数量

    loader_start:   ;偏移地址为0x300

;------------------------------------------------------------------------------------------
;INT 0x15   子功能号: ax = e820h    功能描述: 获取内存布局
;------------------------------------------------------------------------------------------
;int 15h eax = 0000E820h, edx = 534D4150h('SMAP') 获取内存布局
    xor ebx, ebx                ;第一次调用时，ebx值要为0
    ;此方法需要提前准备好一块数据缓冲区ards_buf，用于存放返回的ARDS结构，按照0xe820的调用方法，需要es:di存放缓冲区地址，由于es在mbr已经赋值了，所以只需要为di赋值即可
    mov di, ards_buf            ;ARDS结构缓冲区
    mov edx, 0x534D4150         ;edx只赋值一次，循环体中不会改变
.e820_mem_get_loop:             ;循环获取每个ARDS内存范围描述结构
;每执行一次int 0x15中断后，寄存器eax、ebx、ecx都会更新。eax的值由之前的子功能号变成了字符串SMAP的ASCII码，ebx为新的后续值，ecx为实际写入缓冲区中的字节数
;其中ebx不用管，原封不动即可，eax和ecx每次调用前都要更新为正确的输入参数
    mov eax, 0x0000e820         ;执行int 0x15后，eax的值会变成0x534d4150，所以每次执行int前都需要更新为更新为子功能号
    mov ecx, 20                 ;ARDS地址范围描述符结构大小是20字节
    int 0x15
    jc .e820_failed_so_try_e801 ;若cf位为1则有错误发生，尝试0xe801子功能
    add di, cx                  ;使di增加20字节指向缓冲区中新的ARDS结构位置
    inc word [ards_nr]          ;记录ARDS数量
    cmp ebx, 0                  ;在CF位为0的情况下，若返回的EBX值为0，表示这是最后一个ARDS结构
    jnz .e820_mem_get_loop

;在所有的ARDS结构中找出(base_add_low + length_high)的最大值，即内存的容量
    mov cx, [ards_nr]           ;遍历ards_buf中的每一个ARDS结构体，循环次数是ARDS的数量
    mov ebx, ards_buf           ;ARDS缓冲区
    xor edx, edx                ;edx为总内存大小
;找出最大内存块
.find_max_mem_area:
    ;无需判断type是否为1，最大的内存块一定是可以被使用的
    mov eax, [ebx]              ;base_add_low(基地址的低32位)
    add eax, [ebx + 8]          ;length_low
    add ebx, 20                 ;指向缓冲区中下一个ARDS结构
    cmp edx, eax                ;冒泡排序：找出最大，edx始终是最大的内存容量 —— 
                                ;如果edx >= eax，直接切换下一个ARDS地址；如果edx < eax，替换edx为eax
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_mem_area     ;cx为0时代表ards_buf中的ARDS结构体已经遍历完
    jmp .mem_get_ok

;------------------------------------------------------------------------------------------
;INT 0x15   子功能号: ax = e801h    功能描述: 获取内存大小，最大支持4G
;------------------------------------------------------------------------------------------
;中断返回后，ax和cx的值一样，以KB为单位，bx和dx值一样，以64KB为单位
;在ax和cx中的为内存的低16MB，在bx和dx中的为16MB到4GB
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88    ;若当前e801方法失败，就尝试0x88方法

;1.先算出低15MB的内存
    ;ax和cx是以kb为单位的内存数量，将其转换为byte为单位
    mov cx, 0x400               ;1024
    mul cx                      ;高16位在dx中，低16位在ax中
    shl edx, 16
    and eax, 0x0000FFFF
    or edx, eax
    add edx, 0x100000           ;ax只是15MB，故要加上1MB
    mov esi, edx                ;先把低15MB的内存容量存入esi寄存器备份
    
;2.再将16MB以上的内存转换为byte为单位
    ;bx和dx都是以64Kb为单位的内存数量
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000            ;0x10000为64KB
    mul ecx,                    ;高32位存入edx, 低32位存入eax
    
    add esi, eax                ;由于此方法只能测出4GB以内的内存，故32位eax就够了(edx肯定为0)
    mov edx, esi                ;edx为总内存大小
    jmp .mem_get_ok

;------------------------------------------------------------------------------------------
;INT 0x15   子功能号: ax = 88h    功能描述: 获取内存大小，只获取64MB之内
;------------------------------------------------------------------------------------------
.e801_failed_so_try88:
    ;int 0x15后，ax存入的是以KB为单位的内存容量
    mov ah, 0x88
    int 0x15
    ;jc .error_hlt
    and eax, 0x0000FFFF

    ;16位乘法中，被乘数是ax，乘积为32位。高位位于dx中；低位位于ax中
    mov cx, 0x400               ;0x400为1024，将ax中的内存容量换位byte为单位
    mul cx
    shl edx, 16                 ;把dx移到高16位
    and eax, 0x0000FFFF         
    or edx, eax                 ;把积的低16位组合刀edx，为32位的积
    add edx, 0x100000           ;0x88子功能只会返回1MB以上的内存(实际大小还需要加上1MB)

.mem_get_ok:
    mov [total_mem_bytes], edx  ;将内存换位byte单位后存入total_mem_bytes22222



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
    ; mov sp, LOADER_STACK_TOP
    ; mov bp, loadermsg           ;ES:BP = 字符串地址
    ; mov cx, 17                  ;CX = 字符串长度
    ; mov ax, 0x1301
    ; mov bx, 0x001f              ;页号为0(BH = 0) 蓝底粉红字(BL = 1fh)
    ; mov dx, 0x1800              ;24行，0列
    ; int 0x10                    ;10h号中断

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

    
