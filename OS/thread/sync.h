#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H
#include "list.h"
#include "stdint.h"
#include "thread.h"

/**
 * 信号量结构
 *
 * （信号量只是一个编程理念，是个程序设计结构，只要具备信号初值和等待线程这两个必要的元素就行，其实现形式无具体要求）
 */
struct semaphore
{
    uint8_t value;       // 信号量的值，表示当前可用资源的数量
                         // （对信号量执行 down 操作时，若信号量值为 0 就会阻塞线程）
    struct list waiters; // 等待队列，记录在此信号量上等待（阻塞）的线程
    /**
     * 当信号量值为 0 并执行 down 操作时，表示资源不可以用，线程将被阻塞并加入此等待队列
     * 当信号量大于 0 时，资源可用，线程可执行（等待队列中的线程可以竞争锁了），不许加入等待队列
     */
};

/* 锁结构 */
struct lock
{
    struct task_struct *holder; // 锁的持有者，记录当前是哪个线程持有此锁
                                // （谁成功申请了锁，就应该记录锁被谁持有）
    struct semaphore semaphore; // 锁通过二元信号量来实现，初值为 1
    uint32_t holder_repeat_nr;  // 锁的持有者重复申请锁的次数，释放锁的时候会参考此变量的值
    /** 
     * 原因是一般情况下应该在进入临界区之前加锁，但有时候可能持有了某临界区的锁后，在未释放锁之前，有可能会再次调用重复申请此锁的函数，
     * 这样会导致内外层函数在释放锁时会对同一个锁释放两次，
     *      （还有可能导致死锁，线程在等待自己已经持有的资源而陷入无限期的等待。
     *          例如，一个线程已经持有了一把锁，但由于程序的逻辑，它在没有释放这把锁之前，又尝试再次申请同样的锁，导致它自己在等待自己释放锁，这样就永远无法继续执行下去，形成死锁。）
     * 为了避免这种情况的发生，用此变量来累计重复申请的次数，释放锁时会根据变量 holder_repeat_nr 的值来执行具体动作。
     * 
     * holder_repeat_nr 用于记录同一线程多次申请同一把锁的次数，避免在递归或嵌套函数中，线程对锁进行多次释放，确保正确释放次数以避免逻辑错误。
     */
};

void sema_init(struct semaphore *psema, uint8_t value);
void lock_init(struct lock *plock);
void sema_down(struct semaphore *psema);
void sema_up(struct semaphore *psema);
void lock_acquire(struct lock *plock);
void lock_release(struct lock *plock);

#endif