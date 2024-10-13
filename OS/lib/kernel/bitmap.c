#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

/**
 * @brief 将位图 btmp 初始化
 *
 * 该函数负责初始化位图 btmp 的所有位，将它们全部设置为 0。
 *
 * @param struct bitmap* btmp 指向需要初始化的位图结构的指针
 */
void bitmap_init(struct bitmap *btmp)
{
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/**
 * @brief 通过索引位 bit_idx，判断位图中的该位是否为 1。
 *
 * 此函数是被 bitmap_scan 调用的，当想在位图中获得连续多个可用位时，逐次调用 bitmap_scan_test 依次判断
 *
 * @param struct bitmap* btmp 指向位图结构的指针
 * @param uint32_t bit_idx 需要测试的位索引
 *
 * @return bool 如果该位为 1，返回 true，否则返回 false
 */
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx)
{
    uint32_t byte_idx = bit_idx / 8; // 计算所在字节的位于数组中的下标
    uint32_t bit_odd = bit_idx % 8;  // 计算在字节中的位偏移
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/**
 * @brief 在位图中申请连续的空闲位
 *
 * 该函数负责在位图中寻找连续的 cnt 个空闲位，并返回其起始位的下标。
 *
 * @param struct bitmap* btmp 指向位图结构的指针
 * @param uint32_t cnt 需要申请的连续位的数量

 * @return int 找到的连续位起始下标，若失败则返回 -1
 */
int bitmap_scan(struct bitmap *btmp, uint32_t cnt)
{
    uint32_t idx_byte = 0; // 用于记录空闲位所在的字节索引
    // 寻找第一个不为 0xff 的字节（字节的值不为 0xff，说明该字节里面至少有一个 0）
    while ((0xFF == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len))
    {
        // 表示该字节已分配，若为 0xff，则表示该字节内已无空闲位，向下一个字节继续找
        idx_byte++;
    }

    ASSERT(idx_byte < btmp->btmp_bytes_len);

    if (idx_byte == btmp->btmp_bytes_len) // 如果没有找到空闲字节（该字节数组找不到可用空间），返回 -1
    {
        return -1;
    }

    // 若在位图数组范围内的某字节内找到空闲位
    // 在该字节内逐位扫描，记录字节内第一个空闲位的下标
    int idx_bit = 0;
    // 和 btmp->bits[idx_byte] 这个字节逐位对比（查找字节内的空闲位）
    while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte])
    {
        idx_bit++;
    }

    uint32_t bit_idx_start = idx_byte * 8 + idx_bit; // 将字节内位索引转换为整个位图的位索引

    // 若只需要 1 个空闲位，直接返回
    if (cnt == 1)
    {
        return bit_idx_start;
    }

    uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start - 1); // 记录还有多少位可以判断
    uint32_t next_bit = bit_idx_start + 1;                              // 记录下一个待查找的位
    uint32_t count = 1;                                                 // 记录找到的空闲位的个数

    bit_idx_start = -1; // 先将其置为 -1，若找不到连续的位就直接返回

    while (bit_left-- > 0)
    {
        if (!(bitmap_scan_test(btmp, next_bit))) // 判断下一个待查找的 next_bit 位是否为 0
        {
            count++; // 找到空闲位，计数加 1
        }
        else
        {
            count = 0; // 如果遇到占用的位，重新计数
        }

        if (count == cnt) // 若找到连续 cnt 个空闲位
        {
            bit_idx_start = next_bit - cnt + 1; // 连续 cnt 个空闲位的起始下标
            break;
        }

        next_bit++;
    }

    return bit_idx_start;
}

/**
 * @brief 设置位图中指定位的值
 *
 * 该函数负责将位图 btmp 的指定索引位 bit_idx 设置为 value。
 *
 * @param struct bitmap* btmp 指向位图结构的指针
 * @param uint32_t bit_idx 需要设置的位图中的位索引
 * @param int8_t value 要设置的值（0 或 1）
 *
 * @return void 该函数没有返回值
 */
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value)
{
    ASSERT((value == 0) || (value == 1)); // 确保 value 值合法
    uint32_t byte_idx = bit_idx / 8;      // 计算所在字节的位于数组中的下标
    uint32_t bit_odd = bit_idx % 8;       // 计算在字节中的位偏移

    if (value)
    {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    }
    else
    {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
}
