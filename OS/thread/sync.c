#include "sync.h"
#include "interrupt.h"
#include "debug.h"

/**
 * @brief 初始化信号量
 *
 * 初始化信号量 psema，设置初始值 value，并初始化信号量的等待队列（用于存储申请锁失败导致阻塞的线程 tag）。
 * 信号量用于管理资源的可用性，当值为 0 时，线程将被阻塞等待。
 *
 * 锁是用信号量实现的，因此锁的初始化会调用 sema_init
 *
 * @param psema 指向待初始化的信号量指针
 * @param value 信号量的初值
 */
void sema_init(struct semaphore *psema, uint8_t value)
{
    psema->value = value;       // 为信号量赋初值
    list_init(&psema->waiters); // 初始化信号量的等待队列
}

/**
 * @brief 初始化锁
 *
 * 锁的初始化 plock，设置锁的持有者 holder 为空，设置重复申请次数为 0，并调用 sema_init 来初始化锁的信号量为 1（这样锁中的信号量就成为了二元信号量）。
 *
 * @param plock 待初始化的锁指针
 */
void lock_init(struct lock *plock)
{
    plock->holder = NULL;            // 初始化为没有持有者
    plock->holder_repeat_nr = 0;     // 持有者重复申请次数为 0
    sema_init(&plock->semaphore, 1); // 信号量初始值为 1（锁中的信号量就成了二元信号量）
}

/**
 * @brief 在信号量 psema 上执行一个 down 操作
 *
 * 信号量的 down 操作，用于获取资源。
 *    当信号量的值为 0 时，当前线程将阻塞，并加入等待队列。
 *    当信号量的值为 1 时，当前线程可以继续运行并获取资源。
 *
 * @param psema 待执行 down 操作的信号量
 */
void sema_down(struct semaphore *psema)
{
    /* 关闭中断来保证 down 操作的原子操作 */
    enum intr_status old_status = intr_disable();

    // 用 while 而不用 if 的原因见语雀
    while (psema->value == 0) // 若信号量的 value 为 0， 表示锁已被别人持有
    {
        // 既然当前线程已处于活动中，也就是状态为 TASK_RUNNING，当前线程就不会出现在此信号量的等待队列中，否则重复添加的话会破坏队列
        ASSERT(elem_find(&psema->waiters, &running_thread()->general_tag));
        /* 当前正在运行的线程不应该在信号量的 waiters 队列中 */
        if (elem_find(&psema->waiters, &running_thread()->general_tag))
        {
            PANIC("sema_down: thread blocked has been in waiters_list\n");
        }
        /* ① 若信号量的值等于 0，则当前线程把自己加入该锁的等待队列，然后阻塞自己 */
        list_append(&psema->waiters, &running_thread()->general_tag);
        /* ② 将自己阻塞，状态为 TASK_BLOCKED */
        thread_block(TASK_BLOCKED); // 阻塞线程，直到被唤醒（执行 thread_block 后线程会被阻塞，然后切换到另一个线程 CPU 上）
                                    // （被唤醒后的所有线程共同竞争锁（但在本内核实现中，被唤醒的线程会被 push 到队头，享受优先调度，所以这里使用 if 也是可以的，只不过 while 通用性更好））
    }

    /* 若 value 为 1 或被唤醒，会执行下面的代码，也就是获得了锁 */
    psema->value--;
    ASSERT(psema->value == 0); // 确保信号量值为 0，防止出错

    intr_set_status(old_status); // 恢复之前的中断状态
}

/**
 * @brief 信号量的 up 操作（将信号量的值加 1）
 *
 * 增加信号量的值，并将等待队列中的一个线程唤醒（如果有）。
 * 信号量的值增加表示释放资源，使其他线程可以申请资源。
 *
 * @param psema 待执行 up 操作的信号量指针
 */
