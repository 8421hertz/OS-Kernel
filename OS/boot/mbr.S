; 主引导程序
;------------------------------------------------------------------------------
%include "boot.inc"
SECTION MBR vstart=0x7c00
    mov ax, cs ; 由于 BIOS 三通过 jmp 0: 0x7c00 跳转到 MBR 的，此时 cs 为 0
    ; 初始化其他寄存器
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00 ;0x7c00 往下都属于 MBR
    mov ax, 0xb800 ; 显存文本模式的段地址 ( 屏幕显示区域 )
    mov gs, ax

; 清屏利用 0x06 号功能，上卷全部行，则可以清屏
;----------------------------------------------------------------------------------
;INT 0x10   功能号： 0x06    功能描述 : 上卷窗口
;----------------------------------------------------------------------------------
; 输入
;AH 功能号 = 0x06
;Al = 上卷的行数 ( 如果为 0 ，则表示全部 )
;BH = 上卷的属性
;(CL, CH) = 窗口左上角的 (X,Y) 位置
;(DL, DH) = 窗口右下角的 (X,Y) 位置
; 无返回值
    mov ax, 0x0600
    mov bx, 0x0700
    mov cx, 0           ; 左上角 : (0, 0)
    mov dx, 0x184f      ; 右下角 : (80, 25)
                        ;VAG 文本模式中，一行只能容纳 80 个字符，共 25 行
                        ; 下标从 0 开始，所以 0x18=24, 0x4f=79
                        ;int 0x10
    int 0x10

    ; 输出背景色为绿色，前景色为红色，并且跳动的字符串 "1 MBR"
    mov byte [gs:0x00], 0x1     ; 低字节存储字符编码
    mov byte [gs:0x01], 0xA4    ;A 表示绿色背景闪烁， 4 表示前景色为红色

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

    ;----------------- 将 loader 加载器加载到内存 -----------------
    ;eax、 bx、 cx 为 call 要传送的参数 ( 寄存器传参 )
    mov eax, LOADER_START_SECTOR        ; 起始扇区 LBA 地址 ( 扇区号 ) — 待读入的扇区起始地址
    mov bx, LOADER_BASE_ADDR            ; 将 loader 写入内存的地址
    mov cx, 4                           ; 待读入的扇区数 ( 因为 loader.bin 超过了 512 字节，为了避免将来出错，直接改成读入 4 扇区 )
    call rd_disk_m_16                   ; 以下读取程序的起始部分 ( 一个扇区 )

    jmp LOADER_BASE_ADDR + 0x300

;------------------------------------------------------------------------------------------------------------------------
; 功能：读取硬盘 n 个扇区
rd_disk_m_16:
;------------------------------------------------------------------------------------------------------------------------
                                        ;eax=LBA 扇区号
                                        ;bx= 将 loader 写入内存的地址
                                        ;cx= 读入的扇区数
    mov esi, eax                ; 备份 eax
    mov di, cx                  ; 备份 cx
    ; 读写硬盘
; 第 1 步：先选择通道，往该通道的 sector count 寄存器中写入待操作的扇区数
    ; 我们的硬盘属于 ata0 ，是 Primary 通道，所以其 sector count 寄存器是由 0x1f2 端口来访问的；而且我们的 hd60M.img 是 ata0-master 主盘
    mov dx, 0x1f2               ; 选择硬盘通道
    mov al, cl                  ; 读取的扇区数
    out dx, al                  ; 往 sector count 寄存器中写扇区数

    mov eax, esi                ; 恢复 eax

; 第 2 步：往该通道上的三个 LBA 寄存器 (0x1f3 ~ 0x1f5) 写入扇区起始地址 LBA 的低 24 位。
    ; 将 LBA 地址 7~0 位写入端口 0x173( 寄存器 LBA low)
    mov dx, 0x1f3
    out dx, al

    ; 将 LBA 地址 15~8 位写入端口 0x1f4( 寄存器 LBA mid)
    mov cx, 8                   ; 逻辑右移的位数
    shr eax, cl                 ; 逻辑右移，通过此指令置换出相应的 LBA 地址部分，写入相应的 LBA 寄存器
    mov dx, 0x1f4
    out dx, al

    ; 将 LBA 地址 16~23 位写入端口 0x1f5( 寄存器 LBA high)
    shr eax, cl
    mov dx, 0x1f5
    out dx, al

