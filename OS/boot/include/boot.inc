;----------- loader 和 kernel -----------
LOADER_BASE_ADDR equ 0x900  ;loader加载器的内存存储地址
LOADER_START_SECTOR equ 0x2 ;把loader放在第2块起始扇区扇区(LBA)中