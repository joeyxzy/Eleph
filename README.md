
# 用户态和内核态的数据迁移

## 系统调用流程

首先在用户态中我们通过调用syscall函数，这个syscall函数是Umode下的函数(defined in user/sys.h)

```c
#define __syscall1(n, a) __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3.......
                    .......
                  
#define __SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __SYSCALL_NARGS(...) __SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...)                        \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__)) \
    (__VA_ARGS__)

#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)
```

这个syscall的工作是，应对不同参数量的调用，然后调用不同的syscallx函数

```c
//syscall1为处理一个参数的系统调用
static inline long __syscall1(long n, long a)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    __asm_syscall("r"(a7), "0"(a0))
}
```

到这一步，我们会将系统调用号写进a7寄存器，其它的参数从a0开始往后放
寄存器处理好以后就要调用ecall了

最后，任何系统调用最后都会演变成调用ecall指令，在这之前参数已经放进寄存器，系统调用号也放进了a7(用于指定一会儿调用哪个系统调用函数)，最后ecall指令会完成三个操作

- 将当前的pc写道sepc寄存器
- 将权限升级为Smode
- 转移到stvec寄存器指的位置（即user_vector)

从user_vector出来以后，跳转到trap_user_handler()，在这里我们已经进入了Smode（内核态），在这里我们调用内核态下的syscall函数(defined in kernel/syscall.c)

## 数据迁移系统调用函数

最终，在trap_user_handler函数中，如果接收到系统调用的scause就调用Smode下的syscall函数，再在syscall函数中根据a7寄存器内容，调用对应编号的系统调用函数

以下是两个负责在用户和内核态之间拷贝内容的函数
因为两个地址，一个是内核地址，一个是虚拟地址，内核地址可以通过satp自动翻译，但是虚拟地址需要通过进程携带的页表进行手动翻译，每一页内容存在一个pte当中，所以每次进行memmove时最多复制一页，因为下一页的地址需要重新翻译，所以我们需要通过while循环一页一页的复制，但是反复的调用用户页表来翻译地址，开销很大，我们或许可以考虑一定的优化

```c
// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n,va0,pa0;
    while(len>0)
    {
        va0=PGROUNDDOWN(src);
        pte_t* pte=vm_getpte(pgtbl,va0,false);
        pa0=PTE_TO_PA(*pte);
        if(pa0==0)
          panic("not valid pte in uvm_copyin");
        n=PGSIZE-(src-va0);
        if(n>len)
          n=len;
        memmove((void*)dst,(void*)(pa0+(src-va0)),n);
        len-=n;
        src+=va0+PGSIZE;
        dst+=n;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va0, pa0;
    while(len > 0)
  {
    va0 = PGROUNDDOWN(dst);
    pte_t* pte = vm_getpte(pgtbl,va0,false);
    pa0=PTE_TO_PA(*pte);
    if(pa0 == 0)
      panic("not valid pte in ucm_copyout");
    n = PGSIZE - (dst - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dst - va0)), (void*)src, n);

    len -= n;
    src += n;
    dst = va0 + PGSIZE;
  }
}
```

# 用户堆空间的伸缩

我们创建一个系统调用sys_brk()，它会接收一个地址参数（表示需要更改的堆顶地址），通过判断这个参数大于或小于当前的堆顶来决定调用uvm_heap_grow或者uvm_heap_ungrow

uvm_heap_grow:一页一页的分配物理页并映射地址在页表上

```c
// 用户堆空间增加, 返回新的堆顶地址 (注意堆顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    char* mem;
    uint64 new_heap_top = heap_top + len;
    uint64 old_heap_top = PGROUNDUP(heap_top);
    for(uint64 a=old_heap_top;a<new_heap_top;a+=PGSIZE)
    {
        mem=pmem_alloc(USER);
        memset(mem,0,PGSIZE);
        vm_mappages(pgtbl,a,(uint64)mem,PGSIZE,PTE_W|PTE_R|PTE_X|PTE_U);
    }
    return new_heap_top;
}
```

uvm_heap_ungrow:只需要调用vm_unmappages函数解映射的同时释放物理页

```c
// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;
    uint64 old_heap_top=heap_top;
    vm_unmappages(pgtbl,PGROUNDUP(new_heap_top),old_heap_top-PGROUNDUP(new_heap_top),true);
    return new_heap_top;
}
```

# 创建mmap仓库

