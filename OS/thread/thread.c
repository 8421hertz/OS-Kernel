#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"

#define PG_SIZE 4096 // PCB 的大小为 4K

struct task_struct *main_thread;     // ① 主线程的 PCB 指针
                                     // 进入内核后一直执行的是 main 函数，它就是一个线程，后面会将其完善成线程的结构（加入全部线程队列），因此为其提前定义了个 PCB
struct list thread_ready_list;       // ② 就绪队列（调度器要选择某个线程上处理器的话，必然先将所有线程收集到就绪队列，以后每创建一个线程就将其加到此队列中）
struct list thread_all_list;         // ③ 所有线程的队列（如果线程因为某些原因阻塞，不能放在就绪队列中，需要有个地方能找到它，得知道我们共创建了多少线程）
static struct list_elem *thread_tag; // ④ 用于保存队列中的线程节点（队列中的节点不是线程的 PCB，而是线程 PCB 中的 tag，即 general_tag 或 all_list_tag）
                                     // 对线程的管理都是基于线程 PCB 的，因此必须要将 tag 转换成 PCB，在转换过程中需要记录 tag 的值

// extern void switch_to(struct task_struct *cur, struct task_strct *mext);

/**
 * @brief 返回取当前线程的 PCB 指针
 *
 * 由于各个线程所用的 0 级栈都是在自己的 PCB 当中，因此取当前栈指针高 20 位作为当前运行线程的 PCB
 *
 * 从当前线程的内核栈顶指针 esp 获取该线程的 PCB 指针。
 * 通过获取 esp 寄存器的值，取其高 20 位，即 PCB 起始地址。
 *
 * @return struct task_struct* 返回当前线程的 PCB 指针。
 */
struct task_struct *running_thread()
{
    uint32_t esp;
    asm("mov %%esp, %0" : "=g"(esp)); // 获取当前的 esp 寄存器的值
    /* 取 esp 的高 20 位，即当前 PCB 的起始地址（由于 PCB 占一页，所以高 20 位虚拟地址能够获取物理页的物理地址） */
    return (struct task_struct *)(esp & 0xFFFFF000);
}

/**
 * @brief 由 kernel_thread 去执行传入的函数 function(func_arg)
 *
 * 该函数调用传入的函数指针 function 并传递参数 func_arg。
 * 该函数由 ret 线程栈中的函数指针进入，并且两个参数分别对应线程栈中 ebp + 4 和 ebp + 8 ———— 根据 ABI 调用者约定，被调用函数的栈帧中的栈顶会被认为是调用者函数的返回地址
 *
 * 任务调度机制基于时钟中断，由时钟中断这种 "不可抗力" 来中断所有任务的执行，借此将控制权交到内核手中，由内核的任务调度器 schedule 考虑将处理器使用权发放到某个任务手中，
 * 下次中断再发生时，权利将再被回收，周而复始，这样便保证操作系统不会被 "架空"，而且保证所有任务都有运行的机会
 *
 * 线程的首次运行是由时钟中断处理函数调用任务调度器 schedule 完成的，进入中断后处理器会自动关中断，因此在执行 function 前要打开中断
 * 否则 kernel_thread 中的 function 在关中断的情况下运行（时钟中断被屏蔽），再也不会调度到新的线程，function 会独享处理器
 *
 * @param function 函数指针，kernel_thread 中调用的函数（ebp + 4）
 * @param func_arg 传递给函数 function 的参数（ebp + 8）
 */
static void kernel_thread(thread_func *function, void *func_arg)
{
    /* 执行 function 前要开中断（任务调度的保证）, 避免后续的时钟中断被屏蔽，导致无法调度其他线程 */
    intr_disable();
    function(func_arg); // 执行传入的函数 function，并传递参数 func_arg
}

