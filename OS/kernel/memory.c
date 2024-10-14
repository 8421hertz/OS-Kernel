#include "memory.h"
#include "bitmap.h"
#include "stdint.h"
#include "global.h"
#include "debug.h"
#include "print.h"
#include "string.h"

#define PG_SIZE 4096 // 页的大小（4096B，4K）

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) // 返回虚拟地址的高 10 位（用于在页目录表中定位 pde）
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12) // 返回虚拟地址的中间 10 位（用于在页表中定位 pte）

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
struct virtual_addr kernel_vaddr;   // 此虚拟内存池用于给内核分配虚拟地址（起始地址为 0xc0100000）———— 内核所使用的堆空间的起始虚拟地址
                                    // 0xc0000000 是内核从虚拟地址 3G 起。0x100000 意指跨过低端 1MB 内存, 使虚拟地址在逻辑上连续

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
 * @brief 在虚拟内存池中申请指定页数的虚拟页
 *
 * 该函数在 pf 表示的虚拟内存池中申请 pg_cnt 个虚拟页，成功则返回虚拟页的起始地址，失败则返回 NULL。
 *
 * 针对内核内存池和用户内存池不同处理，暂未实现用户内存池。
 *
 * @param enum pool_flags pf 内存池标志（内核或用户物理内存池）
 * @param uint32_t pg_cnt 申请的页数
 * @return void* 成功返回虚拟页起始地址，失败返回NULL
 */
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int vaddr_start = 0;    // 存储分配的起始虚拟地址
    int bit_idx_start = -1; // 存储位图扫描函数 bitmap_scan 的返回值，默认为 -1
    uint32_t cnt = 0;       // cnt 为连续空闲位的个数
    if (pf == PF_KERNEL)    // 判断是否是在内核虚拟地址池中申请地址
    {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt); // 扫描内核虚拟地址池中的位图
        // 内核虚拟地址池的位图中没有连续 pg_cnt 个空闲位
        if (bit_idx_start == -1)
        {
            return NULL;
        }
        while (cnt < pg_cnt)
        {
            // 根据申请的页数量 pg_cnt，逐次调用 bitmap_set 函数将内核虚拟地址池的位图相应位置 1
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        // 将 bit_idx_start 转换为虚拟地址（虚拟内核内存池的起始地址为 0xc0100000）
        // 位图中起始位索引 bit_idx_start 相对于内存池的虚拟页偏移地址 bit_idx_start * PG_SIZE（位图的 1bit 代表实际 1K 大小的内存）
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    }
    else
    {
        // 用户内存池，将来实现用户进程时再补充
    }
    return (void *)vaddr_start; // 将 vaddr_start 转换成指针后返回
}

/**
 * @brief 获取虚拟地址 vaddr 对应的页表项 pte 指针（指针的值是虚拟地址）
 *
 * 该函数接受虚拟地址 vaddr，返回对应页表项 pte 的指针，利用虚拟地址计算出页表项所在的物理地址。
 *
 * @param uint32_t vaddr 虚拟地址
 *
 * @return uint32_t* 对应页表项 pte 的指针（能够访问 vaddr 所在的 pte 的虚拟地址）
 */
uint32_t *pte_ptr(uint32_t vaddr)
{
    // 最后一个页目录项里写的是页目录表自己的物理地址，目的是为了通过此页目录项编辑页表

    /** 通过页表映射获取页表项的地址
     * ① 先访问到页表自己
     * ② 再用页目录项 PDE（页目录项的索引）作为 pte 的索引访问页表
     * ③ 再用 pte 的索引作为内偏移
     */
    uint32_t *pte = (uint32_t *)(0xffc00000 +                   // 0xffc00000 中的前 10 位代表 第 1023 个页表项
                                 ((vaddr & 0xffc00000) >> 10) + // 用 vaddr 的前 10 位索引页目录项（此页目录项中记录的是页表的物理地址）
                                 PTE_IDX(vaddr) * 4);           // 用 vaddr 的中 10 位索引页表项（页表项索引 * 4B = 页表项的物理地址） ———— 每个页表项大小为 4B

    return pte;
}

/**
 * @brief 获取虚拟地址 vaddr 对应的页目录项 pde 的指针（指针的值是虚拟地址）
 *
 * 该函数接受虚拟地址 vaddr，返回对应页目录项 pde 的指针，利用虚拟地址计算出页目录项所在的物理地址。
 *
 * 目前页表中不存在的虚拟地址， pte_ptr 和 pde_ptr 这两个函数只是根据虚拟地址转换的规则计算出 vaddr 对应的 pte 及 pde 的虚拟地址，与 vaddr 所在的 pte 及 pde 是否存在无关。
 *
 * @param uint32_t vaddr 虚拟地址
 *
 * @return uint32_t* 对应页目录项 pde 的指针
 */