对于每个进程我们都有一段在堆顶上和栈顶下的连续内存，我们为了管理这段内存，引入了mmap仓库。这段连续内存并不会被连续的使用，所以我们需要一个特别的数据结构来管理被切割地散乱的内存。
但是我们想如果每一段散乱的内存都需要一个结点来存储的话，我们将这个用于存储的数据结构保存在进程表内是非常不合理的，因为这个数据结构大小不固定，对于进程表来说过于庞大，所以我们考虑将这个数据结构放置为全局变量，用链表来存储，给足够的结点，这个结点只是一个管理员，被全局管理，当局部的进程分割了内存，需要增加一位管理员来管理多出来的散乱内存时，就向全局申请获得一个管理员，至于管理员需要管理哪里的内存，多少的内存都又“雇用”他的进程决定

初始化仓库

```c
// 初始化上述三个数据结构
void mmap_init()
{
    spinlock_init(&list_lk,"mmap_list_lock");
    list_head=&list_mmap_region_node[0];
    for(int i=0;i<N_MMAP-1;i++)
    {
        mmap_region_node_t* node;
        node=&list_mmap_region_node[i];
        node->mmap.begin=MMAP_BEGIN;
        node->mmap.npages=0;
        node->mmap.next=NULL;
        node->next=&list_mmap_region_node[i+1];
    }
    mmap_region_node_t* node;
    node=&list_mmap_region_node[N_MMAP-1];
    node->mmap.begin=MMAP_BEGIN;
    node->mmap.npages=0;
    node->mmap.next=NULL;
    node->next=NULL;
}
```

分配一个结点（雇佣一位管理员）

```c
// 从仓库申请一个 mmap_region_t
// 若申请失败则 panic
// 注意: list_head 保留, 不会被申请出去
mmap_region_t* mmap_region_alloc()
{
    spinlock_acquire(&list_lk);
    mmap_region_node_t* node=list_head->next;
    if(node==NULL)
    panic("mmap_region_alloc error");
    list_head->next=node->next;
    spinlock_release(&list_lk);
    return &node->mmap;
}
```

归还一个结点

```c
// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t* mmap)
{
    spinlock_acquire(&list_lk);
    int id=((uint64)mmap-(uint64)&list_mmap_region_node->mmap)/(sizeof(mmap_region_node_t));
    list_mmap_region_node[id].next=list_head->next;
    list_head->next=&list_mmap_region_node[id];
    spinlock_release(&list_lk);
}
```

# mmap 与 munmap

我们要通过管理mmap_region_t链表来管理MMAP_BEGIN到MMAP_END这段区域的散乱内存，每当需要为用户分配一段内存的时候，就调用mmap函数管理管理链表的切割，我们需要分类讨论各种情况的切割方式，我们一般分类头尾是否对齐的情况，针对不同情况可能申请和释放管理结点，munmap同理

uvm_mmap():

```c
// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");
    proc_t* p=myproc();
    mmap_region_t* head=p->mmap;
    // 修改 mmap 链 (分情况的链式操作)
    mmap_region_t* now=head->next;
    mmap_region_t* last=head;
    for(;now!=NULL;last=now,now=now->next)
    {
      printf("头部未对齐：%d %d\n",begin>now->begin,begin+npages*PGSIZE<=now->begin+now->npages*PGSIZE);
      printf("头部对齐：%d\n",begin==now->begin);
      //头部未对齐    
      if(begin>now->begin&&begin+npages*PGSIZE<=now->begin+now->npages*PGSIZE)
      {
        //尾部未对齐
        if(begin+npages*PGSIZE<now->begin+now->npages*PGSIZE)
        {
          mmap_region_t* new=mmap_region_alloc();
          new->begin=begin+npages*PGSIZE;
          new->npages=now->npages-(begin-now->begin)/PGSIZE-npages;
          new->next=now->next;
          now->next=new;
          now->npages=(begin-now->begin)/PGSIZE;
          break;
        }
        //尾部对齐
        else if(begin+npages*PGSIZE==now->begin+now->npages*PGSIZE)
        {
          now->npages-=(begin-now->begin)/PGSIZE;
          break;
        }
      }
      //头部对齐
      else if(begin==now->begin||(begin==0&&npages<=now->npages))
      {
        //尾部对齐
        if(npages==now->npages)
        {
          begin=now->begin;
          last->next=now->next;
          mmap_region_free(now);
          break;
        }
        //尾部未对齐
        else if(npages<now->npages)
        {
          begin=now->begin;
          now->begin=now->begin+npages*PGSIZE;
          now->npages=now->npages-npages;
          break;
        }
      }
    }
    assert(now!=NULL,"uvm_mmap failed");
    // 修改页表 (物理页申请 + 页表映射)
    uint64 va = begin;
    for (uint32 i = 0; i < npages; i++)
    {
        char *pa = pmem_alloc(false);
        vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE, perm);
        va += PGSIZE;
    }
}
```

