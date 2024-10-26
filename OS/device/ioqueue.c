#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/**
 * @brief 初始化 io 队列 ioq
 *
 * @param ioq 指向 io 队列的指针
 * @return void
 *
 * 初始化锁，并将生产者和消费者置为 NULL，队头队尾置为 0。
 */
void ioqueue_init(struct ioqueue *ioq)
{
    lock_init(&ioq->lock);                // 初始化 io 队列的锁
    ioq->consumer = ioq->producer = NULL; // 生产者和消费者置空
    ioq->head = ioq->tail = 0;            // 队列的首尾指针指向缓冲区数组第 0 个位置
}

/**
 * @brief 返回 pos 在缓冲区中的下一个位置值
 *
 * @param pos 当前缓冲区的位置
 * @return 下一个缓冲区的位置值
 *
 * 将 pos+1 后对 bufsize 求模，保证缓冲区环形结构。
 */
static int32_t next_pos(int32_t pos)
{
    return (pos + 1) % bufsize; // 缓冲区是环形结构，超过 bufsize 后返回到 0（保证了缓冲区指针回绕着数组 buf，从而实现了环形缓冲区）
}

/**
 * @brief 判断队列是否已满
 *
 * @param ioq 指向 io 队列的指针
 * @return bool 返回 true 如果队列已满，否则返回 false
 *
 * 判断队列是否满的条件是队列头指针的下一个位置是否等于队尾指针。（从这可以看出缓冲区最大容量实际上是 63 字节）
 */
bool ioq_full(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);   // 保证中断已关闭
    return next_pos(ioq->head) == ioq->tail; // 判断是否队列已满
}

/**
 * @brief 判断队列是否为空
 *
 * @param ioq 指向 io 队列的指针
 * @return bool 返回 true 如果队列为空，否则返回 false
 *
 * 判断队列是否为空的条件是队列头指针与队尾指针相等。
 */
static bool ioq_empty(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF); // 保证中断已关闭
    return ioq->head == ioq->tail;         // 判断是否队列为空
}

/**
 * @brief 使当前生产者或消费者函数线程在此缓冲区上睡眠（等待），并在缓冲区中等待
 *
 * @param waiter 二级指针（传给它的实参将是线程指针的地址），指向需要等待的线程 ———— 缓冲区中的生产者或消费者线程
 * @return void
 *
 * 记录当前线程并使其进入阻塞状态，等待其他线程操作。
 */
void ioq_wait(struct task_struct **waiter)
{
    ASSERT(*waiter == NULL && waiter != NULL); // 保证 waiter 为空且有效
    *waiter = running_thread();                // 将当前线程记录在 waiter 指向的指针中（也就是缓冲区的 producer 或 consumer）
                                               // 因此 *waiter 相当于 ioq->consumer 或 ioq->producer
    thread_block(TASK_BLOCKED);                // 阻塞当前线程
}

/**
 * @brief 唤醒等待的生产者或消费者
 *
 * @param waiter 二级指针，指向需要被唤醒的线程（缓冲区中的成员 producer 或 consumer）
 * @return void
 *
 * 唤醒并清除该等待线程的记录。
 */
static void wakeup(struct task_struct **waiter)
{
    ASSERT(*waiter != NULL); // 确保存在等待的线程
    thread_unblock(*waiter); // 唤醒线程（生产者或消费者）
    *waiter = NULL;          // 清空 waiter
}

/**
 * @brief 从 io 队列中获取一个字节（我们对缓冲区操作的数据单位大小是 1 字节）———— 由消费者线程调用
 *
 * @param ioq 指向 io 队列的指针
 * @return 从 ioq 缓冲区的队尾处返回一个字节（属于从缓冲区中取数据）
 *
 * 若循环队列为空（没有数据可取），消费者需要先在此缓冲区上睡眠，直到有生产者将数据添加到此缓冲区后再被叫醒重新取数据
 *		但是消费者有多个，它们之间是竞争关系，醒来后有可能别的消费者刚刚把缓冲区中的数据取走了，因此在当前消费者被叫醒后还要再判断缓冲区是否为空比较保险，所以用 while 重复判断
 */
char ioq_getchar(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF); // 保证中断已关闭

    // 如果队列为空，消费者等待
    /* 若缓冲区（队列）为空，把消费者 ioq->consumer 记为当前线程自己，目的是将来生产者往缓冲区里装数据后，生产者知道唤醒哪个消费者，也就是唤醒当前线程自己 */
    while (ioq_empty(ioq))
    {
        lock_acquire(&ioq->lock); // 申请环形缓冲区队列的锁
        ioq_wait(&ioq->consumer); // 持有锁后，将消费者（当前线程）登记在 ioq->consumer 中（将自己阻塞，也就是在此缓冲区上休眠）
                                  // &ioq->consumer 用来记录哪个消费者没有拿到数据而休眠阻塞（这样等将来某个生产者往缓冲区中添加数据的时候就知道叫醒它继续拿数据了）
        lock_release(&ioq->lock); // 醒来后释放队列锁
    }

    // 缓冲区不为空就会来到这
    char byte = ioq->buf[ioq->tail]; // 从队列缓冲区尾部取出 1 字节数据
    ioq->tail = next_pos(ioq->tail); // 更新队尾指针到下一个位置

    // 若之前此缓冲区是满的，正好有生产者来添加数据，那个生产者一定会在此缓冲区上睡眠。如今缓冲区已经被当前消费者腾出一个数据单位的位置了，此时应该叫醒生产者继续往缓冲区中添加数据。
    // 如果有生产者在等待，唤醒生产者继续往缓冲区中添加数据
    if (ioq->producer != NULL)
    {
        wakeup(&ioq->producer); // 唤醒因队满添加数据而睡眠阻塞的生产者线程
    }

    return byte; // 返回取出的字节
}

/**
 * @brief 生产者向 io 队列中写入一个字节
 *
 * @param ioq 指向 io 队列的指针
 * @param byte 要写入缓冲区的字节数据
 * @return void
 *
 * 如果队列已满，生产者进入等待状态，直到有空间可写入数据。
 */
void ioq_putchar(struct ioqueue *ioq, char byte)
{
    ASSERT(intr_get_status() == INTR_OFF); // 保证中断已关闭

    // 如果队列已满，生产者等待
    while (ioq_full(ioq))
    {
        lock_acquire(&ioq->lock); // 获取队列锁
        ioq_wait(&ioq->producer); // 将生产者登记在 ioq->producer 中（将来消费者取数据时将其唤醒）
        lock_release(&ioq->lock); // 释放队列锁
    }

    ioq->buf[ioq->head] = byte;      // 将字节写入队列缓冲区头部
    ioq->head = next_pos(ioq->head); // 更新队列头指针

    // 判断是否有消费者在此缓冲区上休眠。若之前此缓冲区为空，恰好有消费者来取数据，因此会导致消费者休眠。现在当前生产者线程已经往缓冲区中添加了数据，现在可以将消费者唤醒让它继续取数据了。
    // ioq->consumer != NULL，说明之前已经有消费者线程因为缓冲区空而休眠，被登记在 ioq->consumer，因此需要唤醒消费者。
    if (ioq->consumer != NULL)
    {
        wakeup(&ioq->consumer); // 唤醒消费者线程
    }
}
