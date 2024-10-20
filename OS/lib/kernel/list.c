#include "list.h"
#include "interrupt.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * @brief 初始化双向链表 list
 *
 * 将链表的头结点和尾结点进行初始化，形成一个空链表，头结点指向尾结点，尾结点指向头结点。
 *
 * @param list 指向需要初始化的链表的指针
 * @return void 该函数没有返回值。
 */
void list_init(struct list *list)
{
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

/**
 * @brief 把链表元素 elem 插入在元素 before 之前
 *
 * 将元素 elem 插入到链表中的 before 元素之前，完成插入的双向链表操作。
 * 先关闭中断，保证原子性操作，然后更新 elem 和 before 的前驱后继关系。
 *
 * @param before 指向需要插入位置前的链表元素
 * @param elem 要插入的链表元素
 * @return void 该函数没有返回值。
 */
void list_insert_before(struct list_elem *before, struct list_elem *elem)
{
    // 由于队列是公共资源，对于它的修改一定要保证为原子操作，所以需要关闭中断
    enum intr_status old_status = intr_disable(); // 旧中断状态用变量 old_status 保存

    elem->prev = before->prev; // 将 elem 的前驱指向 before 的前驱
    elem->next = before;       // 将 elem 的后继指向 before

    before->prev->next = elem; // 更新 before 的前驱结点的后继为 elem
    before->prev = elem;       // 更新 before 的前驱为 elem

    intr_set_status(old_status); // 恢复中断状态
}

/**
 * @brief 将元素添加到链表的队首
 *
 * 类似于栈的 push 操作，将元素 elem 插入到链表 plist 的队首。
 * 内部调用 list_insert_before 函数将元素插入到 head.next 之前。
 *
 * @param plist 指向链表的指针
 * @param elem 要插入的链表元素
 * @return void 该函数没有返回值。
 */
void list_push(struct list *plist, struct list_elem *elem)
{
    list_insert_before(plist->head.next, elem); // 在队头插入 elem
}

/**
 * @brief 将元素添加到链表的队尾
 *
 * 类似于队列的 FIFO 操作，将元素 elem 插入到链表 plist 的队尾。
 * 内部调用 list_insert_before 函数将元素插入到 tail 之前。
 *
 * @param plist 指向链表的指针
 * @param elem 要插入的链表元素
 * @return void 该函数没有返回值。
 */
void list_append(struct list *plist, struct list_elem *elem)
{
    list_insert_before(&plist->tail, elem); // 在队尾的前面插入 elem
}

/**
 * @brief 将链表元素从链表中删除
 *
 * 将元素 pelem 从链表中脱离。通过更新 pelem 前驱和后继的指针，完成元素的删除。
 * 先关闭中断，保证原子性操作，然后恢复中断。
 *
 * @param pelem 要从链表中移除的元素
 * @return void 该函数没有返回值。
 */
void list_remove(struct list_elem *pelem)
{
    enum intr_status old_status = intr_disable(); // 关闭中断

    pelem->prev->next = pelem->next; // 更新前驱的后继指针
    pelem->next->prev = pelem->prev; // 更新后继的前驱指针

    intr_set_status(old_status); // 恢复中断状态
}

/**
 * @brief 将链表第一个元素弹出并返回
 *
 * 类似于栈的 pop 操作，将链表 plist 的第一个元素弹出并返回。
 * 内部调用 list_remove 函数将元素从链表中删除。
 *
 * @param plist 指向链表的指针
 * @return struct list_elem* 返回弹出的元素指针
 */
struct list_elem *list_pop(struct list *plist)
{
    struct list_elem *elem = plist->head.next; // 获取链表第一个元素
    list_remove(elem);                         // 从链表中移除
    return elem;
}

/**
 * @brief 从链表中查找指定元素
 *
 * 遍历链表 plist，查找元素 obj_elem，找到时返回 true，否则返回 false。
 *
 * @param plist 指向链表的指针
 * @param obj_elem 要查找的元素指针
 * @return bool 找到返回 true，未找到返回 false。
 */
int elem_find(struct list *plist, struct list_elem *obj_elem)
{
    struct list_elem *elem = plist->head.next;
    while (elem != &plist->tail)
    {
        if (elem == obj_elem)
        {
            return 1;
        }
        elem = elem->next;
    }
    return 0;
}

/**
 * @brief 遍历链表并执行回调函数
 *
 * 遍历链表 plist，将每个元素 elem 和参数 arg 传递给回调函数 func。
 * 如果 func 返回 true，则返回该元素的指针，否则继续遍历，直到遍历结束。
 *
 * @param plist 指向链表的指针
 * @param func 回调函数
 * @param arg 传递给回调函数的参数
 * @return struct list_elem* 返回找到的元素指针，未找到返回 NULL。
 */
struct list_elem *list_traversal(struct list *plist, function func, int arg)
{
    struct list_elem *elem = plist->head.next;
    if (list_empty(plist))
    {
        return NULL;
    }
    while (elem != &plist->tail)
    {
        if (func(elem, arg))
        {
            return elem;
        }
        elem = elem->next;
    }
    return NULL;
}

/**
 * @brief 返回链表长度
 *
 * 计算并返回链表 plist 中元素的数量。
 *
 * @param plist 指向链表的指针
 * @return uint32_t 链表的长度
 */
uint32_t list_len(struct list *plist)
{
    struct list_elem *elem = plist->head.next;
    uint32_t length = 0;
    while (elem != &plist->tail)
    {
        length++;
        elem = elem->next;
    }
    return length;
}

/**
 * @brief 判断链表是否为空
 *
 * 检查链表 plist 是否为空，空时返回 true，否则返回 false。
 *
 * @param plist 指向链表的指针
 * @return bool 空返回 true，非空返回 false。
 */
int list_empty(struct list *plist)
{
    return (plist->head.next == &plist->tail ? 1 : 0);
}