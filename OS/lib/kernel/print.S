TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

section .data
; 作为转换过程中用的缓冲区，实际上它的用途就是用于存储转换后的字符
put_int_buffer  dq  0   ; 因为只支持 32 位数字的输出，按每 4 位二进制表示 1 位十六进制，共 8 个十六进制数字要处理。转化为对应的字符后每个 ASCII 码需要 1 字节，所以每个字符需要 1 字节空间

[bits 32]
section .text

;----------------------- put_str ----------------------
; 功能描述：通过 put_char 来打印以 0 字符结尾的字符串
    ; C 编译器会把字符串的结尾自动加上 '\0'，用它作为字符串的结束标记， ASCII 码位 0
;-------------------------------------------------------
; 输入：栈中参数位打印的字符串
; 输出：无
global put_str
put_str:
    push ebx
    push ecx
    ; 此时的栈中：参数 (4B)、函数返回地址 (4B)、 ebx(4B)、 ecx(4B)
    xor ecx, ecx                    ; 用 ecx 存储参数，先清空
    mov ebx, [esp + 12]             ; 从栈中得到待打印的字符串地址
.goon:  
    mov cl, [ebx]                   ; 取出 1 字节字符数据
    cmp cl, 0                       ; 判断如果处理到了字符串尾 (\0)，则跳转到结束处返回
    jz .str_over
    push ecx                        ; 为 put_char 传递参数
    call put_char
    add esp, 4                      ; 回收参数所占的栈空间
    inc ebx                         ; 使 ebx 指向下一个字符
    jmp .goon
.str_over:
    pop ecx
    pop ebx
    ret

;----------------------- put_char ----------------------
; 功能描述：把栈中的 1 个字符写入到光标所在处
;-------------------------------------------------------
global put_char
put_char:
    pushad                          ; 备份 32 位寄存器环境
    ; 需要保证 gs 中为正确的视频段选择子
    ; 为保险起见，每次打印时都为 gs 赋值 ( 原因是特权级相关 )
    mov ax, SELECTOR_VIDEO          ; 不能直接把立即数送入段寄存器
    mov gs, ax

    ;;;;;;;; 获取当前光标位置 ;;;;;;;;
    ; 先获得高 8 位
    mov dx, 0x03d4                  ; 先往端口地址为 0x3D4 的 Address Register 寄存器 中写入寄存器的索引
    mov al, 0x0e                    ; 索引为 0Eh 的 Cursor Location High Register 寄存器（用于提供光标位置的高 8 位）
    out dx, al
    mov dx, 0x03d5                  ; 再从端口地址为 0x3D5 的 Data Register 寄存器 读、写数据（获得或设置光标的位置）
    in al, dx                       ; 得到了光标位置的高 8 位
    mov ah, al

    ; 再获得低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    in al, dx

    ; 用寄存器 bx 做基址寻址（ bx 现在已经是光标坐标值就行了，它是下一个可打印字符的位置）
    mov bx, ax

    ; 在栈中获取待打印的字符
    mov ecx, [esp + 36]             ; pushad 压入 4x 8= 32 字节，加上主调函数 4 字节的返回地址，故 esp+36 字节
                                        ; CR 回车符 是 0xOd, LF 换行符 是 0x0a
                                        ; 我们这里的处理是不管参数是回车符，还是换行符，一律按我们平时所理解的回车换行符（ CRLF）处理

    ; 处理字符
    cmp cl,0x0d                    ; 看是否是回车符 CR(\r)，来决定是否按照其实际控制意义来处理
    jz .is_carriage_return
    cmp cl,0xa                     ; 看是否是换行符 LF(\n)
    jz .is_line_feed
    cmp cl,0x8                     ; 看是否是 backspace( 退格 )
    jz .is_backspace
    jmp .put_other                 ; 跳转处理可见字符

