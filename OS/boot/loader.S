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
                                            ;这将导致之前做的预测失效，从而起到了刷新的作用
                                            ;出错则挂起


[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA   ;用选择子初始化各段寄存器
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

;------------------------------- 加载Kernel -------------------------------
;rd_disk_m_32函数的参数：
;   ① eax：kernel内核所在的扇区号
;   ② ebx：内核从磁盘读出后，写入到内存的地址
;   ③ ecx：读入的扇区数
    mov eax, KERNEL_START_SECTOR            ;kernel所在的扇区号0x9
    mov ebx, KERNEL_BIN_BASE_ADDR           ;从磁盘读出后，写入到内存地址（0x70000）
    mov ecx, 200                            ;读入的扇区数

    call rd_disk_m_32       ;读取硬盘（将硬盘中的数据读入内存中），此函数所有的寄存器都变成32位的
    
    ;创建页目录及页表并初始化页内存位图
    call setup_page

;------------------------------- 重启加载GDT做准备 -------------------------------

    ;要将描述符表地址及偏移量写入内存gdt_ptr，一会用新地址重新加载
    sgdt [gdt_ptr]                  ;存储到原来gdt所有的位置        

    ;因为我们在页表中会将内核放置到3GB以上的地址，我们也把GDT放在内核的地址空间，
    ;在此通过sgdt指令，将GDT的起始地址和偏移量信息dump（像倒水一样）出来，依然存放到gdt_ptr处
    ;一会儿待条件成熟时，我们再从地址gdt_ptr处重新加载GDT。

    ;将gdt描述符中视频段描述符中的段基址+0xc0000000
    mov ebx, [gdt_ptr + 2]          ;gdt的基址
    or dword [ebx + 0x18 + 4], 0xc0000000       ;视频段是第3个描述符，每个描述符是8字节，故0x18
                                                ;段描述符的最高4字节的最高位是段基址31~24位

    ;将gdt的基址加上0xc0000000，使其成为内核所在的高地址
    add dword [gdt_ptr + 2], 0xc0000000

    add esp, 0xc0000000             ;将栈指针同样映射到内核地址

    ;把页目录表地址赋给cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    ;打开cr0的pg位（第31位）
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdt_ptr]


;;;;;;;;;;;;;;;;;;;;;;;;;;;;  此时不刷新流水线也没问题  ;;;;;;;;;;;;;;;;;;;;;;;;
;由于一直处在32位下（不需要 “显示的清空流水线” ）,原则上不需要强制刷新，经过实际测试没有以下这两句也没问题.
;但以防万一，还是加上啦，免得将来出来莫句奇妙的问题.
    jmp SELECTOR_CODE:enter_kernel
enter_kernel:
    call kernel_init
    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT                    ;用地址0x1500进行访问测试

;-----------------   将kernel.bin中的segment拷贝到编译的地址   -----------------
kernel_init:                ;函数kernel_init的作用：将kernel.bin中的段（segment）拷贝到各段自己被编译的虚拟地址处，将这些段单独提取到内存中，这就是平时所说的内存中的程序映像。
                            ;kernel_init的原理：分析程序中的每个段（segment），如果段类型不是PT_NULL（空程序类型），就将该段拷贝到编译的地址中。
    xor ebx, ebx            ;ebx记录程序头表地址
    xor ecx, ecx            ;cx记录程序头表中的program header程序头数量
    xor edx, edx            ;dx记录program header程序表尺寸，即e_phentsize（遍历段的方式是：指向第一个程序头后，每次增加一个段头的大小，该属性位于偏移程序开头42字节处，e_phentsize）

    mov dx, [KERNEL_BIN_BASE_ADDR + 42]     ;e_phentsize
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]    ;偏移文件开始部分28字节的地方是e_phoff,表示第1 个program header在文件中的偏移量
    add ebx, KERNEL_BIN_BASE_ADDR          ;此时ebx作为程序头表的基址，用它来遍历每一个段（此时ebx指向程序中第1个program header的物理地址）
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]     ;偏移文件开始部分28字节的地方是e_phnum，表示有几个program header

