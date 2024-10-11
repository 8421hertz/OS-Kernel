#include "debug.h"
#include "print.h"
#include "interrupt.h"

/**
 * @brief 打印错误信息并使程序停止运行
 *
 * 该函数用于在发生严重错误时，打印出相关的调试信息（包括文件名、行号、函数名和错误条件），
 * 然后使程序进入死循环暂停执行。主要用于内核在不可恢复的错误情况下，帮助开发人员快速定位问题。
 *
 * @param char* filename 错误发生的文件名。
 * @param int line 错误发生的代码行号。
 * @param const char* func 错误发生的函数名称。
 * @param const char* condition 导致错误的条件（通常为断言失败的表达式）。
 *
 * @return void 该函数没有返回值，因为它会让程序进入死循环，停止进一步执行。
 */
void panic_spin(char *filename, int line, const char* func, const char* condition)
{
    // 关闭中断
    intr_disable();

    put_str("\n\n\n!!! error !!!!!\n");

    //逐行打印出错误发生的文件名
    put_str("filename: ");
    put_str(filename);
    put_str("\n");

    put_str("line: 0x");
    put_int(line);
    put_str("\n");

    put_str("function: ");
    put_str((char*)func);
    put_str("\n");

    put_str("condition: ");
    put_str((char*) condition);
    put_str("\n");

    while(1)
        ;
}