.is_backspace:
;;;;;;;;;;;;;;;;;; backspace 删除并退格 ;;;;;;;;;;;;;;;;;
; 当为 BackSpace 时，本质上只要将光标向前一个显存位置即可。后面再输入的字符自然会覆盖此处的字符
; 但有可能在键入 BackSpace 后并不再键入新的字符，这时光标已经向前移动到待删除的字符位置，但字符还在原处
; 这就显得很怪异，所以此处添加了空格或字符 0
    dec bx                          ; 光标 -1 ，使之指向上一个字符
    shl bx, 1                       ; 光标位置用 2 字节表示，将光标值乘 2 ，表示光标对应显存中的偏移字节（ shl 比 mul 更高效）

    mov byte [gs:bx], 0x20          ; 将待删除的字节补为 0 或空格皆可
    inc bx
    mov byte [gs:bx], 0x07          ; 黑底白字
    shr bx, 1                       ; 将 bx 变回光标值
    jmp set_cursor                 ; 跳转设置（更新）光标 bx 值

.put_other:
;;;;;;;;;;;;;;;;;; 将字符输出到屏幕缓冲区 0xc00b8000 中 ;;;;;;;;;;;;;;;;;
    shl bx, 1
    mov byte [gs:bx], cl            ;ASCII 字符本身
    inc bx
    mov byte [gs:bx], 0x07
    shr bx, 1                       
    inc bx                          ; 下一个光标值
    ; 判断光标位置是否已经超过（或等于）屏幕缓冲区范围 (2000)
        ; 如果超过（或等于）了，则执行换行和滚屏操作
    cmp bx, 2000
    jl set_cursor

; 将光标置于下一行行首（由于是效仿 Linux， Linux 中 \n 表示下一行的行首，所以本系统中把 \n 和 \r 都处理为 Linux 中 \n 的意思）
.is_line_feed:                      ; 是换行符 LF(\n)
.is_carriage_return:                ; 是回车符 CR(\r)
; 处理 CR(\r) 回车符 ———— 如果是 CR（\r），只要把光标移到行首就行了
    xor dx, dx                      ; dx 是被除数的高 16 位，清 0
    mov ax, bx                      ; ax 是被除数的低 16 位，传入
    mov si, 80                      
    div si                          ; dx 中的余数是光标所在这行的坐标
    sub bx, dx                      ; 光标值 - 光标所在这行的坐标 = 行首坐标

; 处理 LF(\n) 换行符
.is_carriage_return_end:
    add bx, 80
    cmp bx, 2000

.is_line_feed_end:
    jl set_cursor                  ; 如果光标值 < 2000 ，则更新坐标

.roll_screen:                       ; 若超出屏幕大小，开始滚屏
;;;;;;;;;;;;;;;;;; 处理滚屏 ;;;;;;;;;;;;;;;;;
; 幕行范围是 0 ～ 24 ，滚屏的原理是将屏幕的第 1 ～ 24 行搬运到第 0 ～ 23 行，再将第 24 行用空格填充
    cld                             ; 将 eflags 寄存器中的方向标志位 DF 清 0, 使执行 movsd 时， esi 与 edi 可以增大
    mov ecx, 960                    ; 一共 (2000-80)*2 个字节需要搬运，总共需要 1920*2/4=960 次复制
    mov esi, 0xb80a0                ; 设置源地址的偏移地址（第 1 行行首）
    mov edi, 0xb8000                ; 设置目标地址的偏移地址（第 0 行行首）
    rep movsd                       ; 一次赋值 4 字节数据
; 将最后一行 (24 行 ) 填充为空白
    mov ebx, 3840                   ; 最后一行首字符的第一个字节偏移 =1920*2
    mov ecx, 80                     ; 一行是 80 字符（ 160 字节），每次清空 1 字符（ 2 字节），一行需要移动 80 次

.cls:
    mov word [gs:ebx], 0x0720       ;0x0720 是黑底白字的空格键
    add ebx, 2
    loop .cls
    mov bx, 1920                    ; 把坐标值重新设置为最后一行行首

global set_cursor
set_cursor:
;;;;;;;;;;;;;;;;;; 设置 ( 更新 ) 坐标值 ;;;;;;;;;;;;;;;;;
; 将光标设为 bx 值
    ; 先设置高 8 位
    mov dx, 0x03d4                  ; 索引寄存器
    mov al, 0x0e                    ; 用于提供光标位置的高 8 位
    out dx, al
    mov dx, 0x03d5                  ; 通过读写数据端口 0x3d5 来获得或设置光标位置
    mov al, bh
    out dx, al

    ; 再设置低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al

