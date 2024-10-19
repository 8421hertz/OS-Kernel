#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"

#define PG_SIZE 4096

/**
 * @brief 由 kernel_thread 去执行传入的函数 function(func_arg)
 *
 * 该函数调用传入的函数指针 function 并传递参数 func_arg。
 *
 * 该函数由 ret 线程栈中的函数指针进入，并且两个参数分别对应线程栈中 ebp + 4 和 ebp + 8 ———— 根据 ABI 调用者约定，被调用函数的栈帧中的栈顶会被认为是调用者函数的返回地址
 *
 * @param function 函数指针，kernel_thread 中调用的函数（ebp + 4）
 * @param func_arg 传递给函数 function 的参数（ebp + 8）
 */
static void kernel_thread(thread_func *function, void *func_arg)
{
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
    pthread->status = TASK_RUNNING;      // 设置线程状态为运行（以后再按照正常的逻辑为状态赋值）
    pthread->priority = prio;            // 设置线程优先级（将来它的作用体现任务（线程和进程的统称）在处理器上执行的时间片长度，即优先级越高，执行的时间片越长）
    /* self_kstack 是线程自己在内核态下使用的栈顶地址 */
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE); // 在线程创建之初，设置 0 特权级的内核栈指针（中断栈、线程栈）为 PCB 的最顶端
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
    asm volatile("movl %0, %%esp; \
                  pop %%ebp; \
                  pop %%ebx; \
                  pop %%esi; \
                  pop %%edi; \
                  ret"
                  : : "g"(thread->self_kstack) : "memory");

    return thread;  // 返回线程 PCB 的指针
}
