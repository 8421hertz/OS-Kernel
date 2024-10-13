#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H
#include "global.h"
#define BITMAP_MASK 1 // 用来在位图中逐位判断，主要就是通过按位于 & 来判断相应位是否为 1

typedef int bool;
#define true 1
#define false 0

struct bitmap
{
    uint32_t btmp_bytes_len; // 位图的字节长度
    // 在遍历位图时，整体上以字节为单位，细节上是以为为单位，所以此处位图的指针必须是单字节
    uint8_t *bits; // 位图的指针
};

void bitmap_init(struct bitmap *btmp);
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx);
int bitmap_scan(struct bitmap *btmp, uint32_t cnt);
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value);

#endif