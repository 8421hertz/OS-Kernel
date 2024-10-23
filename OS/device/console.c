#include "console.h"
#include "print.h"
#include "sync.h"
#include "thread.h"

static struct lock console_lock;  // 终端锁（对终端所有操作都是围绕申请这个锁展开的）

/**
 * @brief 初始化终端（初始化终端锁）
 * 
 * 该函数用于初始化终端的控制台锁，使终端的输出操作能够实现互斥。
 * 
 * @param void 该函数不接受任何参数。
 * @return void 该函数不返回任何值。
 */
void console_init() {
    lock_init(&console_lock);
}

/**
 * @brief 获取终端（申请终端控制台锁）
 * 
 * 该函数用于获取终端的控制台锁，确保终端输出时不会产生竞争问题。
 * 
 * @param void 该函数不接受任何参数。
 * @return void 该函数不返回任何值。
 */
void console_acquire() {
    lock_acquire(&console_lock);
}

/**
 * @brief 释放终端（释放终端控制台锁）
 * 
 * 该函数用于释放终端的控制台锁，允许其他线程执行终端输出操作。
 * 
 * @param void 该函数不接受任何参数。
 * @return void 该函数不返回任何值。
 */
void console_release() {
    lock_release(&console_lock);
}

/**
 * @brief 终端中输出字符串（带锁操作）
 * 
 * 该函数用于在终端中输出指定的字符串。输出时，通过获取并释放控制台锁来实现互斥。
 * 
 * @param char* str 要输出的字符串。
 * @return void 该函数不返回任何值。
 */
void console_put_str(char* str) {
    console_acquire();    // 获取终端锁
    put_str(str);         // 输出字符串
    console_release();    // 释放终端锁
}

/**
 * @brief 终端中输出字符（带锁操作）
 * 
 * 该函数用于在终端中输出指定的字符。输出时，通过获取并释放控制台锁来实现互斥。
 * 
 * @param uint8_t char_asci 要输出的字符。
 * @return void 该函数不返回任何值。
 */
void console_put_char(uint8_t char_asci) {
    console_acquire();    // 获取终端锁
    put_char(char_asci);  // 输出字符
    console_release();    // 释放终端锁
}

/**
 * @brief 终端中输出十六进制整数（带锁操作）
 * 
 * 该函数用于在终端中输出指定的十六进制整数。输出时，通过获取并释放控制台锁来实现互斥。
 * 
 * @param uint32_t num 要输出的十六进制整数。
 * @return void 该函数不返回任何值。
 */
void console_put_int(uint32_t num) {
    console_acquire();    // 获取终端锁
    put_int(num);         // 输出十六进制整数
    console_release();    // 释放终端锁
}
