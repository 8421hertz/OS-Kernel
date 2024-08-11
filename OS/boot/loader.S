%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
    LOADER_STACK_TOP equ LOADER_BASE_ADDR   ;loader 在实模式下的栈指针地址
    ;jmp loader_start

; 构建 GDT 机器内部的描述符
    GDT_BASE:       dd 0x00000000       ;GDT_BASE 为 GDT 的起始地址 (GDT 表中的低 0 个描述符不可用 )
                    dd 0x00000000
    
    CODE_DESC:      dd 0x0000FFFF       ; 低 16 位是段界限，高 16 位是段基址
                    dd DESC_CODE_HIGH4

    DATA_STACK_DESC dd 0x0000FFFF
                    dd DESC_DATA_HIGH4  ;DATA_STACK_DESC 是数据段和栈段的段描述符，一个描述符只能定义一种扩展方向 (type 字段 )，此扩展方向是用来配合段界限的， CPU 在检查段内偏移地址的合法性时，就需要结合扩展方向和段界限来判断 ( 段描述符的扩展方向和栈 esp 向下缩小无关 )。

    VIDEO_DESC      dd 0x80000007       ; 显存段不采用平摊模型。我们直接把段基址置为文本模式的起始地址 0xb8000 ，段大小为 0xbffff-0xb8000=0x7fff，段粒度为 4K，因而段界限 limit 等于 0x7fff/4k=7
                    dd DESC_VIDEO_HIGH4 ; 此时 dpl 为 0

    GDT_SIZE equ $ - GDT_BASE
    GDT_LIMIT equ GDT_SIZE - 1  ;GDT 界限
    times 60 dq 0   ; 此处预留 60 个描述符的空位

    ; 构建代码段、数据 / 栈段、显存段的段选择子
    SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0     ; 相当于 (CODE_BASE - GDT_BASE) / 8 + TI_GDT + RPL0
    SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0
    SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

    ;total_mem_bytes 用于保存内存容量，以字节为单位，此位置比较好标记
    ; 当前偏移 loader.bin 文件头 0x200 字节 (512)
    ;loader.bin 的加载地址是 0x900
    ; 故 total_mem_bytes 内存中的地址是 0xb00
    ; 故将来内核中咱们会引用此地址
    total_mem_bytes dd 0
    ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


    ; 以下是 gdt 的指针，前 2 字节是 GDT 界限，后 4 字节是 GDT 起始地址 —— 此指针是 lgdt 加载到 GDT 到 gdtr 寄存器时用的
    gdt_ptr  dw  GDT_LIMIT
             dd  GDT_BASE
    
    ; 人工对齐： total_mem_bytes(4) + gdt_ptr(6) + ards_buf(244) + ards_nr(2)，共 256 字节
    ;ards_buf 是提前定义的缓冲区，用于存储 BIOS 0x15 中断 0xe820 子功能返回的 ARDS 结构 —— 每个 ARDS 结构大小为 20 字节
    ards_buf times 244 db 0         ; 这 244 字节是凑出来的 ( 实际上用于存储 ARDS 的空间共 200 字节 )，无实际意义 ( 为了对齐代码凑够 0x300)
    ards_nr dw 0                    ; 用于记录 ARDS 结构体数量

    loader_start:   ; 偏移地址为 0x300

;------------------------------------------------------------------------------------------
;INT 0x15   子功能号 : ax = e820h    功能描述 : 获取内存布局
;------------------------------------------------------------------------------------------
;int 15h eax = 0000E820h, edx = 534D4150h('SMAP') 获取内存布局
    xor ebx, ebx                ; 第一次调用时， ebx 值要为 0
    ; 此方法需要提前准备好一块数据缓冲区 ards_buf，用于存放返回的 ARDS 结构，按照 0xe820 的调用方法，需要 es:di 存放缓冲区地址，由于 es 在 mbr 已经赋值了，所以只需要为 di 赋值即可
    mov di, ards_buf            ;ARDS 结构缓冲区
    mov edx, 0x534D4150         ;edx 只赋值一次，循环体中不会改变
.e820_mem_get_loop:             ; 循环获取每个 ARDS 内存范围描述结构
; 每执行一次 int 0x15 中断后，寄存器 eax、 ebx、 ecx 都会更新。 eax 的值由之前的子功能号变成了字符串 SMAP 的 ASCII 码， ebx 为新的后续值， ecx 为实际写入缓冲区中的字节数
; 其中 ebx 不用管，原封不动即可， eax 和 ecx 每次调用前都要更新为正确的输入参数
    mov eax, 0x0000e820         ; 执行 int 0x15 后， eax 的值会变成 0x534d4150 ，所以每次执行 int 前都需要更新为更新为子功能号
    mov ecx, 20                 ;ARDS 地址范围描述符结构大小是 20 字节
    int 0x15
    jc .e820_failed_so_try_e801 ; 若 cf 位为 1 则有错误发生，尝试 0xe801 子功能
    add di, cx                  ; 使 di 增加 20 字节指向缓冲区中新的 ARDS 结构位置
    inc word [ards_nr]          ; 记录 ARDS 数量
    cmp ebx, 0                  ; 在 CF 位为 0 的情况下，若返回的 EBX 值为 0 ，表示这是最后一个 ARDS 结构
    jnz .e820_mem_get_loop