uint32_t *pde_ptr(uint32_t vaddr)
{
    /* 通过页目录表获取页目录项的地址 */
    // 0xfffff000 对应的的还是页目录表的物理地址
    uint32_t *pde = (uint32_t *)(0xfffff000 + PDE_IDX(vaddr) * 4); // 此时指针变量 pde 指向 vaddr 所在的 pde
    return pde;
}

/**
 * @brief 在物理内存池中分配一页物理内存
 *
 * 该函数在 m_pool 指向的物理内存池中分配 1 页物理内存，成功返回页框的物理地址，失败返回 NULL。
 *
 * @param struct pool* m_pool 指向物理内存池的指针
 * @return void* 成功返回分配的物理页地址，失败返回 NULL
 */
static void *palloc(struct pool *m_pool)
{
    /* 扫描或设置位图要保持原子操作，分配一个物理页面 */
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1); // 返回在位图中的起始索引位
    if (bit_idx == -1)
    {
        return NULL; // 分配失败
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);                           // 将 bit_idx 索引位置 1
    uint32_t page_phyaddr = (m_pool->phy_addr_start + (bit_idx * PG_SIZE)); // 保存分配的物理页地址（物理内存池的起始地址 m_pool-> phy_addr_start+ 物理页在内存池中的偏移地址（bit_idx * PG_SIZE））
    return (void *)page_phyaddr;
}

/**
 * @brief 添加虚拟地址 _vaddr 和物理地址 _page_phyaddr 的映射
 *
 * @param _vaddr 要映射的虚拟地址
 * @param _page_phyaddr 要映射到虚拟地址的物理地址
 *
 * 此函数在页表中创建虚拟地址 _vaddr 和物理地址 _page_phyaddr 之间的映射。
 * (本质上是在页表中添加此虚拟地址对应的页表项 pte，并把物理页的物理地址写入此页表项 pte 中)
 * 它确保在创建页表项 (PTE) 之前，页目录项 (PDE) 已经创建。
 * 如果页表项已经存在，为了避免冲突，程序会报错。
 */
static void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr;               // 要映射的虚拟地址
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr; // 要映射到虚拟地址的物理地址
    uint32_t *pde = pde_ptr(vaddr);                  // 获取虚拟地址所在的页目录项虚拟地址指针
    uint32_t *pte = pte_ptr(vaddr);                  // 获取虚拟地址所在的页表项虚拟地址指针

    /************   注意   ************
     * 由于目前页表中不存在的虚拟地址，pte_ptr 和 pde_ptr 这两个函数只是根据虚拟地址转换的规则计算出 vaddr 对应的 pte 及 pde 的虚拟地址，与 vaddr 所在的 pte 及 pde 是否存在无关。
     * 执行 *pte，可能会访问到空的 pde。所以确保 pde 创建完成后才能执行 *pte，
     * 否则会引发 page_fault。因此在 *pde 为 0 时，
     * pte 只能出现在下面 else 语句块中的 *pde 后面。
     ***********************************/
    /************   注意   ************
     * 访问*pte时，会先访问到对应的页目录项 (PDE)。
     * 因此，必须确保页目录项已创建，否则会引发 page_fault 错误。
     ***********************************/

    // 先在页目录内判断页目录项是否存在，若为 1，则表示已存在
    if (*pde & 0x00000001)
    {
        // 页目录项和页表项的第 0 位为 p，此处判断页目录项是否存在（此处确保页表项不存在，避免重复映射）
        ASSERT(!(*pte & 0x00000001));

        if (!(*pte & 0x00000001)) // 页表项不存在
        {
            *pte = (page_phyaddr | PG_US_S | PG_RW_R | PG_P_1); // 把物理页的物理地址和 pte 属性写入此页表项 pte 中
        }
        else
        { // 目前应该不会执行到这，因为上面的 ASSERT 会先执行
            // 页表项重复，抛出异常
            PANIC("pte repeat!");
            *pte = (page_phyaddr | PG_US_S | PG_RW_R | PG_P_1);
        }
    }
    else
    {
        /* 页目录项 pde 不存在，所以要先创建页目录项再创建页表项 */

        // 从内核内存池分配一个物理页来作为页表
        uint32_t page_phyaddr = (uint32_t)palloc(&kernel_pool);

        *pde = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // 把要添加的 pte 的物理地址和 pde 属性写入此页目录项 pde 中

        // 清空(置 0)分配到的物理页，确保没有旧数据影响页表的正确性
        // 因为这样形成的 pte 风格迥异，也许 P 位为 1，也许 pte 的高 20 位有数值，这就会导致新的页表中平白无故地多了好多页表项，
        // 页表就会“臃肿且破烂不堪”。您可以试一下，如果此处不对用作新页表的物理页清 0 的话，
        // 在 bochs 中用 info tab 来查看页表中的映射关系时，能输出好几屏的内容，而且全不是自己添加的
        /* 分配到的物理页地址 pde_phyaddr 对应的物理内存清 0，
         * （内存系统在回收该物理页的时候并没有对其清 0，因为回收时清 0 是低效的，（内存系统在回收该物理页的时候并没有对其清 0，因为回收时清 0 是低效的））
         * 避免里面的陈旧数据变成了页表项，从而让页表混乱。
         */

        /**
         * 访问到 pde 对应的物理地址，用 pte 取高 20 位便可。
         * 因为 pte 基于该 pde 对应的物理地址内再寻址，最后 12 位作为页表项的索引，现在将页表清 θ，避免旧数据变成页表项
         * 把低 12 位置 0 便是该 pde 对应的页表物理页的起始（第 0 个 pte）
         */
        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE); // pte & 0xfffff000)：得到这个 vaddr 所在页表的虚拟地址

        // 将新物理页的物理地址 page_phyaddr 和相关属性(US=1, RW=1, P=1)写入此 pte 中
        ASSERT(!(*pte & 0x00000001));                       // 确保页表项不存在
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // 创建新的页表项（相当于第 0 个页表项）
    }
}

