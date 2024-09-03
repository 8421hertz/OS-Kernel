/**************	 机器模式   ***************
     b -- 输出寄存器 QImode 名称 , 即寄存器中的最低 8 位 :[a-d]l。
     w -- 输出寄存器 HImode 名称 , 即寄存器中 2 个字节的部分 , 如 [a-d]x。

     HImode
         “ Half-Integer”模式，表示一个两字节的整数。
     QImode
         “ Quarter-Integer”模式，表示一个一字节的整数。
*******************************************/

#ifndef __LIB_IO_H
#define __LIB_IO_H
#include "stdint.h"

/**
 * @brief 向指定的 I/O 端口写入一个字节的数据。
 *
 * 该函数使用内联汇编指令 `outb`，将一个字节的数据写入到指定的 I/O 端口。通常用于与硬件设备进行低级别的通信。
 *
 * @param port 要写入数据的 I/O 端口号。范围是 0 到 65535 （ 16 位无符号整数）。
 * @param data 要写入端口的数据。范围是 0 到 255 （ 8 位无符号整数）。
 *
 * asm 代码解析：
 * - `outb %b0, %w1`: 使用 `outb` 指令，将数据从 `al` 寄存器（对应于 `data`）输出到指定的端口（由 `dx` 寄存器指定，对应于 `port`）。
 * - `"a"(data)`: 将 `data` 的值放入 `al` 寄存器中。
 * - `"Nd"(port)`: 将 `port` 的值放入 `dx` 寄存器中，如果 `port` 是常数，它可以直接嵌入指令中。
 */
static inline void outb(uint16_t port, uint8_t data)
{
    asm volatile("outb %b0, %w1"
                 :
                 : "a"(data), "Nd"(port));
}

/**
 * @brief 将从指定地址开始的多个字写入到指定的 I/O 端口。
 *
 * 该函数使用 `outsw` 汇编指令，将内存中从指定地址开始的 `word_cnt` 个 16 位字写入到指定的 I/O 端口。这个操作通常用于与硬件设备进行批量数据传输。
 *
 * @param port 要写入数据的 I/O 端口号。范围是 0 到 65535 （ 16 位无符号整数）。
 * @param addr 指向要写入数据的内存地址。通常是一个 16 位的数据缓冲区。
 * @param word_cnt 要写入的字的数量。每个字是 16 位（ 2 字节）。
 *
 * asm 代码解析：
 * - `"cld; rep outsw"`:
 *   - `cld`: 清除方向标志位，确保字符串操作是从低地址到高地址进行。
 *   - `rep outsw`: 重复执行 `outsw` 指令 `word_cnt` 次。`outsw` 指令将 `ds:esi` 指向的 16 位内容写入到 `port` 端口，并递增 `esi` 寄存器。
 * - `"s"(addr)`: 将 `addr` 的值放入 `esi` 寄存器，作为源地址。
 * - `"c"(word_cnt)`: 将 `word_cnt` 的值放入 `ecx` 寄存器，用于计数。
 * - `"d"(port)`: 将 `port` 的值放入 `edx` 寄存器，指定目标端口。
 */
static inline void outsw(uint16_t port, const void *addr, uint32_t word_cnt)
{
    asm volatile("cld; rep outsw"
                 : "+S"(addr), "+c"(word_cnt)
                 : "d"(port));
}

/**
 * @brief 从指定的 I/O 端口读取一个字节的数据。
 *
 * 该函数使用 `inb` 汇编指令，从指定的 I/O 端口读取一个字节的数据并返回。这通常用于从硬件设备获取输入数据。
 *
 * @param port 要读取数据的 I/O 端口号。范围是 0 到 65535 （ 16 位无符号整数）。
 * @return 读取到的一个字节数据。范围是 0 到 255 （ 8 位无符号整数）。
 *
 * asm 代码解析：
 * - `"inb %w1, %b0"`: 使用 `inb` 指令，从 `port` 指定的 I/O 端口读取一个字节的数据，并将其存储到 `al` 寄存器中（对应于 `data`）。
 * - `"=a"(data)`: 将 `al` 寄存器中的数据（读取的字节）存储到 `data` 变量中。
 * - `"Nd"(port)`: 如果 `port` 是常数且范围在 0 到 255 之间，则作为立即数直接嵌入指令中，否则将 `port` 放入 `dx` 寄存器。
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t data;
    asm volatile("inb %w1, %b0"
                 : "=a"(data)
                 : "Nd"(port));
    return data;
}

/**
 * @brief 从指定的 I/O 端口读取多个字（ 16 位）并存储到指定的内存地址。
 *
 * 该函数使用 `insw` 汇编指令，从指定的 I/O 端口 `port` 读取 `word_cnt` 个 16 位的数据字，并将它们存储到 `addr` 指向的内存区域。这个操作通常用于从硬件设备读取批量数据。
 *
 * @param port 要读取数据的 I/O 端口号。范围是 0 到 65535 （ 16 位无符号整数）。
 * @param addr 指向存储数据的内存地址。通常是一个足够大的缓冲区，以容纳 `word_cnt` 个 16 位字的数据。
 * @param word_cnt 要读取并存储的字的数量。每个字是 16 位（ 2 字节）。
 *
 * asm 代码解析：
 * - `"cld; rep insw"`:
 *   - `cld`: 清除方向标志位，确保字符串操作是从低地址到高地址进行。
 *   - `rep insw`: 重复执行 `insw` 指令 `word_cnt` 次。`insw` 指令将 `dx` 指定的端口的数据读入到 `es:edi` 指向的内存，并递增 `edi` 寄存器。
 * - `"D"(addr)`: 将 `addr` 的值放入 `edi` 寄存器，作为目标内存地址。
 * - `"c"(word_cnt)`: 将 `word_cnt` 的值放入 `ecx` 寄存器，用于计数。
 * - `"d"(port)`: 将 `port` 的值放入 `dx` 寄存器，指定源端口。
 * - `"memory"`: 通知编译器 `addr` 所指向的内存可能会被修改，避免编译器进行相关优化。
 */
static inline void insw(uint16_t port, void *addr, uint32_t word_cnt)
{
    asm volatile("cld; rep insw"
                 : "+D"(addr), "+c"(word_cnt)
                 : "d"(port)
                 : "memory");
}

#endif