; 在所有的 ARDS 结构中找出 (base_add_low + length_high) 的最大值，即内存的容量
    mov cx, [ards_nr]           ; 遍历 ards_buf 中的每一个 ARDS 结构体，循环次数是 ARDS 的数量
    mov ebx, ards_buf           ;ARDS 缓冲区
    xor edx, edx                ;edx 为总内存大小
; 找出最大内存块
.find_max_mem_area:
    ; 无需判断 type 是否为 1 ，最大的内存块一定是可以被使用的
    mov eax, [ebx]              ;base_add_low( 基地址的低 32 位 )
    add eax, [ebx + 8]          ;length_low
    add ebx, 20                 ; 指向缓冲区中下一个 ARDS 结构
    cmp edx, eax                ; 冒泡排序：找出最大， edx 始终是最大的内存容量 —— 
                                ; 如果 edx >= eax，直接切换下一个 ARDS 地址；如果 edx < eax，替换 edx 为 eax
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_mem_area     ;cx 为 0 时代表 ards_buf 中的 ARDS 结构体已经遍历完
    jmp .mem_get_ok

;------------------------------------------------------------------------------------------
;INT 0x15   子功能号 : ax = e801h    功能描述 : 获取内存大小，最大支持 4G
;------------------------------------------------------------------------------------------
; 中断返回后， ax 和 cx 的值一样，以 KB 为单位， bx 和 dx 值一样，以 64KB 为单位
; 在 ax 和 cx 中的为内存的低 16MB，在 bx 和 dx 中的为 16MB 到 4GB
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88    ; 若当前 e801 方法失败，就尝试 0x88 方法

;1. 先算出低 15MB 的内存
    ;ax 和 cx 是以 kb 为单位的内存数量，将其转换为 byte 为单位
    mov cx, 0x400               ;1024
    mul cx                      ; 高 16 位在 dx 中，低 16 位在 ax 中
    shl edx, 16
    and eax, 0x0000FFFF
    or edx, eax
    add edx, 0x100000           ;ax 只是 15MB，故要加上 1MB
    mov esi, edx                ; 先把低 15MB 的内存容量存入 esi 寄存器备份
    
;2. 再将 16MB 以上的内存转换为 byte 为单位
    ;bx 和 dx 都是以 64Kb 为单位的内存数量
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000            ;0x10000 为 64KB
    mul ecx,                    ; 高 32 位存入 edx, 低 32 位存入 eax
    
    add esi, eax                ; 由于此方法只能测出 4GB 以内的内存，故 32 位 eax 就够了 (edx 肯定为 0)
    mov edx, esi                ;edx 为总内存大小
    jmp .mem_get_ok

;------------------------------------------------------------------------------------------
;INT 0x15   子功能号 : ax = 88h    功能描述 : 获取内存大小，只获取 64MB 之内
;------------------------------------------------------------------------------------------
.e801_failed_so_try88:
    ;int 0x15 后， ax 存入的是以 KB 为单位的内存容量
    mov ah, 0x88
    int 0x15
    ;jc .error_hlt
    and eax, 0x0000FFFF

    ;16 位乘法中，被乘数是 ax，乘积为 32 位。高位位于 dx 中；低位位于 ax 中
    mov cx, 0x400               ;0x400 为 1024 ，将 ax 中的内存容量换位 byte 为单位
    mul cx
    shl edx, 16                 ; 把 dx 移到高 16 位
    and eax, 0x0000FFFF         
    or edx, eax                 ; 把积的低 16 位组合刀 edx，为 32 位的积
    add edx, 0x100000           ;0x88 子功能只会返回 1MB 以上的内存 ( 实际大小还需要加上 1MB)

.mem_get_ok:
    mov [total_mem_bytes], edx  ; 将内存换位 byte 单位后存入 total_mem_bytes22222



;------------------------------------------------------------------------------------------
;INT 0x10   功能号 :0x13 功能描述：打印字符
;------------------------------------------------------------------------------------------
; 输入 :
;AH 子功能号 =13H
;BH = 页码
;BL = 属性 ( 若 AL = 00H 或 01H)
;CX = 字符串长度
;(DH, DL) = 坐标 ( 行、列 )
;ES:BP = 字符串地址
;AL = 显示输出方式
;   0 —— 字符串只含有显示字符，其显示属性在 BL 中；显示后，光标位置不变
;   1 —— 字符串中只含有显示字符，其显示属性在 BL 中；显示后，光标位置改变
;   2 —— 字符串中含显示字符和显示属性。显示后，光标位置不变
;   3 —— 字符串中含有显示字符和显示属性。显示后，光标位置改变
; 无返回值
    ; mov sp, LOADER_STACK_TOP
    ; mov bp, loadermsg           ;ES:BP = 字符串地址
    ; mov cx, 17                  ;CX = 字符串长度
    ; mov ax, 0x1301
    ; mov bx, 0x001f              ; 页号为 0(BH = 0) 蓝底粉红字 (BL = 1fh)
    ; mov dx, 0x1800              ;24 行， 0 列
    ; int 0x10                    ;10h 号中断

