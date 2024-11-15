#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();

// 进程数组
static proc_t procs[NPROC];

//注意稍后变指针
static proc_t proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;


// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    trap_user_return();
}

// 返回一个未使用的进程空间
// 设置pid + 设置上下文中的ra和sp
// 申请tf和pgtbl使用的物理页
proc_t* proc_alloc()
{

}

// 释放一个进程空间
// 释放pgtbl的整个地址空间
// 释放mmap_region到仓库
// 设置其余各个字段为合适初始值
// tips: 调用者需持有p->lk
void proc_free(proc_t* p)
{

}

// 进程模块初始化
void proc_init()
{

}


// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t proc_pgtbl=pmem_alloc(USER);
    vm_mappages(proc_pgtbl,TRAMPOLINE,(uint64)trampoline,PGSIZE,PTE_R|PTE_X);
    vm_mappages(proc_pgtbl,TRAPFRAME,trapframe,PGSIZE,PTE_R|PTE_W);
    return proc_pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_first()
{
    //uint64 page;
    proczero.tf=(trapframe_t*)pmem_alloc(USER);
    // pid 设置
    proczero.pid=0;

    // pagetable 初始化
    proczero.pgtbl=proc_pgtbl_init((uint64)proczero.tf);

    // ustack 映射 + 设置 ustack_pages 
    vm_mappages(proczero.pgtbl,USTACK_BOTTOM-PGSIZE,(uint64)pmem_alloc(USER),PGSIZE,PTE_R|PTE_W|PTE_U);
    proczero.ustack_pages=1;

    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    char* addr;
    addr=pmem_alloc(USER);
    vm_mappages(proczero.pgtbl,DATA_CODE_START,(uint64)addr,PGSIZE,PTE_U|PTE_R|PTE_W|PTE_X);
    memmove(addr,initcode,initcode_len);

    // 设置 heap_top
    // 此时堆为空的，所以堆底就是堆顶
    proczero.heap_top=HEAP_BOTTOM;

    // 设置 mmap_region_t
    proczero.mmap=mmap_region_alloc();
    proczero.mmap->begin=MMAP_BEGIN;
    proczero.mmap->npages=0;
    proczero.mmap->next=mmap_region_alloc();
    proczero.mmap->next->begin=MMAP_BEGIN;
    proczero.mmap->next->npages=(MMAP_END-MMAP_BEGIN)/PGSIZE;

    // tf字段设置
    proczero.tf->epc=DATA_CODE_START;
    proczero.tf->sp=USTACK_BOTTOM;
    // 内核字段设置
    proczero.kstack=KSTACK(proczero.pid);
    proczero.ctx.sp=KSTACK(proczero.pid)+PGSIZE;
    proczero.ctx.ra=(uint64)trap_user_return;
    
/*     //LAB5 TEST
    pgtbl_t new=proc_pgtbl_init((uint64)proczero.tf);
    uvm_copy_pgtbl(proczero.pgtbl,new,proczero.heap_top,proczero.ustack_pages,proczero.mmap);
    vm_print(new);
    printf("***\n");
    vm_print(proczero.pgtbl); */

    // 上下文切换
    cpu_t* cpu=mycpu();
    cpu->proc=&proczero;
    swtch(&(cpu->ctx),&(proczero.ctx));
}

// 进程复制
// UNUSED -> RUNNABLE
int proc_fork()
{

}

// 进程放弃CPU的控制权
// RUNNING -> RUNNABLE
void proc_yield()
{

}

// 等待一个子进程进入 ZOMBIE 状态
// 将退出的子进程的exit_state放入用户给的地址 addr
// 成功返回子进程pid，失败返回-1
int proc_wait(uint64 addr)
{

}

// 父进程退出，子进程认proczero做父，因为它永不退出
static void proc_reparent(proc_t* parent)
{

}

// 唤醒一个进程
static void proc_wakeup_one(proc_t* p)
{
    assert(spinlock_holding(&p->lk), "proc_wakeup_one: lock");
    if(p->state == SLEEPING && p->sleep_space == p) {
        p->state = RUNNABLE;
    }
}

// 进程退出
void proc_exit(int exit_state)
{

}

// 进程切换到调度器
// ps: 调用者保证持有当前进程的锁
void proc_sched()
{

}

// 调度器
void proc_scheduler()
{

}

// 进程睡眠在sleep_space
void proc_sleep(void* sleep_space, spinlock_t* lk)
{

}

// 唤醒所有在sleep_space沉睡的进程
void proc_wakeup(void* sleep_space)
{

}