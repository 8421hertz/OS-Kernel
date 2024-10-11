#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

void panic_spin(char *filename, int line, const char *func, const char *condition);

/* ******************************************* __VA_ARGS__ *******************************************
 * __VA_ARGS__ 是预处理器所支持的专用标识符，代表所有与省略号对应的参数（只允许在具有可变参数的宏替换列表中出现）。
 * "..."表示定义的宏参数可变，可以传递任意数量的参数。
 * 在下面的 PANIC 宏中，__VA_ARGS__ 代表传递给该宏的可变参数。 */
#define PANIC(...) panic_spin(__FILE__, __LINE__, __func__, __VA_ARGS__)
/***************************************************************************************************/

/*
 * ASSERT 宏用于在调试阶段进行断言检测。在调试时，如果条件 CONDITION 为 false，
 * 将调用 PANIC 宏并输出错误信息；在非调试状态下(NDEBUG 定义时)，ASSERT 不执行任何操作。
 */
#ifdef NDEBUG
// 如果定义了NDEBUG，则将 ASSERT 宏定义为空操作，即 ((void)0) ———— （相当于删除了 ASSERT）。
// 这样当程序不再处于调试模式时，ASSERT 不会对程序产生任何影响，从而减少程序体积和运行开销。
// ASSERT 是在调试过程中使用的，宏毕竟是一段代码，经预处理器展开后，调用宏的地方越多，程序体积越大，所以执行得越慢。
// 所以，当我们不再调试时，就应该让此宏失效，这样编译出来的程序才比较小，运行会稍快一些。
#define ASSERT(CONDITION) ((void)0)
#else
// 如果没有定义 NDEBUG，则定义 ASSERT 宏为一个条件判断语句。
// 如果 CONDITION 为真，什么都不做；否则，调用 PANIC 宏来报告错误。
#define ASSERT(CONDITION)                                                             \
    if (CONDITION)                                                                    \
    {                                                                                 \
    }                                                                                 \
    else                                                                              \
    {                                                                                 \
        /* 符号 # 将 CONDITION 转为字符串字面量，用于报错时显示 */ \
        PANIC(#CONDITION);                                                            \
    }
#endif // NDEBUG

#endif // __KERNEL_DEBUG_H