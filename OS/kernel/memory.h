#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"

// 虚拟内存池结构，用于虚拟地址管理
struct virtual_addr
{
    struct bitmap vaddr_bitmap; // 虚拟地址用到的位图结构（以页为单位，管理虚拟内存地址的分配情况）
    uint32_t vaddr_start;       // 虚拟地址起始地址
    // 尽管虚拟地址空间最大是 4GB，但相对来说是无限的，不需要指定地址空间大小
};

/* 内存池标记，用于判断是哪个地址池 */
enum pool_flags
{
    PF_KERNEL = 1, // 内核物理内存池
    PF_USER = 2    // 用户物理内存池
};

// 以下各属性的值是以它们的位次来定义的，并不是 0 或 1，这样方便后面的页表项或页目录项的属性合成
#define PG_P_1 1    // 页表项或页目录项存在属性位（此页表项或页目录项已存在） ———— 第 0 位
#define PG_P_0 0    // 页表项或页目录项存在属性位（此页表项或页目录项不存在） ———— 第 0 位
#define PG_RW_R 0   // R/W 属性位值，读/执行
#define PG_RW_W 2   // R/W 属性位值，读/写/执行
#define PG_US_S 0   // U/S 属性位值，系统级（表示只允许特权级别为 0、1、2 的程序访问此页内存，3 特权级程序不被允许）
#define PG_US_U 4   // U/S 属性位值，用户（表示允许所有特权级别程序访问此页内存）



extern struct pool kernel_pool, user_pool;
static void mem_pool_init(uint32_t all_mem);
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt);
uint32_t *pte_ptr(uint32_t vaddr);
uint32_t *pde_ptr(uint32_t vaddr);
static void *palloc(struct pool *m_pool);
static void page_table_add(void *_vaddr, void *_page_phyaddr);
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void *get_kernel_pages(uint32_t pg_cnt);
void mem_init(void);

#endif