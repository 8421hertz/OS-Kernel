#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H
#include "global.h"
#include "stdint.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* elem2entry 和 offset 是为了将节点元素转换成实际元素项 */
// (int)(&((struct_type *)0)->member)为结构体成员 member 在结构体中的偏移量
#define offset(struct_type, member) (int)(&((struct_type *)0)->member)
/* 宏 elem2entry 的作用是将指针 elem_ptr 转换成 struct_type 类型的指针，其原理是用 elem_ptr 的地址减去 elem_ptr 在结构体 struct_type 中的偏移量，
   此地址便是结构体 struct_type 的起始地址，最后将此地址差转换为 struct_type 指针类型 */
// elem_ptr 是待转换的地址，它属于某个结构体中某个成员 member 的地址，参数 struct_member_name 是 elem_ptr 所在结构体中对应地址的成员名字，
// 也就是说参数 struct_member_name 是一个字符串，参数 struct_type 是 elem_ptr 所属结构体的类型。
/**
 * (1) 用结构体成员的地址减去成员在结构体中的偏移量，先获得到结构体的起始地址
 * (2) 再通过前置类型转换将第 1 步的地址转换成结构体类型
 */
#define elem2entry(struct_type, struct_member_name, elem_ptr) \
    (struct_type *)((int)elem_ptr - offset(struct_type, struct_member_name))

/********** 定义链表结点成员结构 ***********/
/* 结点中不需要数据成元, 只要求前驱和后继结点指针 */
struct list_elem
{
    struct list_elem *prev; // 前驱结点
    struct list_elem *next; // 后继结点
};

/* 链表结构, 用来实现队列 */
struct list
{
    // 队首和队尾是链表的两个固定入口，新插入的节点不会替换它们的位置（它们的值无意义，初始化时会被置为空）
    /* head 是队首, 是固定不变的, 不是第 1 个元素, 第 1 个元素为 head.next */
    struct list_elem head;
    /* tail 是队尾, 同样是固定不变的 */
    struct list_elem tail;
};

/* 自定义函数类型 function, 用于在 list_traversal 中做回调函数 */
typedef int(function)(struct list_elem *, int arg);

void list_init(struct list *);
void list_insert_before(struct list_elem *before, struct list_elem *elem);
void list_push(struct list *plist, struct list_elem *elem);
void list_iterate(struct list *plist);
void list_append(struct list *plist, struct list_elem *elem);
void list_remove(struct list_elem *pelem);
struct list_elem *list_pop(struct list *plist);
int list_empty(struct list *plist);
uint32_t list_len(struct list *plist);
struct list_elem *list_traversal(struct list *plist, function func, int arg);
int elem_find(struct list *plist, struct list_elem *obj_elem);

#endif