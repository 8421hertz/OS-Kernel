#include "string.h"
#include "global.h"
#include "debug.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * @brief 将 dst_ 内存块的起始的前 size 个字节置为 value 值
 *
 * 用于内存区域的数据初始化，原理是逐字节地把 value 写入起始内存地址为 dst_ 的 size 个空间，在本系统中通常用于内存分配时的数据清 0。
 *
 * @param dst_ 指向需要设置内存的指针
 * @param value 要设置的字节值
 * @param size 要设置的字节数量
 *
 * @return void 该函数没有返回值。
 */
void memset(void *dst_, uint8_t value, uint32_t size)
{
    ASSERT(dst_ != NULL);
    uint8_t *dst = (uint8_t *)dst_;
    while (size-- > 0)
    {
        *dst++ = value;
    }
}

/**
 * @brief 将 src_ 起始的 size 个字节复制到 dst_
 *
 * 用于内存数据拷贝，原理是将 src_ 起始的 size 个字节复制到 dst_，逐字节拷贝。
 *
 * @param dst_ 目标内存指针
 * @param src_ 源内存指针
 * @param size 要复制的字节数
 *
 * @return void 该函数没有返回值。
 */
void memcpy(void *dst_, const void *src_, uint32_t size)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    uint8_t *dst = (uint8_t *)dst_;
    const uint8_t *src = src_; // C 语言允许这种隐式转换，它默认从 const void* 转换为 const uint8_t* 而不会报错
    while (size-- > 0)
    {
        *dst++ = *src++;
    }
}

/**
 * @brief 比较内存区域以 a_ 和 b_ 为起始的 size 个字节
 *
 * @param a_ 第一个内存区域的指针
 * @param b_ 第二个内存区域的指针
 * @param size 需要比较的字节数量
 *
 * @return int 如果两者相等返回 0；如果 *a_ 大于 *b_，返回 1；否则返回 -1。
 */
int memcpm(const void *a_, const void *b_, uint32_t size)
{
    const char *a = a_;
    const char *b = b_;
    ASSERT(a != NULL && b != NULL);
    while (size-- > 0)
    {
        if (*a != *b)
        {
            return (*a > *b) ? 1 : -1;
        }
        a++;
        b++;
    }
    return 0;
}

/**
 * @brief 把起始于地址 src_ 的字符串复制到地址 dst_，以 src_ 处的字符 '0' 作为终止条件
 *
 * @param dst_ 目标字符串指针
 * @param src_ 源字符串指针
 *
 * @return char* 返回目标字符串的起始地址
 */
char *strcpy(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char *r = dst_; // 用来返回目的字符串起始地址
    while (*dst_++ = *src_++)
        ;
    return r;
}

/**
 * @brief 计算并返回以 str 开头的字符串的长度，不包括终止字符 '\0'。
 *
 * @param str 输入的字符串
 *
 * @return uint32_t 返回字符串的长度。
 */
uint32_t strlen(const char *str)
{
    ASSERT(str != NULL);
    const char *p = str;
    while (*p++) // while 循环结束后，p 会在 '\0' 的后一个位置
        ;
    return (p - str - 1);
}

/**
 * @brief 比较两个字符串
 *
 * 比较字符串 a 和 b 的大小。如果 a 大于 b，返回 1；相等返回 0；否则返回 -1。
 * 以地址 a 处的字符串的长度，也就是直到字符 0 为终止条件
 *
 * @param a 第一个字符串
 * @param b 第二个字符串
 *
 * @return int8_t 比较结果，a > b 返回 1；a == b 返回 0；a < b 返回 -1。
 */
int8_t strcmp(const char *a, const char *b)
{
    ASSERT(a != NULL && b != NULL);
    while (*a != 0 && *b != 0)
    {
        a++;
        b++;
    }
    /**
     * 如果 *a 小于 *b 就返回 −1，否则就属于 *a 大于等于 *b 的情况。
     * 在后面的布尔表达式 *a > *b 中，若 *a 大于 *b，表达式就等于 1，
     * 否则表达式不成立，也就是布尔值为 0，恰恰表示 *a 等于 *b
     */
    return (*a < *b) ? -1 : *a > *b;
}

/**
 * @brief 查找字符串中首次出现的字符
 *
 * 在字符串 str 中查找字符 ch，返回首次出现的地址。
 *
 * @param str 输入字符串
 * @param ch 要查找的字符
 *
 * @return char* 返回字符首次出现的位置，如果未找到则返回 NULL。
 */
char *strchr(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL);
    while (*str != 0)
    {
        if (*str == ch)
        {
            return (char *)str; // 需要强制转化成和返回值类型一样
        }
        str++;
    }
    return NULL;
}

/**
 * @brief 查找字符串中最后一次出现的字符
 *
 * 在字符串 str 中查找字符 ch，返回最后一次出现的地址。
 *
 * @param str 输入字符串
 * @param ch 要查找的字符
 *
 * @return char* 返回字符最后一次出现的位置，如果未找到则返回 NULL。
 */
char *strrchr(const char *str, const uint8_t ch)
{
    ASSERT(str != NULL);
    const char *last_char = NULL;
    while (*str != 0)
    {
        if (*str == ch)
        {
            last_char = str;
        }
        str++;
    }
    return (char *)last_char;
}

/**
 * @brief 拼接两个字符串
 *
 * 将源字符串 src_ 拼接到目标字符串 dst_ 的末尾，返回拼接后的字符串地址。
 *
 * @param dst_ 目标字符串指针
 * @param src_ 源字符串指针
 *
 * @return char* 返回拼接后的目标字符串的地址。
 */
char *strcat(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char *str = dst_;
    while (*str++) // 找到 dst_ 的结尾
        ;
    --str;                   // 回到字符串的末尾 '\0' 位置
    while (*str++ = *src_++) // 逐个字符赋值 src_，包括末尾的 '\0'
        ;
    return dst_;
}

/**
 * @brief 计算字符在字符串中出现的次数
 *
 * 计算字符 ch 在字符串 str 中出现的次数。
 *
 * @param str 输入字符串
 * @param ch 要计算的字符
 *
 * @return uint32_t 返回字符 ch 出现的次数。
 */
uint32_t strchrs(const char *str, uint8_t ch)
{
    ASSERT(str != NULL);
    uint32_t ch_cnt = 0;
    const char *p = str;
    while (*p != 0)
    {
        if (*p == ch)
        {
            ch_cnt++;
        }
        p++;
    }
    return ch_cnt;
}