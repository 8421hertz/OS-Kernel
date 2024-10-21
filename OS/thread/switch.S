[bits 32]
section .text
;switch_to 用于任务切换
global switch_to    ; 将 switch_to 导出为全局符号
switch_to:
    ; 栈中此处是返回地址（遵循 ABI 原则，被调函数需要保存盗用函数的 5 个寄存器）
    ; 由于栈是高地址->低地址扩展的，因此这 4 个 push 操作步骤与线程栈 struct thread_stack 的结构是逆序的
    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp + 20]         ; 得到栈中的参数 cur 指针（PCB 指针），cur = [esp+20]
    mov [eax], esp              ; 将当前内核栈指针 esp（task_struct 的 self_kstart 栈指针地址）保存到当前线程 cur(PCB) 指针中 ———— PCB 的前 4 个字节就是 self_kstack 内核栈指针的地址

    ; --------------- 以上是备份当前线程的环境，下面是恢复下一个线程的环境（实现线程内核栈的切换） ---------------

    mov eax, [esp + 24]         ; 得到栈中的被调度线程的 PCB 指针 next 参数
    ; （被调度 next 线程的内核栈）将 next 线程 PCB 指针（内核指针 self_kstack）恢复到 esp 中，从而恢复之前保存过的寄存器映像
    mov esp, [eax]              ; 将被调度线程 next 的内核栈指针 self_kstcak（PCB 的前 4 个字节）传给 esp
                                ; （self_kstack 用来记录 0 级栈顶指针，被换上 cpu 时用来恢复 0 级栈）
                                ; （0 级内核栈中保存了进程或线程的所有信息，包括 3 级栈指针）

    ; 这些被恢复的寄存器映像是在之前某次执行 switch_to 时，由现在的这个 next 线程作为那时候的当前线程 cur 被换下处理器前保存的，也是用前 15 行代码保存的，只是它们分属于不同的 switch_to 调用次序
    pop ebp
    pop ebx
    pop edi
    pop esi

    ; 当执行 ret 指令时，返回地址会从栈中弹出并加载到 eip（指令指针寄存器）中，而此时内核栈已经是下一个线程的内核栈
    ; 因此，ret 返回的并不是当前线程的执行点，而是上某一次某线程（如今的 next 被调度线程）执行 switch_to 函数时传入的返回地址，从而使 next 被调度线程的代码恢复执行
    ret                         ; ① 由中断进入，返回到 schedule 继续执行（详见语雀）
                                ; ② 未由中断进入，第一次执行时会返回到 kernel_thread（详见语雀）





