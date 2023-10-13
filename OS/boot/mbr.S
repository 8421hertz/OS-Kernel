;主引导程序
;------------------------------------------------------------------------------
%include "boot.inc"
SECTION MBR vstart=0x7c00
    mov ax, cs ;由于BIOS三通过jmp 0: 0x7c00跳转到MBR的，此时cs为0
    ;初始化其他寄存器
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00 ;0x7c00往下都属于MBR
    mov ax, 0xb800 ;显存文本模式的段地址(屏幕显示区域)
    mov gs, ax

;清屏利用0x06号功能，上卷全部行，则可以清屏
;----------------------------------------------------------------------------------
;INT 0x10   功能号：0x06    功能描述: 上卷窗口
;----------------------------------------------------------------------------------
;输入
;AH功能号= 0x06
;Al = 上卷的行数(如果为0，则表示全部)
;BH = 上卷的属性
;(CL, CH) = 窗口左上角的(X,Y)位置
;(DL, DH) = 窗口右下角的(X,Y)位置
;无返回值
    mov ax, 0x0600
    mov bx, 0x0700
    mov cx, 0           ;左上角: (0, 0)
    mov dx, 0x184f      ;右下角: (80, 25)
                        ;VAG文本模式中，一行只能容纳80个字符，共25行
                        ;下标从0开始，所以0x18=24, 0x4f=79
                        ;int 0x10
    int 0x10

    ;输出背景色为绿色，前景色为红色，并且跳动的字符串"1 MBR"
    mov byte [gs:0x00], 0x1     ;低字节存储字符编码
    mov byte [gs:0x01], 0xA4    ;A表示绿色背景闪烁，4表示前景色为红色

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

    ;----------------- 将loader加载器加载到内存 -----------------
    ;eax、bx、cx为call要传送的参数(寄存器传参)
    mov eax, LOADER_START_SECTOR        ;起始扇区LBA地址(扇区号) — 待读入的扇区起始地址
    mov bx, LOADER_BASE_ADDR            ;将loader写入内存的地址
    mov cx, 4                           ;待读入的扇区数(因为loader.bin超过了512字节，为了避免将来出错，直接改成读入4扇区)
    call rd_disk_m_16                   ;以下读取程序的起始部分(一个扇区)

    jmp LOADER_BASE_ADDR

;------------------------------------------------------------------------------------------------------------------------
;功能：读取硬盘n个扇区
rd_disk_m_16:
;------------------------------------------------------------------------------------------------------------------------
                                        ;eax=LBA扇区号
                                        ;bx=将loader写入内存的地址
                                        ;cx=读入的扇区数
    mov esi, eax                ;备份eax
    mov di, cx                  ;备份cx
    ;读写硬盘
;第1步：先选择通道，往该通道的sector count寄存器中写入待操作的扇区数
    ;我们的硬盘属于ata0，是Primary通道，所以其sector count寄存器是由0x1f2端口来访问的；而且我们的hd60M.img是ata0-master主盘
    mov dx, 0x1f2               ;选择硬盘通道
    mov al, cl                  ;读取的扇区数
    out dx, al                  ;往sector count寄存器中写扇区数

    mov eax, esi                ;恢复eax

;第2步：往该通道上的三个LBA寄存器(0x1f3 ~ 0x1f5)写入扇区起始地址LBA的低24位。
    ;将LBA地址7~0位写入端口0x173(寄存器LBA low)
    mov dx, 0x1f3
    out dx, al

    ;将LBA地址15~8位写入端口0x1f4(寄存器LBA mid)
    mov cx, 8                   ;逻辑右移的位数
    shr eax, cl                 ;逻辑右移，通过此指令置换出相应的LBA地址部分，写入相应的LBA寄存器
    mov dx, 0x1f4
    out dx, al

    ;将LBA地址16~23位写入端口0x1f5(寄存器LBA high)
    shr eax, cl
    mov dx, 0x1f5
    out dx, al

;第3步：往device寄存器(0x1f6端口)中写入LBA地址的24～27位，并置第6位为1，使其为LBA模式，设置第4位，选择操作的硬盘（master硬盘或slave硬盘）。
    ;第5位和第7位固定位1，第6位为1表示启用LBA
    shr eax, cl
    and al, 0x0f
    or al, 0xe0
    mov dx, 0x1f6
    out dx, al

;第4步：往该通道上的command端口0x1f7写入操作命令。
    ;这里是读命令0x20
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

;第5步：检测硬盘状态(status寄存器的BSY位) —— status寄存器仍然是0x1f7端口
.not_ready:
    nop                     ;相当于sleep了一下
    in al, dx               ;将status寄存器读入al中
    ;status寄存器中，若第3位为1，表示硬盘控制器已经准备好数据传输；第7位为1，表示硬盘忙
    and al, 0x88

    cmp al, 0x08            ;判断第3位是否为1; 如果第3位为0，此时的第7位肯定为1
    jnz .not_ready          ;若未准备好，继续等

;第6步：从0x170端口读数据
    ;di为读取的扇区数，一个扇区有512字节，每次读入一个字
    ;共需要读取di * 512 / 2次，所以di * 256
    mov ax, di      ;这里为1个扇区
    mov dx, 256
    mul dx          ;ax * dx
    mov cx, ax

    mov dx, 0x1f0

.go_on_read:
    in ax, dx
    mov [bx], ax            ;由于在实模式下偏移地址为16位，所以用bx只会访问到0～FFFFh的偏移。待写入的地址超过bx的范围时，从硬盘上读出的数据会把0x0000～0xffff的覆盖，所以此处加载的程序不能超过64KB，即2的16次方等于65536。
    add bx, 2
    loop .go_on_read
    ret


    times 510 - ($ - $$) db 0
    db 0x55, 0xaa

;;;;;;;;;; 下面这三行代码用于获取光标位置 ;;;;;;;;;;
;.get_cursor获取当前光标位置，在光标处打印字符
;字符打印的位置，不一定要在光标处，字符的位置只和显存中的地址有关，和光标三无关的，这只是认为的加了个约束
    mov ah, 0x03        ;输入: 3号子功能是获取光标位置，需要存入ah寄存器
    mov bh, 0           ;bh寄存器存储的是待获取光标的页号

    int 0x10            ;输出: ch=光标开始行，cl=光标结束行
                        ;dh=光标所在行号，dl=光标所在列号

;;;;;;;;;; 获取光标位置结束 ;;;;;;;;;;

;;;;;;;;;; 打印字符串 ;;;;;;;;;;
    ;还是用10h中断，不过这次调用13号子功能打印字符串
    mov ax, message
    mov bp, ax          ;es:bp为字符串首地址，es此时同cs一致
                        ;开头时已经为sreg初始化
    
    ;光标位置要用到dx寄存器中的内容，cx中的光标位置可忽略
    mov cx, 5           ;cx为字符串长度，不包过结束符0点字符个数
    mov ax, 0x1301      ;子功能号13是显示字符及属性，要存入ah寄存器
                        ;al设置写字符方式 al = 01: 显示字符串，光标跟随移动
    mov bx, 0x0002      ;bh设置要显示的页号，此处是0页
                        ;bl中是字符属性，属性黑底绿字(bl -= 02h)
    
    int 0x10

;;;;;;;;;; 打印字符结束 ;;;;;;;;;;    

    jmp $               ;死循环，使程序悬停在这里

    message db "1 MBR"  ;需要打印的字符串
    ;times 510 - ($ - $$) db 0   ;数据0x55、0xaa之前的扇区空间需要填满0
    ;db 0x55, 0xaa


