/**
 * malloc_page - 在 pf 所指向的内存池中分配指定 pg_cnt 数量的页空间，返回起始虚拟地址
 *
 *
 * @param pf: 指定内存池（内核或用户）
 * @param pg_cnt: 要分配的页数量
 *
 * @return: 成功则返回起始虚拟地址，失败返回 NULL
 *
 * 此函数实现三个步骤：（申请虚拟地址，然后为此虚拟地址分配物理地址，并在页表中建立好虚拟地址到物理地址的映射）
 * 1. 通过 vaddr_get 在虚拟地址空间中申请 pg_cnt 个虚拟页。
 * 2. 通过 palloc 在物理内存池中申请 pg_cnt 个物理页。
 * 3. 通过 page_table_add 将以上得到的 虚拟地址 和 物理地址 在页表中建立映射。
 *
 * malloc_page 就是以上三个函数的封装
 */
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840); // 确保申请的页数合理（内核空间和用户空间各约16MB，保守起见 15MB 来限制）
                                         // 15*1024*1024/4096=3840页
    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL)
    {
        return NULL; // 虚拟地址申请失败
    }

    uint32_t vaddr = (uint32_t)vaddr_start; // 分配的虚拟内存起始地址
    uint32_t cnt = pg_cnt;
    struct pool *mem_pool = (pf & PF_KERNEL) ? &kernel_pool : &user_pool; // 判断是内核物理内存池还是用户物理内存池

    /* 虚拟地址是连续的，但物理地址可能不连续，因此逐个映射 */
    while (cnt-- > 0)
    {
        // 在相应的内存池申请物理页
        void *page_phyaddr = palloc(mem_pool); // 失败时要将曾经申请的虚拟地址和物理页全部回滚（恢复虚拟/物理内存池位图），在将来完成内存回收时再补充
                                               // 当申请物理页失败时，应该将曾经已申请成功的虚拟地址和物理地址全部回滚，虽然地址还未使用，但虚拟内存池的位图已经被修改了，如果物理内存池的位图也被修改过，还要再把物理地址回滚。
        if (page_phyaddr == NULL)
        {
            return NULL; // 申请失败时返回 NULL
        }

        page_table_add((void *)vaddr, page_phyaddr); // 在页表中建立映射（将虚拟地址 vaddr 映射为物理地址 page_phyaddr）
        vaddr += PG_SIZE;                            // 移动到下一个虚拟页（继续下一个循环中的申请物理页和页表映射）———— 加上 PG_SIZE 相当于移动到是下一个页表项
    }
    return vaddr_start; // 返回起始虚拟地址
}

/**
 * get_kernel_pages - 从内核物理内存池中申请指定页数的内存
 * @param pg_cnt: 要申请的页数量
 * @return: 成功则返回起始虚拟地址，失败返回 NULL
 *
 * 通过调用 malloc_page 函数实现内存分配，并将分配的页初始化为 0。
 */
void *get_kernel_pages(uint32_t pg_cnt)
{
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt); // 从内核内存池中申请页
    if (vaddr != NULL)                            // 若成功分配（分配的地址不为空），清空分配的页（将页框清 0 后返回）
    {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr; // 返回虚拟地址
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