;--------------------------- 准备进入保护模式 ---------------------------
;1. 打开 A20
;2. 加载 GDT
;3. 将 cr0 的 pe 为置 1
    ;------------- 打开 A20 -------------
    in al, 0x92
    or al, 0000_0010b
    out 0x92, al

    ;------------- 加载 GDT -------------
    lgdt [gdt_ptr]


    ;------------- 将 cr0 寄存器的第 0 位 (PE 位 ) 置 1 -------------
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start    ; 刷新流水线 —— 当前处于 16 位保护模式下，为了能使用 32 位偏移地址，所以添加了伪指令 dword
                                            ; 这将导致之前做的预测失效，从而起到了刷新的作用
                                            ; 出错则挂起


[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA   ; 用选择子初始化各段寄存器
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

;------------------------------- 加载 Kernel -------------------------------
;rd_disk_m_32 函数的参数：
;   ① eax： kernel 内核所在的扇区号
;   ② ebx：内核从磁盘读出后，写入到内存的地址
;   ③ ecx：读入的扇区数
    mov eax, KERNEL_START_SECTOR            ;kernel 所在的扇区号 0x9
    mov ebx, KERNEL_BIN_BASE_ADDR           ; 从磁盘读出后，写入到内存地址（ 0x70000 ）
    mov ecx, 200                            ; 读入的扇区数

    call rd_disk_m_32       ; 读取硬盘（将硬盘中的数据读入内存中），此函数所有的寄存器都变成 32 位的
    
    ; 创建页目录及页表并初始化页内存位图
    call setup_page

;------------------------------- 重启加载 GDT 做准备 -------------------------------

    ; 要将描述符表地址及偏移量写入内存 gdt_ptr，一会用新地址重新加载
    sgdt [gdt_ptr]                  ; 存储到原来 gdt 所有的位置        

    ; 因为我们在页表中会将内核放置到 3GB 以上的地址，我们也把 GDT 放在内核的地址空间，
    ; 在此通过 sgdt 指令，将 GDT 的起始地址和偏移量信息 dump（像倒水一样）出来，依然存放到 gdt_ptr 处
    ; 一会儿待条件成熟时，我们再从地址 gdt_ptr 处重新加载 GDT。

    ; 将 gdt 描述符中视频段描述符中的段基址 +0xc0000000
    mov ebx, [gdt_ptr + 2]          ;gdt 的基址
    or dword [ebx + 0x18 + 4], 0xc0000000       ; 视频段是第 3 个描述符，每个描述符是 8 字节，故 0x18
                                                ; 段描述符的最高 4 字节的最高位是段基址 31~24 位

    ; 将 gdt 的基址加上 0xc0000000 ，使其成为内核所在的高地址
    add dword [gdt_ptr + 2], 0xc0000000

    add esp, 0xc0000000             ; 将栈指针同样映射到内核地址

    ; 把页目录表地址赋给 cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    ; 打开 cr0 的 pg 位（第 31 位）
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdt_ptr]


;;;;;;;;;;;;;;;;;;;;;;;;;;;;  此时不刷新流水线也没问题  ;;;;;;;;;;;;;;;;;;;;;;;;
; 由于一直处在 32 位下（不需要 “显示的清空流水线” ）, 原则上不需要强制刷新，经过实际测试没有以下这两句也没问题 .
; 但以防万一，还是加上啦，免得将来出来莫句奇妙的问题 .
    jmp SELECTOR_CODE:enter_kernel
enter_kernel:
    call kernel_init
    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT                    ; 用地址 0x1500 进行访问测试

;-----------------   将 kernel.bin 中的 segment 拷贝到编译的地址   -----------------
kernel_init:                ; 函数 kernel_init 的作用：将 kernel.bin 中的段（ segment）拷贝到各段自己被编译的虚拟地址处，将这些段单独提取到内存中，这就是平时所说的内存中的程序映像。
                            ;kernel_init 的原理：分析程序中的每个段（ segment），如果段类型不是 PT_NULL（空程序类型），就将该段拷贝到编译的地址中。
    xor ebx, ebx            ;ebx 记录程序头表地址
    xor ecx, ecx            ;cx 记录程序头表中的 program header 程序头数量
    xor edx, edx            ;dx 记录 program header 程序表尺寸，即 e_phentsize（遍历段的方式是：指向第一个程序头后，每次增加一个段头的大小，该属性位于偏移程序开头 42 字节处， e_phentsize）

    mov dx, [KERNEL_BIN_BASE_ADDR + 42]     ;e_phentsize
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]    ; 偏移文件开始部分 28 字节的地方是 e_phoff, 表示第 1 个 program header 在文件中的偏移量
    add ebx, KERNEL_BIN_BASE_ADDR          ; 此时 ebx 作为程序头表的基址，用它来遍历每一个段（此时 ebx 指向程序中第 1 个 program header 的物理地址）
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]     ; 偏移文件开始部分 28 字节的地方是 e_phnum，表示有几个 program header