/**
 * @brief 初始化线程栈
 *
 * 初始化线程栈 thread_stack，待执行的函数和参数放到 thread_stack 中相应的位置。
 *
 * @param pthread 待创建的线程的指针（指向线程的 PCB）
 * @param function 函数指针，表示在线程中执行的函数
 * @param func_arg 传递给线程执行函数 function 的参数
 */
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg)
{
    /**
     * 先为线程所使用的中断栈 struct intr_stack 预留空间，可见 thread.h 中的定义
     * ① 将来线程进入中断后，位于 kernel.S 中的中断入口代码会通过此栈来保存上下文
     * ② 将来实现用户进程时，会将用户进程的初始信息放在中断栈中
     */
    pthread->self_kstack -= sizeof(struct intr_stack);   // 为中断栈预留空间（此时 self_kstack 指向 PCB 中的中断栈下面的地址）
    pthread->self_kstack -= sizeof(struct thread_stack); // 预留出线程栈空间
                                                         // (unused_retaddr)就在线程栈中
    // 定义线程栈指针
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack; // 获取线程栈指针

    /************************ 这三行就是为了在 kernel_thread 中调用 function(arg) 做准备 ***************************
     * 当 kernel_thread 函数开始执行时，处理器会认为当前栈顶是调用者的返回地址，因此它会从当前栈顶 +4 找到函数的参数
     */
    // 线程是使函数单独上处理器运行的机制，因此线程肯定得知道要运行在哪个函数上，首次执行某个函数时，这个栈就用来保存待运行的函数，其中 eip 便是该函数的地址
    // 将来用 switch_to 函数实现任务切换，当任务切换时，此 eip 用于保存任务切换后的返回的新任务的地址
    /* 线程第一次执行时，eip 指向待调用的函数 kernel_thread，其他时候 eip 指向 switch_to 返回的新任务的地址 */
    // 通过 ret 线程栈中的 eip 地址从而跳转到 kernel_thread 函数中（并且此函数的两个参数也在线程栈中）
    kthread_stack->eip = kernel_thread; // 设置线程入口点为 kernel_thread
                                        // (kernel_thread 不是通过 call 指令调用的，而是通过 ret 来执行的，
                                        // 因此无法按照正常的函数调用形式传递 kernel_thread 所需要的参数 ———— kernel_thread(function, func_argc)，
                                        // 只能将参数放在 kernel_thread 所用的线程栈中，即处理器进入 kernel_thread 函数体时，栈顶会被认为是调用者函数的返回地址)
                                        // 栈顶 +4 为参数 function，栈顶 +8 为参数 func_argc
    kthread_stack->function = function; // 传递函数指针
    kthread_stack->func_arg = func_arg; // 传递函数参数

    // 因为线程中的函数尚未执行，在执行过程中寄存器才会有值，此时置为 0 即可
    /**
     * 下面的 4 个寄存器（ABI 中是 5 个）归主调函数所用，被调函数需要保护好这 4 个函数的值，在被调函数运行完后，这 5 个寄存器的值必须和运行前一样，被调函数必须在自己的栈中存储这些寄存器的值
     * 		switch_to 函数是我们用汇编语言实现的，它是被内核调度器函数调用的，因此这里面涉及到主调函数寄存器的保护（将它们保存在栈中），就是 ebp、ebx、edi 和 esi 这 4 个寄存器。
     */
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->edi = kthread_stack->esi = 0;

    // 线程栈中的 kthread->unused_retaddr 是不需要赋值的，因为它只是用来占位的（调动函数返回地址）
}

/**
 * @brief 初始化线程的基本信息（PCB）
 *
 * 初始化线程的基本信息，包括名称、优先级、状态等，并设置线程的栈顶位置（PCB 一级的其他初始化）。
 *
 * @param pthread 待初始化的线程 PCB(线程控制块) 的指针
 * @param name 线程名称
 * @param prio 线程优先级
 */
void init_thread(struct task_struct *pthread, char *name, int prio)
{
    memset(pthread, 0, sizeof(pthread)); // 将线程所在的线程控制块(PCB)全部清0 ———— 一页
    strcpy(pthread->name, name);         // 设置线程名称
    if (pthread == main_thread)          // 判断是否为主线程
    {
        /* 由于主线程 main 已经在内核中运行，所以将其状态设为 TASK_RUNNING */
        pthread->status = TASK_RUNNING;
    }
    else
    {
        /* 其他线程，将其状态设置为准备运行 */
        pthread->status = TASK_READY;
    }
    /* self_kstack 是线程自己在内核态下使用的栈顶地址 */
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE); // 在线程创建之初，设置 0 特权级的内核栈指针（中断栈、线程栈）为 PCB 的最顶端
    pthread->priority = prio;                                         // 设置线程优先级（将来它的作用体现任务（线程和进程的统称）在处理器上执行的时间片长度，即优先级越高，执行的时间片越长）
    pthread->ticks = prio;                                            // 初始化时间片
    pthread->elapsed_ticks = 0;                                       // 线程已经运行的时间片数为 0（表示线程尚未执行过）
    pthread->pgdir = NULL;                                            // 用户线程没有自己的地址空间(没有页表)，因此将线程的页表置为 NULL
    // PCB 的最顶端是 0 特权级栈，将来线程在内核态下的任何操作都是用此 PCB 中的栈，如果出现了某些异常导致入栈操作过多，这会破坏 PCB 低处的线程信息
    // 为此，需要检测这些线程信息是否被破坏了，stack_magic 被安排在线程信息的最边缘，它作为栈的边缘
    // 目前用不到此值，以后再进行线程调度时会检测它
    pthread->stack_magic = 0x19870916; // 设置内核保护的自定义魔数，用于检测栈溢出（自定义个值就行，这与代码功能无关）
}

/**
 * @brief 创建并启动一个新线程
 *
 * 创建一个优先级为 prio 的线程，线程名为 name，线程所执行的函数为 function，参数为 func_arg ———— function(func_arg)。
 *
 * 新创建的线程会被加入到就绪队列和全局队列中
 *
 * @param name 线程名称
 * @param prio 线程优先级
 * @param function 函数指针，表示在线程中执行的函数
 * @param func_arg 传递给线程执行函数的参数
 * @return struct task_struct* 返回新创建的线程 PCB 指针
 */