.put_char_done:
    popad
    ret                             ; 将之前入栈的 8 个 32 位寄存器恢复到各个的寄存器中

;----------------------- put_int ----------------------
; 功能描述：将小端字节序的数字变成对应的 ASCII 后，倒置
    ; 输入：栈中参数位待打印的数字（ 32 位十六进制数）
    ; 输出：在屏幕上打印十六进制数字，并不会打印前缀 0x
          ; 如打印十进制 15 时，只会打印 f，不会是 0xf
;-------------------------------------------------------
global put_int
put_int:
    pushad
    mov ebp, esp
    ; 把数字的前 8 字节放入 eax 中（ eax 用于备份，四位四位的传送给 edx）
    mov eax, [ebp + 4*9]             ; (8 个寄存器 + 返回地址 ) * 4 = 36 字节
    mov edx, eax                    ; edx 用于数位转换
    mov edi, 7                      ; 指定在 put_int_buffer 中初始的偏移量 ( 缓冲区中最后一个字节 )
    mov ecx, 8                      ; 32 位数字中，十六进制数字的位数是 8 个
    mov ebx, put_int_buffer         ; 缓冲区基址

    ; 将 32 位数字按照十六进制的形式从低位到高位逐个处理
    ; 共处理 8 个十六进制数字
.16based_4bits:                     ; 每 4 位二进制是十六进制数字的 1 位
    ; 遍历每一位十六进制数字
    and edx, 0x0000000F             ; 解析十六进制数字的每一位（低 4 位有效）
    ;and 操作后， edx 只有低 4 位有效
    cmp edx, 9                      
    jg .is_A2F                      ; 数字 0~9 (>9)
    add edx, '0'                    ; 0~9: 数字 + 字符 '0' = 字符数字 (ASCII 码是 8 位大小。 add 求和操作后， edx 低 8 位有效 )
    jmp .store
.is_A2F:                            ; A~F 数字 - 10 + 'A' = 字符数字
    sub edx, 10
    ; ASCII 码，便是 A～ F 对应的 ASCII 码
    add edx, 'A'

; 将每一位数字转换成对应的字符后，按照以 " 大端 " 的顺序存储到缓冲区 put_int_buffer
; 高位字符存放在低地址，低位字符存放在高地址
; 这样和大端字节序类似，只不过这里是字符序
.store:
    ; 此时 dl 中应该是数字对应的字符的 ASCII 码
    mov [ebx + edi], dl
    dec edi
    shr eax, 4                      ; 去掉已经换完的 4 位
    mov edx, eax
    loop .16based_4bits

; 现在 put_int_buffer 中已全是十六进制数字字符，打印之前
; 把高位连续的字符去掉，比如字符 000123 变成 123
.ready_to_print:
    inc edi                         ; 此时由于 edi 退减为 -1(exffffffff)，所以加 1 使其为 θ
.skip_prefix_0:
    cmp edi, 8
    ; 若已经比较到第 9 个字符了 (edi==8)，则表示字符串全为 0
    je .full0

; 找出连续的 0 字符， edi 作为非 0 的最高位字符的偏移
.go_on_skip:
    mov cl, [put_int_buffer + edi]
    inc edi
    cmp cl, '0'
    je .skip_prefix_0               ; 继续判断下一位字符是否为字符 ( 不是数字 θ )
    ; 若当前字符不为 '0', 要使 edi 减 1 恢复指向当前字符
    dec edi                         ; 原因是 edi 在上面的 inc 操作中指向了下一个字符
    jmp .put_each_num

.full0:
; 输入的数字为全时，则只打印 0
    mov cl, '0'
.put_each_num:
    push ecx                        ; 此时 cl 中为可打印的字符
    call put_char
    add esp, 4                      ; 恢复栈环境
    inc edi
    mov cl, [put_int_buffer + edi]
    cmp edi, 8
    jl .put_each_num                ; edi < 8
    popad
    ret