.each_segment:
    cmp byte [ebx + 0], PT_NULL             ; 若 p_type 等于 PT_NULL，说明此 program header 未使用
    je .PT_NULL

    ; 为函数 memcpy 压入参数（参数是从右往左依然压入），函数原型类似于 memcpy(dst, src, size）
    push dword [ebx + 16]                   ;———— 压入函数 memcpy 的第三个参数： size 段大小
                                                ;program header 中偏移 16 字节的地方是 p_filesz（本段在文件中的大小） 
    mov eax, [ebx + 4]                      ; 距程序头偏移量为 4 字节的地方是 p_offset（本段在文件内的起始偏移字节） 
    add eax, KERNEL_BIN_BASE_ADDR           ; 加上 kernel.bin 被加载到的物理地址，使 eax 为该段的物理地址
    push eax                                ;———— 压入函数 memcpy 的第二个参数： src 源地址
    push dword [ebx + 8]                    ;———— 压入函数 memcpy 的第一个参数： dst 目的地址
                                                ; 偏移程序头 8 字节的位置是 p_vaddr，这就是目的地址
    call mem_cpy                        ; 调用 mem_cpy 完成段复制
    add esp, 12                         ; 清理栈中压入的三个参数

.PT_NULL:
    add ebx, edx                            ;edx 为 program header 大小（ e_phentsize），使 ebx 指向下一个 program header
    loop .each_segment
    ret

;-------------------- 逐字节拷贝 mem_cpy(dst, src, size) --------------------
; 输入 : 栈中三个参数 (dst, src, size)
; 输出 : 无
;---------------------------------------------------------------------------
mem_cpy:
    cld                                 ; 将 eflags 寄存器中的方向标志位 DF 置为 0
    push ebp                            ; 我们访问栈中的参数是基于 ebp 的，需要现将 ebp 备份
    mov ebp, esp
    push ecx                            ;rep 指令用到了 ecx，但是 ecx 对于外层段的循环还得用，需要先入栈备份
    mov edi, [ebp + 8]          ;dst
    mov esi, [ebp + 12]         ;src
    mov ecx, [ebp + 16]         ;size
    rep movsb

    ; 恢复环境
    pop ecx
    pop ebp
    ret




;-------------------------- 创建页目录及页表并初始化页内存位图 --------------------------
setup_page:
    ; 先把页目录占用的空间逐字节清 0 （每个页目录项占用 4B， 1000 个页目录项占用 4K 的空间）
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir
; 开始创建页目录项（ PDE）
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS     ; 属性为 7
    add eax, 0x1000                 ;eax 为页目录项的结构 : ① 31~12 位为页表的物理地址的前 20 位，后 12 位都是 0
                                    ;                   ② 0~11 位为页目录项的属性
    mov ebx, eax                    ; 此处为 ebx 赋值，是为 .create_pte 做准备， ebx 是基址

    ; 将 0 号和 768 号页目录项都指向第 1 个页表（ 0x101000 ）
    or eax, PG_US_U | PG_RW_W | PG_P
    mov [PAGE_DIR_TABLE_POS + 0x00], eax        ;1 号页目录项指向 1 号页表
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax       ;768 号页目录项指向 0 号页表

    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax        ; 使最后一个页目项指向页目录表的地址（ 1024*4B-4=4092 ）

    ; 下面创建页表项（ PTE）
    mov ecx, 256                    ;1M 低端内存 / 每页大小 4k = 256 个物理页，所以 0 和 769 页目录项对应的页表只需要创建 256 个页表项即可
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P           ; 属性为 7
.create_pte:
    mov [ebx+esi*4], edx            ; 此时的 ebx 是 0x101000 （ 0 号页表中 0 号页表项的地址）
                                    ;      edx 是第 0 号表项中的物理页的首地址（每一个物理页占 4K），对应的地址为 0~4095 （ 0xFFF）。
                                    ;       低端 1MB 的虚拟地址会对应低端 1MB 的物理地址
    add edx, 4096                   ;1 号页表项中的物理页的首地址（ PDE 和 PTE 的结构中，低 12 位是属性位，但是分页机制会自动将地址的后 12 位置为 0 ）
    inc esi                         ; 用于定位到下一个页表项的位置
    loop .create_pte

    ; 创建内核其它页表的 PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000                 ; 此时 eax 为第 2 个页表物理位置
    or eax, PG_US_U | PG_RW_W | PG_P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254                    ; 范围为第 769~1022 的所有目录项数量（ 768 号目录项指向 0 号页表；最后一个页目录项 1023 号指向页目录表）
                                    ; 为了实现内核完全共享，需要把 768~1022 号页目录项都映射到低端（用于应对内核申请大量空间的情况）
    mov esi, 769                    ; 第 769 个页目录项
.create_kernel_pde:              
    mov [ebx+esi*4], eax            ; 将 1 号页表项物理地址放入 769 号页目录项中
    inc esi                         ; 定位到下一个页目录项的地址
    add eax, 0x1000                 ; 定位到下一个页表的地址
    loop .create_kernel_pde

    ret

;-------------------------------------------------------------------------------
			   ; 功能 : 读取硬盘 n 个扇区
rd_disk_m_32:	   
;-------------------------------------------------------------------------------
							 ; eax=LBA 起始逻辑扇区号
							 ; ebx= 将数据写入的内存地址
							 ; ecx= 读入的扇区数

    mov esi, eax            ; 备份扇区号到 esi
    mov di, cx              ; 备份要读入的扇区数到 di

; 读写硬盘 :
; 第 1 步：设置要读取的扇区数
      mov dx,0x1f2
      mov al,cl
      out dx,al            ; 读取的扇区数

      mov eax,esi	   ; 恢复 ax

; 第 2 步：设置起始 LBA 扇区号

      ;LBA 地址 7~0 位写入端口 0x1f3
      mov dx,0x1f3                       
      out dx,al                          

      ;LBA 地址 15~8 位写入端口 0x1f4
      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      ;LBA 地址 23~16 位写入端口 0x1f5
      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f	   ;lba 第 24~27 位
      or al,0xe0	   ; 设置 7 ～ 4 位为 1110, 表示 lba 模式
      mov dx,0x1f6
      out dx,al

; 第 3 步：向 0x1f7 端口写入读命令， 0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;;;;;;; 至此 , 硬盘控制器便从指定的 lba 地址 (eax) 处 , 读出连续的 cx 个扇区 , 下面检查硬盘状态 , 不忙就能把这 cx 个扇区的数据读出来

; 第 4 步：检测硬盘状态
  .not_ready:		   ; 测试 0x1f7 端口 (status 寄存器 ) 的的 BSY 位
      ; 同一端口 , 写时表示写入命令字 , 读时表示读入硬盘状态
      nop
      in al,dx
      and al,0x88	   ; 第 4 位为 1 表示硬盘控制器已准备好数据传输 , 第 7 位为 1 表示硬盘忙
      cmp al,0x08
      jnz .not_ready	   ; 若未准备好 , 继续等。

; 第 5 步：从 0x1f0 端口读数据
      mov ax, di	   ; 以下从硬盘端口读数据用 insw 指令更快捷 , 不过尽可能多的演示命令使用 ,
			   ; 在此先用这种方法 , 在后面内容会用到 insw 和 outsw 等

      mov dx, 256	   ;di 为要读取的扇区数 , 一个扇区有 512 字节 , 每次读入一个字 , 共需 di*512/2 次 , 所以 di*256
      mul dx
      mov cx, ax	   
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx		
      mov [ebx], ax
      add ebx, 2
			  ; 由于在实模式下偏移地址为 16 位 , 所以用 bx 只会访问到 0~FFFFh 的偏移。
			  ; loader 的栈指针为 0x900,bx 为指向的数据输出缓冲区 , 且为 16 位，
			  ; 超过 0xffff 后 ,bx 部分会从 0 开始 , 所以当要读取的扇区数过大 , 待写入的地址超过 bx 的范围时，
			  ; 从硬盘上读出的数据会把 0x0000~0xffff 的覆盖，
			  ; 造成栈被破坏 , 所以 ret 返回时 , 返回地址被破坏了 , 已经不是之前正确的地址 ,
			  ; 故程序出会错 , 不知道会跑到哪里去。
			  ; 所以改为 ebx 代替 bx 指向缓冲区 , 这样生成的机器码前面会有 0x66 和 0x67 来反转。
			  ; 0X66 用于反转默认的操作数大小 ! 0X67 用于反转默认的寻址方式 .
			  ; cpu 处于 16 位模式时 , 会理所当然的认为操作数和寻址都是 16 位 , 处于 32 位模式时 ,
			  ; 也会认为要执行的指令是 32 位 .
			  ; 当我们在其中任意模式下用了另外模式的寻址方式或操作数大小 ( 姑且认为 16 位模式用 16 位字节操作数，
			  ; 32 位模式下用 32 字节的操作数 ) 时 , 编译器会在指令前帮我们加上 0x66 或 0x67 ，
			  ; 临时改变当前 cpu 模式到另外的模式下 .
			  ; 假设当前运行在 16 位模式 , 遇到 0X66 时 , 操作数大小变为 32 位 .
			  ; 假设当前运行在 32 位模式 , 遇到 0X66 时 , 操作数大小变为 16 位 .
			  ; 假设当前运行在 16 位模式 , 遇到 0X67 时 , 寻址方式变为 32 位寻址
			  ; 假设当前运行在 32 位模式 , 遇到 0X67 时 , 寻址方式变为 16 位寻址 .

      loop .go_on_read
      ret












    ; 输出背景色为草绿色，前景色为红色，并且跳动的字符串 "2 LOADER"
    ; mov byte [gs:0x00], '2'
    ; mov byte [gs:0x01], 0xA4        ;A 表示绿色背景闪烁， 4 表示前景色为红色

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

    
;rd_disk_m_32 函数的参数：
;   ① eax： kernel 内核所在的扇区号
;   ② ebx：内核从磁盘读出后，写入到内存的地址
;   ③ ecx：读入的扇区数
    mov eax, KERNEL_START_SECTOR            ;kernel 所在的扇区号 0x9
    mov ebx, KERNEL_BIN_BASE_ADDR           ; 从磁盘读出后，写入到内存地址（ 0x70000 ）
    mov ecx, 200                            ; 读入的扇区数

    call rd_disk_m_32       ; 读取硬盘（将硬盘中的数据读入内存中），此函数所有的寄存器都变成 32 位的
    
    ; 创建页目录及页表并初始化页内存位图
    call setup_page

;------------------------------- 重启加载 GDT 做准备 -------------------------------

    ; 要将描述符表地址及偏移量写入内存 gdt_ptr，一会用新地址重新加载
    sgdt [gdt_ptr]                  ; 存储到原来 gdt 所有的位置        

    ; 因为我们在页表中会将内核放置到 3GB 以上的地址，我们也把 GDT 放在内核的地址空间，
    ; 在此通过 sgdt 指令，将 GDT 的起始地址和偏移量信息 dump（像倒水一样）出来，依然存放到 gdt_ptr 处
    ; 一会儿待条件成熟时，我们再从地址 gdt_ptr 处重新加载 GDT。

    ; 将 gdt 描述符中视频段描述符中的段基址 +0xc0000000
    mov ebx, [gdt_ptr + 2]          ;gdt 的基址
    or dword [ebx + 0x18 + 4], 0xc0000000       ; 视频段是第 3 个描述符，每个描述符是 8 字节，故 0x18
                                                ; 段描述符的最高 4 字节的最高位是段基址 31~24 位

    ; 将 gdt 的基址加上 0xc0000000 ，使其成为内核所在的高地址
    add dword [gdt_ptr + 2], 0xc0000000

    add esp, 0xc0000000             ; 将栈指针同样映射到内核地址

    ; 把页目录表地址赋给 cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    ; 打开 cr0 的 pg 位（第 31 位）
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdt_ptr]