uvm_munmap():

```c
// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");
    proc_t* p=myproc();
    mmap_region_t* head=p->mmap;
    uint64 end=begin+npages*PGSIZE;
    // new mmap_region 的产生
    mmap_region_t* new=mmap_region_alloc();
    new->begin=begin;
    new->npages=npages;
    // 尝试合并 mmap_region
    mmap_region_t* last=head;
    mmap_region_t* now=last->next;
    printf("need:%d %d\n",(begin-MMAP_BEGIN)/PGSIZE,(begin-MMAP_BEGIN)/PGSIZE+npages);
    for(;now!=NULL;last=now,now=now->next)
    {
      printf("had: last:%d %d\nnext:%d %d\n",(last->begin-MMAP_BEGIN)/PGSIZE,(last->begin-MMAP_BEGIN)/PGSIZE+last->npages,(now->begin-MMAP_BEGIN)/PGSIZE,(now->begin-MMAP_BEGIN)/PGSIZE+now->npages);
      uint64 lastend=last->begin+last->npages*PGSIZE;
      if(begin>=lastend&&end<=now->begin)
      {
        //首尾都对齐
        if(begin==lastend&&end==now->begin)
        {
          if(last!=head)
          {
            mmap_merge(last,new,true);
            mmap_merge(last,now,true);
            last->next=now->next;
          }
          else
          {
            mmap_merge(new,now,false);
            now->begin=new->begin;
            now->npages+=new->npages;
          }
        }
        //首部对齐
        else if(begin==lastend)
        {
          if(last!=head)
            mmap_merge(last,new,true);
          else
          {
            new->next=last->next;
            last->next=new;
          }
        }
        //尾部对齐
        else if(end==now->begin)
        {
          mmap_merge(new,now,false);
        }
        //首尾都不对齐
        else
        {
          assert(begin>lastend&&end<now->begin,"uvm_munmap error");
          last->next=new;
          new->next=now;
        }
        break;
      }
      //确保头尾没有重合在前后两个区域中
      else
      {
        assert(end<=last->begin||begin>=now->begin+now->npages*PGSIZE,"unvalid munmap");
      }
    }
    if(now==NULL)
    {
      if(begin==last->begin+last->npages*PGSIZE)
      {
        mmap_merge(last,new,true);
      }
      else 
      last->next=new;
    }
    // 页表释放
    vm_unmappages(p->pgtbl,begin,npages*PGSIZE,true);
}
```

# 页表的复制和销毁

uvm_copy_pgtbl():

```c
// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top */
    copy_range(old, new, HEAP_BOTTOM, heap_top);

    /* step-2: ustack */
    //最开始用户栈只安排了一页
    copy_range(old, new, USTACK_BOTTOM-ustack_pages*PGSIZE, USTACK_BOTTOM);

    /* step-3: mmap_region */
    for(mmap_region_t *region=mmap;region!=NULL;region=region->next)
    {
        uint64 begin=region->begin+region->npages*PGSIZE;
        uint64 end;
        if(region->next==NULL)
        end=MMAP_END;
        else 
        end=region->next->begin;
        copy_range(old,new,begin,end);
    }
}
```

uvm_destroy_pgtbl():

```c
// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3, level = 0 说明是页表管理的物理页
void uvm_destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
  if(level==0)
  {
    pmem_free((uint64)pgtbl,USER);
  }
  for(uint32 i=0;i<PGSIZE/sizeof(pte_t);i++)
  {
    pte_t* pte=(pte_t*)pgtbl[i];
    uvm_destroy_pgtbl((pgtbl_t)PTE_TO_PA(*pte),level-1);
  }
  pmem_fr
```

## 测试代码

最后观察是否两个页表除了code和data段以外相同

```c
//LAB5 TEST
	pgtbl_t new=proc_pgtbl_init((uint64)proczero.tf); uvm_copy_pgtbl(proczero.pgtbl,new,proczero.heap_top,proczero.ustack_pages,proczero.mmap);
    vm_print(new);
    printf("***\n");
    vm_print(proczero.pgtbl);
```
