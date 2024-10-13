#include "memory.h"
#include "bitmap.h"
#include "stdint.h"
#include "global.h"
#include "debug.h"
#include "print.h"
#include "string.h"

#define PG_SIZE 4096 // 页的大小（4096B，4K）

/****************************  内存位图的基址  *******************************
 * 因为 0xc009f000 是内核主线程 main 栈顶, 0xc009e000 是内核主线程的 PCB 起始地址。
 * 一个页框大小的位图可表示 128MB 内存（4096 * 8 * 4K = 128MB）, 位图位置安排在地址 0xc009a000（0xc009a000~0xc009dfff），可表示 512MB
 * 这样本系统最大支持 4 个页框的位图，即 512MB 内存
 *
 *********************************************************************/
#define MEM_BITMAP_BASE 0xc009a000

/* 0xc0000000 是内核从虚拟地址 3G 起。0x100000 意指跨过低端 1MB 内存, 使虚拟地址在逻辑上连续 */
// 内核所使用的堆空间的起始虚拟地址
#define K_HEAP_START 0xc0100000 // 内核也需要动态申请内存来完成某项工作，动态申请的内存都是在堆空间中完成的，
                                // 我们为此也定义了内核所用的堆空间，堆也是内存，内存就得有地址，从 0xc0100000 虚拟地址开始分配

// 物理内存池结构，用于物理地址管理，生成两个实例用于管理 内核内存池 和 用户内存池 中的所有物理内存
struct pool
{
    struct bitmap pool_bitmap; // 本内存池用到的位图结构（以页为单位，管理物理内存地址的分配情况）
    uint32_t phy_addr_start;   // 本内存池所管理物理内存的起始地址
    uint32_t pool_size;        // 本内存池字节容量（因为物理地址是有限的）
};

struct pool kernel_pool, user_pool; // 生成 物理内核内存池 和 物理用户内存池
struct virtual_addr kernel_vaddr;   // 此虚拟内存池用于给内核分配虚拟地址

/**
 * @brief 初始化物理内存池
 *
 * 根据所有内存容量 all_mem 的大小初始化 物理内核内存池 和 物理用户内存池
 *
 * @param all_mem 物理内存总量
 */
static void mem_pool_init(uint32_t all_mem)
{
    put_str("   mem_pool_init start\n");

    // 页表大小（256 个页框） = 1 页大小的页目录表 + 第 0 和 第 768 个页目录项指向同一个页表（对应低端4MB）+ 第 769~1022 个页目录项共指向 254 个页表
    // （第 1023 个 pde 指向页目录表，不重复计算空间）（1GB~3GB的用户空间还未分配页表）
    uint32_t page_table_size = PG_SIZE * 256; // 记录页目录项和页表占用的字节大小，总大小=页目录表大小+所有页表大小

    uint32_t used_mem = page_table_size + 0x100000; // 已使用的内存字节数 = 页目录项和页表占用的字节大小 + 低端 1MB 内存
    uint32_t free_mem = all_mem - used_mem;         // 可用内存 = 总内存 - 已使用内存
    uint16_t all_free_pages = free_mem / PG_SIZE;   // 将剩余内存转换成页数（1 页 为 4K，不管总内存是不是 4K 的倍数，不足 1 页的内存被忽略）

    uint16_t kernel_free_pages = all_free_pages / 2;               // 存储分配给 内核物理内存池 的空闲物理页数量
    uint16_t user_free_pages = all_free_pages - kernel_free_pages; // 把分配给内核后剩余的空闲物理页作为 用户物理内存池 的空闲物理页

    /**
     * 为了简化位图操作，余数不处理
     * ① 坏处：会丢失内存，(1~7页)*2 的内存 ———— 内核物理内存池+用户物理内存池
     * ② 好处：不用做内存的越界检测，因为位图表示的内存少于实际物理内存
     */
    uint32_t kbm_length = kernel_free_pages / 8; // 内核物理内存池的字节长度（因为位图中的 1 位表示 1 页，以字节为单位，故用 kernel_free_pages 除以 8 以获得位图的字节长度）
    uint32_t ubm_length = user_free_pages / 8;   // 用户物理内存池的字节长度

    uint32_t kp_start = used_mem;                               // 内核物理内存池的起始地址
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 用户物理内存池的起始地址

    // 用以上的两个物理起始地址初始化各自内存池的起始地址（内核物理内存池起始地址 和 用户物理内存池起始地址）
    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    // 用各自的内存池中的容量字节数（物理页书 * PG_SIZE）初始化各自内存池的 pool_size
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;

    // 用各自物理内存池的位图字节长度 kbm_length 和 ubm_length 初始化各自内存池中的位图字节长度成员
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    /************ 初始化 内核物理内存池位图 和 用户物理内存池位图 ************
     * 位图是全局的数组，长度不可定。
     * 全局或静态的数组需要在编译时知道其长度,
     * 而我们需要根据内存池大小计算出位图需要多少字节, （位图的长度取决于具体要管理的内存页数量，因此是无法预计），所以必须在一块内存存在它们的位图。
     ********************************************************/
    // 内核内存池的位图的起始地址是0xc009a000, 这是主线程序的栈地址
    // (内核的大约预订了70KB左右)
    // 32MB内存的位图长度为2KB ———— 2K*1024*1024*4K=32MB

    // 内核物理内存池的位图位于 MEM_BITMAP_BASE (0xc009a000) 处
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;

    // 用户物理内存池的位图位于 MEM_BITMAP_BASE (0xc009a000) + kbm_length
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);

    /************************* 输出内存池信息 *************************/
    // 包括 内存池的所用位图的起始物理地址 和 内存池的起始物理地址
    put_str("       kernel_pool_bitmap_start: ");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start: ");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("       user_pool_bitmap_start: ");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start: ");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    /* 将物理内存池位图初始化为 0 */
    // 0 代表位对应的内存页未分配
    // 1 代表位对应的内存页已分配
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    /* 下面初始化内核虚拟内存地址池位图 */
    // 用于维护内核堆的虚拟地址，所以要和物理内核内存池大小一致
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;

    /* 内核虚拟内存池的位图的数组指向一块未使用的内存，目前将其安排在紧挨着内核物理内存池和用户物理内存池所用的位图之后 */
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);

    // 在 loader 中我们已经通过设置页表把虚拟地址 0xc0000000~0xc00fffff 映射到了物理地址 0x00000000~0x000fffff（低端 1MB 内存）
    // 0xc0100000 是用于给内核动态堆分配内存的虚拟地址
    kernel_vaddr.vaddr_start = K_HEAP_START; // 虚拟内核内存池的起始地址为 0xc0100000
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("   mem_pool_init done\n");
}

/**
 * @brief 内存管理部分的初始化入口
 */
void mem_init(void)
{
    put_str("mem_init start\n");
    // 在 loader.S 中，为了获取内存容量，我们用了三种 BIOS 方法，最终把获取到的内存容量保存在汇编变量 total_mem_bytes 中，其物理地址为 0xb00
    uint32_t mem_bytes_total = *(uint32_t *)(0xb00);
    mem_pool_init(mem_bytes_total);
    put_str("mem_init done\n");
}
