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


// 第一个进程
static proc_t proczero;

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
void proc_make_fisrt()
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

    // tf字段设置
    proczero.tf->epc=DATA_CODE_START;
    proczero.tf->sp=USTACK_BOTTOM;
    // 内核字段设置
    proczero.kstack=KSTACK(proczero.pid);
    proczero.ctx.sp=KSTACK(proczero.pid)+PGSIZE;
    proczero.ctx.ra=(uint64)trap_user_return;
    // 上下文切换
    cpu_t* cpu=mycpu();
    cpu->proc=&proczero;
    swtch(&(cpu->ctx),&(proczero.ctx));
}