;;;;;;;;;;;;;;;;;;;;;;;;;;;;  此时不刷新流水线也没问题  ;;;;;;;;;;;;;;;;;;;;;;;;
; 由于一直处在 32 位下（不需要 “显示的清空流水线” ）, 原则上不需要强制刷新，经过实际测试没有以下这两句也没问题 .
; 但以防万一，还是加上啦，免得将来出来莫句奇妙的问题 .
    jmp SELECTOR_CODE:enter_kernel
enter_kernel:
    call kernel_init
    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT                    ; 用地址 0x1500 进行访问测试

;-----------------   将 kernel.bin 中的 segment 拷贝到编译的地址   -----------------
kernel_init:                ; 函数 kernel_init 的作用：将 kernel.bin 中的段（ segment）拷贝到各段自己被编译的虚拟地址处，将这些段单独提取到内存中，这就是平时所说的内存中的程序映像。
                            ;kernel_init 的原理：分析程序中的每个段（ segment），如果段类型不是 PT_NULL（空程序类型），就将该段拷贝到编译的地址中。
    xor ebx, ebx            ;ebx 记录程序头表地址
    xor ecx, ecx            ;cx 记录程序头表中的 program header 程序头数量
    xor edx, edx            ;dx 记录 program header 程序表尺寸，即 e_phentsize（遍历段的方式是：指向第一个程序头后，每次增加一个段头的大小，该属性位于偏移程序开头 42 字节处， e_phentsize）

    mov dx, [KERNEL_BIN_BASE_ADDR + 42]     ;e_phentsize
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]    ; 偏移文件开始部分 28 字节的地方是 e_phoff, 表示第 1 个 program header 在文件中的偏移量
    add ebx, KERNEL_BIN_BASE_ADDR          ; 此时 ebx 作为程序头表的基址，用它来遍历每一个段（此时 ebx 指向程序中第 1 个 program header 的物理地址）
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]     ; 偏移文件开始部分 28 字节的地方是 e_phnum，表示有几个 program header

