#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"

// 虚拟内存池结构，用于虚拟地址管理
struct virtual_addr
{
    struct bitmap vaddr_bitmap;     // 虚拟地址用到的位图结构（以页为单位，管理虚拟内存地址的分配情况）
    uint32_t vaddr_start;           // 虚拟地址起始地址
    // 尽管虚拟地址空间最大是 4GB，但相对来说是无限的，不需要指定地址空间大小
};

extern struct pool kernel_pool, user_pool;
static void mem_pool_init(uint32_t all_mem);
void mem_init(void);

#endif