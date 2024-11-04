# 设计任务

在这次实验中，我们希望在整个OS当中引入进程这个概念，我们首先引入第一个用户进程，初始化它，一切就绪以后完成cpu的上下文切换

# 实验内容

## 内核页表添加映射

```c
// 完成 UART CLINT PLIC 内核代码区 内核数据区 可分配区域 trampoline 内核栈的映射
// 相当于填充kernel_pgtbl
void kvm_init()
{
    kernel_pgtbl=pmem_alloc(KERNEL);
    memset(kernel_pgtbl,0,PGSIZE);
    vm_mappages(kernel_pgtbl,UART_BASE,UART_BASE,PGSIZE,PTE_R|PTE_W);   vm_mappages(kernel_pgtbl,PLIC_BASE,PLIC_BASE,0x400000,PTE_R|PTE_W);   vm_mappages(kernel_pgtbl,CLINT_BASE,CLINT_BASE,0x10000,PTE_R|PTE_W);
    vm_mappages(kernel_pgtbl,KERNEL_BASE,KERNEL_BASE,(uint64)KERNEL_DATA-KERNEL_BASE,PTE_R|PTE_X);
    //这段区域放的是内核代码，所以要给它一个x位的权限，允许执行，但是不给w权限，防止内核代码被修改
    vm_mappages(kernel_pgtbl,(uint64)KERNEL_DATA,(uint64)KERNEL_DATA,(uint64)ALLOC_BEGIN-(uint64)KERNEL_DATA,PTE_R|PTE_W);
    //这段区域放的是数据段和bss段
    vm_mappages(kernel_pgtbl,(uint64)ALLOC_BEGIN,(uint64)ALLOC_BEGIN,(uint64)ALLOC_END-(uint64)ALLOC_BEGIN,PTE_R|PTE_W);
    //trampoline的物理页在内核初始化的时候就分配好
    vm_mappages(kernel_pgtbl,TRAMPOLINE,(uint64)trampoline,PGSIZE,PTE_R|PTE_X);
    //映射内核栈
    kp_map_stacks();
}
```

### trampoline映射

因为我们引入了用户进程，所以我们需要映射trampoline这个物理页在内核页表上
trampoline里面包含了用户态和内核态切换时需要的代码和数据结构，所以当我们需要引入用户进程的时候，就必须在内核页表中添加trampoline的映射了，它被放在了页表地址的最高处，大小位一个PGSIZE

### 内核栈映射

我们的每个进程在内核页表中都有一个部分是内核栈，它们需要在内核页表初始化的时候就映射好，然后后续再将其页表中的地址放到进程的内核栈地址存储变量里
但是在我们当前的 `kvm_init()`函数里面只需要完成映射内核栈即可

## 初始化第一个用户进程

```c
// 第一个进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t* proc_pgtbl=pmem_alloc(USER);
    vm_mappages(proc_pgtbl,TRAMPOLINE,(uint64)trampoline,PGSIZE,PTE_R|PTE_X);
    vm_mappages(proc_pgtbl,TRAPFRAME,trapframe,PTE_R|PTE_W);
}

void proc_make_fisrt()
{
    uint64 page;
    proczero.tf=(trapframe_t*)pmem_alloc();
    // pid 设置
    proczero.pid=0;

    // pagetable 初始化
    proczero.pgtbl=proc_pgtbl_init(proczero.tf);

    // ustack 映射 + 设置 ustack_pages 
    vm_mappages(proczero,USTACK_BOTTOM,(uint64)pmem_alloc(USER),RTE_R|RTE_W);
    proczero.ustack_pages=1;

    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    uint64* addr;
    vm_mappages(proczero,DATA_CODE_START,addr=(uint64)pmem_alloc(USER),RTE_U|RTE_R|RTE_W|RTE_X);
    memmove((void*)addr,initcode,initcode_len);

    // 设置 heap_top
    // 此时堆为空的，所以堆底就是堆顶
    proczero.heap_top=HEAP_BOTTOM;

    // tf字段设置
    proczero.tf->epc=DATA_CODE_START;
    proczero.tf->sp=USTACK_BOTTOM;
    // 内核字段设置
    proczero.kstack=KSTACK(proczero.pid);
    proczero.ctx.sp=KSTACK(proczero.pid)+PGSIZE;
    proczero.ctx.ra=(uint64)trap_user_return;
    // 上下文切换
    cpu_t* cpu=mycpu();
    cpu->proc=proczero;
    swtch(&cpu->ctx,&proczero.ctx);
}
```

### 用户页表的地址安排

![1730711009590](image/README/1730711009590.png)

我们特别关注用户栈和堆区
栈底和堆底都是保持不动的，属于起始位置，栈底在高地址，随着放入内容堆顶向低地址减少，但是这里我们给内核栈只分配了一页，堆区则是堆底在低地址，随着内容增加，堆顶向高地址增加

### 映射

我们要在初始化中完成代码和数据段的映射，trampoline和trapframe的映射，用户栈的映射
在此要特别关注每个映射的权限

#### 权限

- code+data：X，R，W，U（因为这个区域有代码需要执行，有数据需要读写）
- ustack：R，W
- TRAPFRAME：R，W
- trampoline：R，X（管理内核态和用户态切换代码，可读不能修改需要可执行）

### 初始化字段和寄存器

#### context上下文

context是不同进程在内核态切换时的暂存区

```c
    proczero.ctx.sp=KSTACK(proczero.pid)+PGSIZE;
    proczero.ctx.ra=(uint64)trap_user_return;
```

sp寄存器放内核栈的起始地址
ra寄存器放陷入程序的地址

#### trapframe

trapframe是内核态和用户态之间切换时的暂存区

```c
    proczero.tf->epc=DATA_CODE_START;
    proczero.tf->sp=USTACK_BOTTOM;
```

epc放数据段和代码段的地址
sp放用户栈的栈底地址

### 上下文切换

```c
    cpu_t* cpu=mycpu();
    cpu->proc=proczero;
    swtch(&cpu->ctx,&proczero.ctx);
```

让cpu持有别的进程，同时调用swtch汇编函数实现上下文寄存器切换
