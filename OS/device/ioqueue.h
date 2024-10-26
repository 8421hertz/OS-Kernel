#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64 // 唤醒缓冲区的大小为 64B

typedef int bool;
#define true 1
#define false 0

/* 环形缓冲区队列 */
struct ioqueue
{
    // 生产者消费者问题
    struct lock lock; // 本缓冲区的锁，每次对缓冲区操作时都要先申请这个锁，从而保证缓冲区操作互斥
    /* 生产者（缓冲区不满时就继续往里面放数据，否则就休眠，此项记录哪个生产者在此缓冲区上睡眠） */
    struct task_struct *producer;   
    /* 消费者（缓冲区不空时就继续从里面拿数据，否则就休眠，此项记录哪个消费者在此缓冲区上睡眠） */
    struct task_struct *consumer;
    char buf[bufsize];  // 缓冲区的大小（数组）
    int32_t head;       // 队首（数据往队首处写入）
    int32_t tail;       // 队尾（数据从队尾处读出）

    // ...
};

void ioqueue_init(struct ioqueue *ioq);
static int32_t next_pos(int32_t pos);
bool ioq_full(struct ioqueue *ioq);
static bool ioq_empty(struct ioqueue *ioq);
void ioq_wait(struct task_struct **waiter);
static void wakeup(struct task_struct **waiter);
char ioq_getchar(struct ioqueue *ioq);
void ioq_putchar(struct ioqueue *ioq, char byte);

#endif