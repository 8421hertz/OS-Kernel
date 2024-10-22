#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H

#include "stdint.h"
#include "list.h"

/* 自定义通用函数类型，它将在很多线程函数中作为形参类型 */
// thread_func：这是新的类型别名，用来代指一个函数类型。它是用户定义的名称，表示任意符合这个签名的函数的类型。
// (void *)：表示该函数接受一个参数，并且这个参数是 void* 类型的指针。void* 是一种通用指针类型，可以指向任何类型的数据。
typedef void thread_func(void *); // 定义返回值为 void 的函数类型
                                  // 用来指定线程中运行的函数类型
                                  // （我们在线程中打算运行某段代码（函数）时，需要一个参数来接收该函数的地址，因此这里先定义这个返回值 void 的函数类型）

/* 进程或线程的状态 */
enum task_status // 进程和线程的区别是它们是否独自拥有地址空间（页表）
{
    TASK_RUNNING, // 进程正在运行或准备运行
    TASK_READY,   // 进程处于就绪状态，已准备好执行，但未被分配CPU
    TASK_BLOCKED, // 进程因某些条件未满足而被阻塞，无法继续执行
    TASK_WAITING, // 进程处于等待状态，可能等待某个事件或资源
    TASK_HANGING, // 进程被挂起，暂时停止运行，但没有终止
    TASK_DIED     // 进程已终止或结束，不再执行
};

/************ 程序的中断栈 intr_stack（位于线程自己的内核栈中） ************
 * 此结构是用于中断发生时保护程序（线程或进程）的上下文环境的栈内容: ———— 在 kernel.S 中的中断入口程序 "intr%lentry" 所执行的上下文保护的一系列压栈操作都是压入了此结构中
 * 进程或线程被外部中断或软中断打断时，中断处理入口程序会按照此结构压入上下文寄存器，intr_exit 中的出栈操作是此结构的逆操作
 * 初始情况下此栈在线程自己的内核栈中位置固定，在 PCB 所在页的最顶端；但每次进入中断时就不一定了
 *		如果进入中断时不涉及到特权级变化，它的位置就会在当前的 esp 之下，
 *		否则处理器会从 TSS 中获得新的 esp 的值，然后在新的 esp 之下
 * ① 将来线程进入中断后，位于 kernel.S 中的中断入口程序的代码会通过此栈来保存上下文。
 * ② 将来实现用户进程时，会将用户进程的初始信息放在中断栈中。
 *******************************************/
struct intr_stack
{
    uint32_t vec_no; // kernel.S 宏 VECTOR 中 push %1 压入的中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // 虽然 pushad 把 esp 也压入，但 esp 是不断变化的，所以会被 popad 忽略
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    /* 以下由 cpu 从低特权级进入高特权级时压入 */
    uint32_t err_code; // err_code 中断错误码会被压入在 eip 之后
    void (*eip)(void); // 这里声明了一个函数指针（eip 是一个指向函数的指针，该函数没有参数，并且返回类型为 void）
    uint32_t cs;
    uint32_t eflags;
    void *esp;
    uint32_t ss;
};

/*********************************** 线程栈 thread_stack（位于线程自己的内核栈中） **************************************
 * 线程自己的栈，用于存储线程中待执行的函数。
 * 此结构在线程自己的内核栈中位置不固定，仅用在执行 switch_to 时保存线程环境。实际位置取决于实际运行情况。
 * 线程栈的两个作用：
 *  ① 在线程首次运行时，线程栈用于存储创建线程所需的相关数据；线程栈用于保存待运行的函数，其中 eip 便是该函数的地址。
 *  ② 用在任务切换函数 switch_to 任务切换函数中的，这是线程已经处于正常运行后线程栈所体现的作用；当任务切换时，此 eip 用于保存执行 switch_to 任务切换函数后返回的的新任务地址。
 */
struct thread_stack
{
    /**
     * 下面的 4 个寄存器（ABI 中是 5 个）归主调函数所用，被调函数需要保护好这 4 个函数的值，在被调函数运行完后，这 5 个寄存器的值必须和运行前一样，被调函数必须在自己的栈中存储这些寄存器的值
     *      esp 的值会由调用约定来保证，因此这里我们不打算保护 esp 的值
     *  除了这 5 个寄存器外（包括 esp）的寄存器为被调用函数所用
     */
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    // 线程是使函数单独上处理器运行的机制，因此线程肯定得知道要运行在哪个函数上，首次执行某个函数时，这个栈就用来保存待运行的函数，其中 eip 便是该函数的地址
    // 将来用 switch_to 函数实现任务切换，当任务切换时，此 eip 用于保存任务切换后的返回的新任务的地址
    /* 线程第一次执行时，eip 指向待调用的函数 kernel_thread，其他时候 eip 指向 switch_to 返回的新任务的地址 */
    void (*eip)(thread_func *func, void *func_arg);

