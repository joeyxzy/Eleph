# pmem
我们分开了内核物理页和用户物理页
# 测试遇到的问题
## acquire问题
![[屏幕截图 2024-10-20 144623.png]]
这个地方其实出现了两个问题，一个是panic报的错，holding判断出现了问题，
我加了一个函数，打印出当前记录的cpuid，和立即取出cpuid，发现并不符合进入条件，所以证明在别的线程当中出现了修改cpuid的情况，但是在我脑子里能修改cpuid的只有取锁的时候才可以，也不符合，后来发现是在释放自旋锁的时候，默认让cpuid指向0了，这是不对的，因为我们需要表示cpuid指向-1表示没有
另外一个问题是panic的输出被切割了，所以需要修改一下panic

## 解决sfence_vma的卡住问题
问题出在没有理解那几个标志位的真实意义
```c
vm_mappages(kernel_pgtbl,KERNEL_BASE,KERNEL_BASE,(uint64)KERNEL_DATA-KERNEL_BASE,PTE_R|PTE_X);
    //这段区域放的是内核代码，所以要给它一个x位的权限，允许执行，但是不给w权限，防止内核代码被修改
    vm_mappages(kernel_pgtbl,(uint64)KERNEL_DATA,(uint64)KERNEL_DATA,(uint64)ALLOC_BEGIN-(uint64)KERNEL_DATA,PTE_R|PTE_W);
    //这段区域放的是数据段和bss段
    vm_mappages(kernel_pgtbl,(uint64)ALLOC_BEGIN,(uint64)ALLOC_BEGIN,(uint64)ALLOC_END-(uint64)ALLOC_BEGIN,PTE_R|PTE_W);
```
![[Pasted image 20241021112707.png]]
所以其实只需要将.text段单独映射就够了，因为.text段的权限是特殊的，他需要有x权限来执行，同时不允许W权限防止随意修改内核代码

DATA~BEGIN这个区域放好了数据和bss段，真正可以放入物理页链表内的是从BEGIN~END这个区域

所以其实映射可以将后两段合在一起

我测试的时候卡在sfence_vma就是因为没有给内核代码执行的权限

# vm_mappages
```c
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    uint64 now =PGROUNDDOWN(va);
    uint64 end=PGROUNDDOWN(va+len-1);
    pte_t *pte;
    while(1)
    {
        if((pte=vm_getpte(pgtbl,now,true))==0)
            panic("vm_mappgaes:getpte");
        if((*pte)&PTE_V&&pa!=PTE_TO_PA(*pte))
        //更宽松的remap检查，允许修改权限位，但是不允许修改物理页号
        {
            panic("vm_mappgaes:remap");
        }
        *pte=PA_TO_PTE(pa)|PTE_V|perm;
        if(now==end)
            break;
        now+=PGSIZE;
        pa+=PGSIZE;
    }
    return ;
}
```
在xv6中，直接简单粗暴地判断V位来决定有没有remap，但是有的时候我们可能需要修改一下权限位，而不更改物理页号，这个时候粗暴的判断就无法实现这件事，所以需要我们修改一下remap的判断来实现权限位的修改