.each_segment:
    cmp byte [ebx + 0], PT_NULL             ; 若 p_type 等于 PT_NULL，说明此 program header 未使用
    je .PT_NULL

    ; 为函数 memcpy 压入参数（参数是从右往左依然压入），函数原型类似于 memcpy(dst, src, size）
    push dword [ebx + 16]                   ;———— 压入函数 memcpy 的第三个参数： size 段大小
                                                ;program header 中偏移 16 字节的地方是 p_filesz（本段在文件中的大小） 
    mov eax, [ebx + 4]                      ; 距程序头偏移量为 4 字节的地方是 p_offset（本段在文件内的起始偏移字节） 
    add eax, KERNEL_BIN_BASE_ADDR           ; 加上 kernel.bin 被加载到的物理地址，使 eax 为该段的物理地址
    push eax                                ;———— 压入函数 memcpy 的第二个参数： src 源地址
    push dword [ebx + 8]                    ;———— 压入函数 memcpy 的第一个参数： dst 目的地址
                                                ; 偏移程序头 8 字节的位置是 p_vaddr，这就是目的地址
    call mem_cpy                        ; 调用 mem_cpy 完成段复制
    add esp, 12                         ; 清理栈中压入的三个参数

.PT_NULL:
    add ebx, edx                            ;edx 为 program header 大小（ e_phentsize），使 ebx 指向下一个 program header
    loop .each_segment
    ret

;-------------------- 逐字节拷贝 mem_cpy(dst, src, size) --------------------
; 输入 : 栈中三个参数 (dst, src, size)
; 输出 : 无
;---------------------------------------------------------------------------
mem_cpy:
    cld                                 ; 将 eflags 寄存器中的方向标志位 DF 置为 0
    push ebp                            ; 我们访问栈中的参数是基于 ebp 的，需要现将 ebp 备份
    mov ebp, esp
    push ecx                            ;rep 指令用到了 ecx，但是 ecx 对于外层段的循环还得用，需要先入栈备份
    mov edi, [ebp + 8]          ;dst
    mov esi, [ebp + 12]         ;src
    mov ecx, [ebp + 16]         ;size
    rep movsb

    ; 恢复环境
    pop ecx
    pop ebp
    ret




;-------------------------- 创建页目录及页表并初始化页内存位图 --------------------------
setup_page:
    ; 先把页目录占用的空间逐字节清 0 （每个页目录项占用 4B， 1000 个页目录项占用 4K 的空间）
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir
; 开始创建页目录项（ PDE）
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS     ; 属性为 7
    add eax, 0x1000                 ;eax 为页目录项的结构 : ① 31~12 位为页表的物理地址的前 20 位，后 12 位都是 0
                                    ;                   ② 0~11 位为页目录项的属性
    mov ebx, eax                    ; 此处为 ebx 赋值，是为 .create_pte 做准备， ebx 是基址

    ; 将 0 号和 768 号页目录项都指向第 1 个页表（ 0x101000 ）
    or eax, PG_US_U | PG_RW_W | PG_P
    mov [PAGE_DIR_TABLE_POS + 0x00], eax        ;1 号页目录项指向 1 号页表
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax       ;768 号页目录项指向 0 号页表

    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax        ; 使最后一个页目项指向页目录表的地址（ 1024*4B-4=4092 ）

    ; 下面创建页表项（ PTE）
    mov ecx, 256                    ;1M 低端内存 / 每页大小 4k = 256 个物理页，所以 0 和 769 页目录项对应的页表只需要创建 256 个页表项即可
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P           ; 属性为 7
.create_pte:
    mov [ebx+esi*4], edx            ; 此时的 ebx 是 0x101000 （ 0 号页表中 0 号页表项的地址）
                                    ;      edx 是第 0 号表项中的物理页的首地址（每一个物理页占 4K），对应的地址为 0~4095 （ 0xFFF）。
                                    ;       低端 1MB 的虚拟地址会对应低端 1MB 的物理地址
    add edx, 4096                   ;1 号页表项中的物理页的首地址（ PDE 和 PTE 的结构中，低 12 位是属性位，但是分页机制会自动将地址的后 12 位置为 0 ）
    inc esi                         ; 用于定位到下一个页表项的位置
    loop .create_pte

    ; 创建内核其它页表的 PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000                 ; 此时 eax 为第 2 个页表物理位置
    or eax, PG_US_U | PG_RW_W | PG_P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254                    ; 范围为第 769~1022 的所有目录项数量（ 768 号目录项指向 0 号页表；最后一个页目录项 1023 号指向页目录表）
                                    ; 为了实现内核完全共享，需要把 768~1022 号页目录项都映射到低端（用于应对内核申请大量空间的情况）
    mov esi, 769                    ; 第 769 个页目录项
.create_kernel_pde:              
    mov [ebx+esi*4], eax            ; 将 1 号页表项物理地址放入 769 号页目录项中
    inc esi                         ; 定位到下一个页目录项的地址
    add eax, 0x1000                 ; 定位到下一个页表的地址
    loop .create_kernel_pde

    ret

;-------------------------------------------------------------------------------
			   ; 功能 : 读取硬盘 n 个扇区
rd_disk_m_32:	   
;-------------------------------------------------------------------------------
							 ; eax=LBA 起始逻辑扇区号
							 ; ebx= 将数据写入的内存地址
							 ; ecx= 读入的扇区数

    mov esi, eax            ; 备份扇区号到 esi
    mov di, cx              ; 备份要读入的扇区数到 di

; 读写硬盘 :
; 第 1 步：设置要读取的扇区数
      mov dx,0x1f2
      mov al,cl
      out dx,al            ; 读取的扇区数

      mov eax,esi	   ; 恢复 ax

; 第 2 步：设置起始 LBA 扇区号

      ;LBA 地址 7~0 位写入端口 0x1f3
      mov dx,0x1f3                       
      out dx,al                          

      ;LBA 地址 15~8 位写入端口 0x1f4
      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      ;LBA 地址 23~16 位写入端口 0x1f5
      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f	   ;lba 第 24~27 位
      or al,0xe0	   ; 设置 7 ～ 4 位为 1110, 表示 lba 模式
      mov dx,0x1f6
      out dx,al

; 第 3 步：向 0x1f7 端口写入读命令， 0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;;;;;;; 至此 , 硬盘控制器便从指定的 lba 地址 (eax) 处 , 读出连续的 cx 个扇区 , 下面检查硬盘状态 , 不忙就能把这 cx 个扇区的数据读出来

; 第 4 步：检测硬盘状态
  .not_ready:		   ; 测试 0x1f7 端口 (status 寄存器 ) 的的 BSY 位
      ; 同一端口 , 写时表示写入命令字 , 读时表示读入硬盘状态
      nop
      in al,dx
      and al,0x88	   ; 第 4 位为 1 表示硬盘控制器已准备好数据传输 , 第 7 位为 1 表示硬盘忙
      cmp al,0x08
      jnz .not_ready	   ; 若未准备好 , 继续等。

; 第 5 步：从 0x1f0 端口读数据
      mov ax, di	   ; 以下从硬盘端口读数据用 insw 指令更快捷 , 不过尽可能多的演示命令使用 ,
			   ; 在此先用这种方法 , 在后面内容会用到 insw 和 outsw 等

      mov dx, 256	   ;di 为要读取的扇区数 , 一个扇区有 512 字节 , 每次读入一个字 , 共需 di*512/2 次 , 所以 di*256
      mul dx
      mov cx, ax	   
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx		
      mov [ebx], ax
      add ebx, 2
			  ; 由于在实模式下偏移地址为 16 位 , 所以用 bx 只会访问到 0~FFFFh 的偏移。
			  ; loader 的栈指针为 0x900,bx 为指向的数据输出缓冲区 , 且为 16 位，
			  ; 超过 0xffff 后 ,bx 部分会从 0 开始 , 所以当要读取的扇区数过大 , 待写入的地址超过 bx 的范围时，
			  ; 从硬盘上读出的数据会把 0x0000~0xffff 的覆盖，
			  ; 造成栈被破坏 , 所以 ret 返回时 , 返回地址被破坏了 , 已经不是之前正确的地址 ,
			  ; 故程序出会错 , 不知道会跑到哪里去。
			  ; 所以改为 ebx 代替 bx 指向缓冲区 , 这样生成的机器码前面会有 0x66 和 0x67 来反转。
			  ; 0X66 用于反转默认的操作数大小 ! 0X67 用于反转默认的寻址方式 .
			  ; cpu 处于 16 位模式时 , 会理所当然的认为操作数和寻址都是 16 位 , 处于 32 位模式时 ,
			  ; 也会认为要执行的指令是 32 位 .
			  ; 当我们在其中任意模式下用了另外模式的寻址方式或操作数大小 ( 姑且认为 16 位模式用 16 位字节操作数，
			  ; 32 位模式下用 32 字节的操作数 ) 时 , 编译器会在指令前帮我们加上 0x66 或 0x67 ，
			  ; 临时改变当前 cpu 模式到另外的模式下 .
			  ; 假设当前运行在 16 位模式 , 遇到 0X66 时 , 操作数大小变为 32 位 .
			  ; 假设当前运行在 32 位模式 , 遇到 0X66 时 , 操作数大小变为 16 位 .
			  ; 假设当前运行在 16 位模式 , 遇到 0X67 时 , 寻址方式变为 32 位寻址
			  ; 假设当前运行在 32 位模式 , 遇到 0X67 时 , 寻址方式变为 16 位寻址 .

      loop .go_on_read
      ret












    ; 输出背景色为草绿色，前景色为红色，并且跳动的字符串 "2 LOADER"
    ; mov byte [gs:0x00], '2'
    ; mov byte [gs:0x01], 0xA4        ;A 表示绿色背景闪烁， 4 表示前景色为红色

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

    