    /********** 以下仅供第一次被调度上 cpu 时使用 ***********/

    /* 参数 unused_retaddr 只是个占位符，是指向函数的指针，充当函数返回地址（仅仅起占位的作用） */
    void (*unused_retaddr)(void); // 在返回地址所在的栈帧中占个位置（当进入函数时，ebp 会自动跳过返回地址）
    thread_func *function;        // 由 kernel_thread 所调用的函数名（是在线程中执行的函数） ———— 它知道自己需要什么类型的函数，因此在 function 函数体中可以自己转换成想要的类型
    void *func_arg;               // 由 kernel_thread 所调用的函数（function）所需要的参数 ———— function(func_arg)
};

/***** 进程或线程的 pcb, 进程控制块 *****/
struct task_struct
{
    // 中断栈 intr_stack 和线程栈 thread_stack 都位于线程 PCB 中的内核栈中，也就是位于 PCB 的高地址处
    uint32_t *self_kstack;   // 各线程的内核栈顶指针，当线程被创建时，self_kstack 被初始化为自己的 PCB 所在的页的顶端
                             // 之后再运行时，在被换下处理器之前，会把线程的上下文信息（寄存器映像）保存在 0 特权级栈中
                             // self_kstack 便用来记录 0 特权级栈在保存线程上下文后的新栈顶，在下一次此线程又被调度到处理器上时，可以把 self_kstack 的值加载到处理器中运行
    enum task_status status; // 线程状态
    char name[16];           // 记录任务（线程/进程）的名字
    uint8_t priority;        // 线程优先级（用于决定进程/线程的时间片，及被调度到处理器上后的运行时间）

    /*
     * ticks 和上面的 priority 要配合使用，priority 任务优先级体现在任务执行的时间片上，优先级越高，每次任务被调度上处理器后执行的时间片就越长
     * ticks 为每次被调度到处理器上执行的时间滴答数(任务时间片)，每次时钟中断都会将当前任务的 ticks 减 1，当减到 0 时就被换下处理器，
     * 调度器把 priority 重新赋值给 ticks，这样当此线程下一次又被调度时，将再次在处理器上运行 ticks 个时间片
     */
    uint8_t ticks;

    uint32_t elapsed_ticks; // 记录任务在处理器上运行的 ticks 时钟滴答数，从开始执行，到运行结束所经历的总时钟数

    /******** 以下两个标签仅仅是加入队列时用的，将来从队列中把它们取出来时，还需要再通过 offset 宏与 elem2entry 宏的 "反操作"，实现从 &general_tag 到 &thread 的地址转换，将它们还原成线程的 PCB 地址后才能使用（这两个线程 "标签" 定义在 thread.c 中） ********

    /*
     * ① 就绪队列中的"标签"节点
     * 它是线程的"标签"，当线程被加入到就绪队列 thread_ready_list 或其他等待队列中时，就把该线程 PCB 中 general_tag 的地址加入队列
     * general_tag 的作用是用于线程在一般的队列中的结点
     *		一个 struct list_elem 类型的节点只有一对前驱和后继指针，它只能被加入到一个队列
    */
    struct list_elem general_tag;

    /**
     * ② 全部队列中的"标签"节点
     * 在咱们的系统中，为管理所有线程，还存在一个全部线程队列 thread_all_list，因此线程还需要另外一个"标签"，即 all_list_tag
     * all_list_tag 的作用是用于线程队列 thread_all_list 中的结点
     * 专门用于线程被加入全部线程队列 thread_all_list 时使用
     */
    struct list_elem all_list_tag;

    // 线程与进程最大的区别是进程独享自己的地址空间（有自己的页表），而线程共享所有在进程的地址空间，即线程无页表
    uint32_t *pgdir; // 进程自己页表的虚拟地址（如果该任务为线程，则 pgdir 为 NULL）
                     // 页表加载时还是要被转换成物理地址的

    uint32_t stack_magic; // 栈的边界标记，用于检测栈的溢出（由于 0 级栈和 PCB 是在同一页，栈位于页的顶端并向下扩展，因此担心压栈过程中会把 PCB 中的信息给覆盖，所以
                          // 所以每次在线程或进程调度时要判断是否触及到了进程信息的边界，也就是判断 stack_magic 的值是否为初始化的内容）
                          // stack_magic 是一个魔数
};

struct task_struct *running_thread();
struct task_struct *thread_start(char *name,
                                 int prio,
                                 thread_func function,
                                 void *func_arg);
void init_thread(struct task_struct *pthread, char *name, int prio);
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg);
static void kernel_thread(thread_func *function, void *func_arg);
void schedule();
static void make_main_thread(void);
void thread_init(void);

#endif