void sema_up(struct semaphore *psema)
{
    /* 关中断，保证原子操作 */
    enum intr_status old_status = intr_disable();
    ASSERT(psema->value == 0);        // 保证信号量的初值为 0，即有线程阻塞在此信号量上
    if (!list_empty(&psema->waiters)) // 如果等待队列不为空
    {
        // sema_up 使信号量加 1，这表示有信号资源可用了，也就是其他线程可以申请锁了，因此在信号的等待队列 psema->waiters 中
        // 通过 list_pop 弹出队首的第一个线程，并通过宏 elem2entry 将其转换成 PCB，存储到 thread_blokced 中，唤醒线程
        struct task_struct *thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters)); // 弹出等待队列的第一个线程 tag，并转换成 PCB
        thread_unblock(thread_blocked);                                                                              // 唤醒线程
                                                                                                                     // （所谓的唤醒线程并不是指马上就运行，而是重新加入到就绪队列，将来可以参与调度，运行是将来的事，而且当前是在关中断的情况下，
                                                                                                                     //  所以调度器并不会被触发。因此不用担心线程已经加到就绪队列，但 value 的值还没变成 1 会导致错误）
    }
    psema->value++;            // 信号量值加 1
    ASSERT(psema->value == 1); // 检查信号量值是否正确

    intr_set_status(old_status); // 恢复之前的中断状态
}

/**
 * @brief 获取锁 plock
 *
 * 获取锁的操作，如果锁已经被当前线程持有，则增加锁的重复申请计数。
 * 		有时候，线程可能会嵌套申请同一把锁，这种情况下再申请锁，就会形成死锁，即自己在等待自己释放锁
 *
 * 如果锁未被持有，通过对信号量的 down 操作获取锁，并将当前线程设为锁的持有者。
 *
 * @param plock 索要获得的锁指针
 */
void lock_acquire(struct lock *plock)
{
    /* 排除自己已持有锁单还未释放的情况 */
    if (plock->holder != running_thread()) // 如果当前线程不是锁的持有者
    {
        sema_down(&plock->semaphore);         // 执行 P 操作（将锁的信号量减 1），获取锁（获取锁的过程中可能会阻塞，不过早晚会成功返回的）
        plock->holder = running_thread();     // 将锁的持有者设为当前线程
        ASSERT(plock->holder_repeat_nr == 0); // 确保锁的重复计数为 0
        plock->holder_repeat_nr = 1;          // 第一次申请锁
    }
    else
    {
        plock->holder_repeat_nr++; // 如果当前线程已经持有锁，增加重复申请次数
    }
}

/**
 * @brief 释放锁
 *
 * 释放当前线程持有的锁，
 *		如果锁的重复申请次数大于 1(当前线程重复申请锁)，则减少次数，不真正释放锁。
 *		否则，释放锁并唤醒在信号量上阻塞的线程。
 *
 * @param plock 待释放的锁指针
 */
void lock_release(struct lock *plock)
{
    ASSERT(plock->holder == running_thread()); // 确保当前线程是锁的持有者
    if (plock->holder_repeat_nr > 1)           // 如果锁被重复申请了多次
    {
        plock->holder_repeat_nr--; // 减少重复申请次数
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1); // 确保锁的重复申请次数为 1

    /**
     * 注意：plock->holder = NULL 应在 sema_up(&plock->semaphore) 前
     * 原因：释放锁的操作并不在关中断下进行，有可能被调度器换下处理器。
     *			若 sema_up 操作在前的话，sema_up 会先把 value 置为 1，若老线程刚执行完 sema_up，还未执行 plock->holder=NULL 便被换下处理器，
     *			新调度上来的进程可能也申请了这个锁，value 为 1，因此申请成功，锁的持有者 plock->holder 将变成这个新进程的 PCB
     *			假如这个新线程还未释放锁又被换下了处理器，老线程又被调度上来执行，它会继续执行 plock->holder = NULL，将持有者 holder 置空，这就乱了
     */
    plock->holder = NULL;           // 将锁的持有者置空，表示锁已被释放
    plock->holder_repeat_nr = 0;    // 重置重复申请计数
    sema_up(&plock->semaphore);     // 执行信号量的 V 操作（信号量加 1），唤醒阻塞线程
}
