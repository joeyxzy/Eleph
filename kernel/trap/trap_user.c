#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    //uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();

    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");
    //将此时用户态的pc记录在trapframe中
    p->tf->epc = sepc;
    if(scause==8)
    {
        printf("get a syscall from proc %d\n", myproc()->pid);
    }
    else
    {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    }
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    struct proc *p = myproc();

    //在修改trap_vector地址之前关闭中断
    intr_off();
    //放入uservec的虚拟地址，因为此时satp写入了内核页表
    w_stvec(TRAMPOLINE+(user_vector-trampoline));
    //在trapframe中保存值，便于下一次进程由用户空间切换到内核空间时使用
    //epc在之前就已经初始化好了
    p->tf->kernel_satp = r_satp();         
    p->tf->kernel_sp = p->kstack + PGSIZE; // 指向内核栈顶
    p->tf->kernel_trap = (uint64)user_vector;
    p->tf->kernel_hartid = r_tp();   

    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // sret后进入Umode
    x |= SSTATUS_SPIE; // 在Umode中允许中断
    w_sstatus(x);

    //sret指令执行的最直接效果是将程序计数器设置成SEPC寄存器的值
    //所以现在我们将SEPC寄存器的值设置成之前在trapframe中保存的用户程序计数器的值
    w_sepc(p->tf->epc);
    //写好satp这个参数，未来将作为参数传入user_return这段汇编里面，然后写入satp寄存器
    uint64 satp = MAKE_SATP(p->pgtbl);
    //fn是user_return的虚拟地址
    uint64 fn = TRAMPOLINE + (user_return - trampoline);
    //进入user_return汇编
    ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}