; 第 3 步：往 device 寄存器 (0x1f6 端口 ) 中写入 LBA 地址的 24 ～ 27 位，并置第 6 位为 1 ，使其为 LBA 模式，设置第 4 位，选择操作的硬盘（ master 硬盘或 slave 硬盘）。
    ; 第 5 位和第 7 位固定位 1 ，第 6 位为 1 表示启用 LBA
    shr eax, cl
    and al, 0x0f
    or al, 0xe0
    mov dx, 0x1f6
    out dx, al

; 第 4 步：往该通道上的 command 端口 0x1f7 写入操作命令。
    ; 这里是读命令 0x20
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

; 第 5 步：检测硬盘状态 (status 寄存器的 BSY 位 ) —— status 寄存器仍然是 0x1f7 端口
.not_ready:
    nop                     ; 相当于 sleep 了一下
    in al, dx               ; 将 status 寄存器读入 al 中
    ;status 寄存器中，若第 3 位为 1 ，表示硬盘控制器已经准备好数据传输；第 7 位为 1 ，表示硬盘忙
    and al, 0x88

    cmp al, 0x08            ; 判断第 3 位是否为 1; 如果第 3 位为 0 ，此时的第 7 位肯定为 1
    jnz .not_ready          ; 若未准备好，继续等

; 第 6 步：从 0x170 端口读数据
    ;di 为读取的扇区数，一个扇区有 512 字节，每次读入一个字
    ; 共需要读取 di * 512 / 2 次，所以 di * 256
    mov ax, di      ; 这里为 1 个扇区
    mov dx, 256
    mul dx          ;ax * dx
    mov cx, ax

    mov dx, 0x1f0

.go_on_read:
    in ax, dx
    mov [bx], ax            ; 由于在实模式下偏移地址为 16 位，所以用 bx 只会访问到 0 ～ FFFFh 的偏移。待写入的地址超过 bx 的范围时，从硬盘上读出的数据会把 0x0000 ～ 0xffff 的覆盖，所以此处加载的程序不能超过 64KB，即 2 的 16 次方等于 65536 。
    add bx, 2
    loop .go_on_read
    ret


    times 510 - ($ - $$) db 0
    db 0x55, 0xaa

;;;;;;;;;; 下面这三行代码用于获取光标位置 ;;;;;;;;;;
;.get_cursor 获取当前光标位置，在光标处打印字符
; 字符打印的位置，不一定要在光标处，字符的位置只和显存中的地址有关，和光标三无关的，这只是认为的加了个约束
    mov ah, 0x03        ; 输入 : 3 号子功能是获取光标位置，需要存入 ah 寄存器
    mov bh, 0           ;bh 寄存器存储的是待获取光标的页号

    int 0x10            ; 输出 : ch= 光标开始行， cl= 光标结束行
                        ;dh= 光标所在行号， dl= 光标所在列号

;;;;;;;;;; 获取光标位置结束 ;;;;;;;;;;

;;;;;;;;;; 打印字符串 ;;;;;;;;;;
    ; 还是用 10h 中断，不过这次调用 13 号子功能打印字符串
    mov ax, message
    mov bp, ax          ;es:bp 为字符串首地址， es 此时同 cs 一致
                        ; 开头时已经为 sreg 初始化
    
    ; 光标位置要用到 dx 寄存器中的内容， cx 中的光标位置可忽略
    mov cx, 5           ;cx 为字符串长度，不包过结束符 0 点字符个数
    mov ax, 0x1301      ; 子功能号 13 是显示字符及属性，要存入 ah 寄存器
                        ;al 设置写字符方式 al = 01: 显示字符串，光标跟随移动
    mov bx, 0x0002      ;bh 设置要显示的页号，此处是 0 页
                        ;bl 中是字符属性，属性黑底绿字 (bl -= 02h)
    
    int 0x10

;;;;;;;;;; 打印字符结束 ;;;;;;;;;;    

    jmp $               ; 死循环，使程序悬停在这里

    message db "1 MBR"  ; 需要打印的字符串
    ;times 510 - ($ - $$) db 0   ; 数据 0x55 、 0xaa 之前的扇区空间需要填满 0
    ;db 0x55, 0xaa


