.each_segment:
    cmp byte [ebx + 0], PT_NULL             ;若p_type等于PT_NULL，说明此program header未使用
    je .PT_NULL

    ;为函数memcpy压入参数（参数是从右往左依然压入），函数原型类似于 memcpy(dst, src, size）
    push dword [ebx + 16]                   ;———— 压入函数memcpy的第三个参数：size段大小
                                                ;program header中偏移16字节的地方是p_filesz（本段在文件中的大小） 
    mov eax, [ebx + 4]                      ;距程序头偏移量为4字节的地方是p_offset（本段在文件内的起始偏移字节） 
    add eax, KERNEL_BIN_BASE_ADDR           ;加上kernel.bin被加载到的物理地址，使eax为该段的物理地址
    push eax                                ;———— 压入函数memcpy的第二个参数：src源地址
    push dword [ebx + 8]                    ;———— 压入函数memcpy的第一个参数：dst目的地址
                                                ;偏移程序头8字节的位置是p_vaddr，这就是目的地址
    call mem_cpy                        ;调用mem_cpy完成段复制
    add esp, 12                         ;清理栈中压入的三个参数

.PT_NULL:
    add ebx, edx                            ;edx为program header大小（e_phentsize），使ebx指向下一个program header
    loop .each_segment
    ret

;-------------------- 逐字节拷贝 mem_cpy(dst, src, size) --------------------
;输入: 栈中三个参数(dst, src, size)
;输出: 无
;---------------------------------------------------------------------------
mem_cpy:
    cld                                 ;将eflags寄存器中的方向标志位DF置为0
    push ebp                            ;我们访问栈中的参数是基于ebp的，需要现将ebp备份
    mov ebp, esp
    push ecx                            ;rep指令用到了ecx，但是ecx对于外层段的循环还得用，需要先入栈备份
    mov edi, [ebp + 8]          ;dst
    mov esi, [ebp + 12]         ;src
    mov ecx, [ebp + 16]         ;size
    rep movsb

    ;恢复环境
    pop ecx
    pop ebp
    ret




;-------------------------- 创建页目录及页表并初始化页内存位图 --------------------------
setup_page:
    ;先把页目录占用的空间逐字节清0（每个页目录项占用4B，1000个页目录项占用4K的空间）
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir
;开始创建页目录项（PDE）
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS     ;属性为7
    add eax, 0x1000                 ;eax为页目录项的结构: ① 31~12位为页表的物理地址的前20位，后12位都是0
                                    ;                   ② 0~11位为页目录项的属性
    mov ebx, eax                    ;此处为ebx赋值，是为.create_pte做准备，ebx是基址

    ;将 0号和768号页目录项都指向第1个页表（0x101000）
    or eax, PG_US_U | PG_RW_W | PG_P
    mov [PAGE_DIR_TABLE_POS + 0x00], eax        ;1号页目录项指向1号页表
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax       ;768号页目录项指向0号页表

    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax        ;使最后一个页目项指向页目录表的地址（1024*4B-4=4092）

    ;下面创建页表项（PTE）
    mov ecx, 256                    ;1M低端内存 / 每页大小4k = 256个物理页，所以0和769页目录项对应的页表只需要创建256个页表项即可
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P           ;属性为7
.create_pte:
    mov [ebx+esi*4], edx            ;此时的ebx是0x101000（0号页表中0号页表项的地址）
                                    ;      edx是第0号表项中的物理页的首地址（每一个物理页占4K），对应的地址为0~4095（0xFFF）。
                                    ;       低端1MB的虚拟地址会对应低端1MB的物理地址
    add edx, 4096                   ;1号页表项中的物理页的首地址（PDE和PTE的结构中，低12位是属性位，但是分页机制会自动将地址的后12位置为0）
    inc esi                         ;用于定位到下一个页表项的位置
    loop .create_pte

    ;创建内核其它页表的PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000                 ;此时eax为第2个页表物理位置
    or eax, PG_US_U | PG_RW_W | PG_P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254                    ;范围为第769~1022的所有目录项数量（768号目录项指向0号页表；最后一个页目录项1023号指向页目录表）
                                    ;为了实现内核完全共享，需要把 768~1022 号页目录项都映射到低端（用于应对内核申请大量空间的情况）
    mov esi, 769                    ;第769个页目录项
.create_kernel_pde:              
    mov [ebx+esi*4], eax            ;将1号页表项物理地址放入769号页目录项中
    inc esi                         ;定位到下一个页目录项的地址
    add eax, 0x1000                 ;定位到下一个页表的地址
    loop .create_kernel_pde

    ret

;-------------------------------------------------------------------------------
			   ;功能:读取硬盘n个扇区
rd_disk_m_32:	   
;-------------------------------------------------------------------------------
							 ; eax=LBA起始逻辑扇区号
							 ; ebx=将数据写入的内存地址
							 ; ecx=读入的扇区数

    mov esi, eax            ;备份扇区号到esi
    mov di, cx              ;备份要读入的扇区数到di

;读写硬盘:
;第1步：设置要读取的扇区数
      mov dx,0x1f2
      mov al,cl
      out dx,al            ;读取的扇区数

      mov eax,esi	   ;恢复ax

;第2步：设置起始LBA扇区号

      ;LBA地址7~0位写入端口0x1f3
      mov dx,0x1f3                       
      out dx,al                          

      ;LBA地址15~8位写入端口0x1f4
      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      ;LBA地址23~16位写入端口0x1f5
      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f	   ;lba第24~27位
      or al,0xe0	   ; 设置7～4位为1110,表示lba模式
      mov dx,0x1f6
      out dx,al

;第3步：向0x1f7端口写入读命令，0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;;;;;;; 至此,硬盘控制器便从指定的lba地址(eax)处,读出连续的cx个扇区,下面检查硬盘状态,不忙就能把这cx个扇区的数据读出来

;第4步：检测硬盘状态
  .not_ready:		   ;测试0x1f7端口(status寄存器)的的BSY位
      ;同一端口,写时表示写入命令字,读时表示读入硬盘状态
      nop
      in al,dx
      and al,0x88	   ;第4位为1表示硬盘控制器已准备好数据传输,第7位为1表示硬盘忙
      cmp al,0x08
      jnz .not_ready	   ;若未准备好,继续等。

;第5步：从0x1f0端口读数据
      mov ax, di	   ;以下从硬盘端口读数据用insw指令更快捷,不过尽可能多的演示命令使用,
			   ;在此先用这种方法,在后面内容会用到insw和outsw等

      mov dx, 256	   ;di为要读取的扇区数,一个扇区有512字节,每次读入一个字,共需di*512/2次,所以di*256
      mul dx
      mov cx, ax	   
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx		
      mov [ebx], ax
      add ebx, 2
			  ; 由于在实模式下偏移地址为16位,所以用bx只会访问到0~FFFFh的偏移。
			  ; loader的栈指针为0x900,bx为指向的数据输出缓冲区,且为16位，
			  ; 超过0xffff后,bx部分会从0开始,所以当要读取的扇区数过大,待写入的地址超过bx的范围时，
			  ; 从硬盘上读出的数据会把0x0000~0xffff的覆盖，
			  ; 造成栈被破坏,所以ret返回时,返回地址被破坏了,已经不是之前正确的地址,
			  ; 故程序出会错,不知道会跑到哪里去。
			  ; 所以改为ebx代替bx指向缓冲区,这样生成的机器码前面会有0x66和0x67来反转。
			  ; 0X66用于反转默认的操作数大小! 0X67用于反转默认的寻址方式.
			  ; cpu处于16位模式时,会理所当然的认为操作数和寻址都是16位,处于32位模式时,
			  ; 也会认为要执行的指令是32位.
			  ; 当我们在其中任意模式下用了另外模式的寻址方式或操作数大小(姑且认为16位模式用16位字节操作数，
			  ; 32位模式下用32字节的操作数)时,编译器会在指令前帮我们加上0x66或0x67，
			  ; 临时改变当前cpu模式到另外的模式下.
			  ; 假设当前运行在16位模式,遇到0X66时,操作数大小变为32位.
			  ; 假设当前运行在32位模式,遇到0X66时,操作数大小变为16位.
			  ; 假设当前运行在16位模式,遇到0X67时,寻址方式变为32位寻址
			  ; 假设当前运行在32位模式,遇到0X67时,寻址方式变为16位寻址.

      loop .go_on_read
      ret












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

    
