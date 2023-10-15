;------------------- loader 和 kernel -------------------
LOADER_BASE_ADDR equ 0x900  ;loader加载器的内存存储地址
LOADER_START_SECTOR equ 0x2 ;把loader放在第2块起始扇区扇区(LBA)中

;------------------- gdt描述符属性 -------------------
;主要都是新增段描述符的属性及选择子，都是以宏的方式实现的
;符号一律采用"DESC"字段名_字段相关信息"的形式
DESC_G_4K   equ     1_00000000000000000000000b      ;G位为1表示为4K粒度(用来指定段界限的单位大小)
DESC_D_32   equ      1_0000000000000000000000b      ;D位为1表示代码段并且指令中的有效地址及操作数是32位，指令有效地址用EIP寄存器
DESC_L      equ       0_000000000000000000000b      ;L位为1表示32位代码段
DESC_AVL    equ        0_00000000000000000000b      ;对硬件和CPU来说，它没有专门的用途，是留给操作系统用的，暂时置为0
;段界限:代码段、数据段、显存段
DESC_LIMIT_CODE2  equ   1111_0000000000000000b      ;是代码段的段界限的第2部分，它与第2部分将组成20个二进制1，即总共的段界限将是0xFFFFF
DESC_LIMIT_DATA2  equ   DESC_LIMIT_CODE2
DESC_LIMIT_VIDEO2 equ	0000_0000000000000000b
DESC_P      equ     1_000000000000000b              ;P为1表示段存在于内存中
;设置特权级：0,1,2,3
DESC_DPL_0  equ		00_0000000000000b
DESC_DPL_1  equ		01_0000000000000b
DESC_DPL_2  equ		10_0000000000000b
DESC_DPL_3  equ		11_0000000000000b 
;s为0时表示系统段；s为1时表示数据段和代码段
DESC_S_CODE	equ		1_000000000000b
DESC_S_DATA	equ		DESC_S_CODE
DESC_S_SYS	equ		0_000000000000b
;type用于指定本描述符的类型
;（x=1,c=0,w=10,a=0：代码段为可执行、非一致性、不可读、已访问位a清0）
DESC_TYPE_CODE	equ		1000_00000000b
; (x=0,e=0,w=1,a=0)：数据段不可执行、向上扩展、可读写、已访问位a清0
DESC_TYPE_DATA	equ		0010_00000000b

;DESC_CODE_HIGH4定义了代码段描述符的高4字节
;其中(0x00)<<24表示"段基址24~31"字段，由于平坦模式的段基址是0，所以采用0左移24位来表示段基址
DESC_CODE_HIGH4	  equ	(0x00<<24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
						DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00

;DESC_DATA_HIGH4定义了数据段段描述符的高4字节
DESC_DATA_HIGH4	  equ	(0x00<<24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
						DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00

;DESC_VIDEO_HIGH4定义了显存段段描述符的高4字节						
DESC_VIDEO_HIGH4  equ	(0x00<<24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + \
						DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0b

;------------ 选择子属性 ------------
;RPL为请求特权级
RPL0 equ 00b
RPL1 equ 01b
RPL2 equ 10b
RPL3 equ 11b
;TI用于指示选择子是在GDT中还是在LDT中
;TI为0表示在GDT中索引描述符，TI为1表示在LDT中索引描述符
TI_GDT equ 000b
TI_LDT equ 100b