struct task_struct *thread_start(char *name,
                                 int prio,
                                 thread_func function,
                                 void *func_arg)

{
    struct task_struct *thread = get_kernel_pages(1); // 从内核空间中分配一页内存（4096）作为 PCB 的起始地址
                                                      // 注意：无论是进程或线程的 PCB，这都是给内核调度器使用的结构，属于内核管理的数据，因此将来用户进程的 PCB 也要依然从内核物理内存池中申请
    init_thread(thread, name, prio);                  // 初始化刚刚创建的 thread 线程的 PCB
    thread_create(thread, function, func_arg);        // 初始化线程栈 thread_stack，待执行的函数和参数放到 thread_stack 中相应的位置

    // 使用汇编代码启动线程，将线程栈顶设为 esp（此时 esp 为指向线程栈的最低处），并弹出被调用函数需要保护的 4 个寄存器值（ABI）
    // 通过 ret 指令返回到 void (*eip)(thread_func *func, void *func_arg); 所对应的 kernel_thread 函数（这个函数的两个参数通过 ebp +4 和 +8 得到，ebp 会自动跳过 unused_retaddr 返回地址占位符）
    // asm volatile("movl %0, %%esp; \
    //               pop %%ebp; \
    //               pop %%ebx; \
    //               pop %%esi; \
    //               pop %%edi; \
    //               ret"
    //              : : "g"(thread->self_kstack) : "memory");

    /* 确保线程"标签"节点没在就绪队列 thread_ready_list 中 */
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    // 队列中的节点就是线程 PCB 的成员 general_tag 线程"标签"节点（优点：struct list_elem 节点类型只有 8 字节，这个队列显得轻量小巧；如果使用 PCB 做节点，尺寸太大）
    list_append(&thread_ready_list, &thread->general_tag); // 将线程"标签"节点加入到就绪队列中

    /* 确保线程"标签"节点没在全局队列 thread_all_list 中 */
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->general_tag); // 将线程"标签"节点加入到全局队列中

    return thread; // 返回线程 PCB 的指针
}

/* 实现任务调度 */
void schedule()
{
    // 确保在调度时中断已经关闭
    ASSERT(intr_get_status() == INTR_OFF);

    // 获取当前正在运行的线程 PCB 指针
    struct task_struct *cur = running_thread();
    if (cur->status == TASK_RUNNING)
    {
        // 如果当前线程 cur 的时间片 ticks 到了，将其加入到就绪队列的尾部
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        // 将当前线程的时间片 ticks 重置为其优先级 priority（为了下次运行时不会马上被换下处理器）
        cur->ticks = cur->priority;
        cur->status = TASK_READY; // 将状态设置为就绪
    }
    else
    {
        // 如果线程因为某事件阻塞被换下处理器，不需要将其加入到就绪队列，因为当前线程并不在就绪队列中（需要事件完成后才能继续上 CPU 运行）
        // （比如对 0 值的信号就行 P 操作就会让线程阻塞，到同步机制时会介绍）
    }

    // 确保就绪队列中有现成可供调度
    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL; // 清空全局变量 thread_tag，避免上次的残留值影响（由于是全局变量）

    // 从就绪队列 thread_ready_list 中弹出第一个线程，准备将其调度到 CPU 上
    thread_tag = list_pop(&thread_ready_list);
    // thread_tag 并不是线程，它仅仅是线程 PCB 中的 general_tag 或 all_list_tag，要获得线程的信息，必须将其转换成 PCB 指针才行，因此我们用到了宏 elem2entry
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING; // 设置新线程的状态为运行中（表示新线程可以上处理器了）
    switch_to(cur, next);   // 切换新线程（切换寄存器映像）———— 将线程 cur 的上下文保护好，再将线程 next 的上下文装在到处理器，实现任务切换
}

/**
 * @brief 将内核中的 main 函数设置为主线程
 *
 * 初始化主线程 PCB，主线程已在内核中运行（不需要再为其申请页安装其 PCB，也不需要再通过 thread_create 构造它的线程栈），只需要通过 init_thread 填充其名称和优先级
 *
 * 只需要将主线程加入到全部队列 thread_all_list 中（存储所有线程，包括就绪的、阻塞的、正在执行的）
 *
 * 该函数将 main 函数作为内核线程处理。
 *
 * @return void 该函数没有返回值
 */
static void make_main_thread(void)
{
    /* main 函数早已在内核中运行，将其 PCB 地址从 esp 获取 */
    main_thread = running_thread();
    init_thread(main_thread, "main", 31); // 初始化主线程

    /* 主线程不在就绪队列中（正在运行），不需要加入到就绪队列，加入到全局队列中就行 */
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag); // 加入全局队列队尾
}
