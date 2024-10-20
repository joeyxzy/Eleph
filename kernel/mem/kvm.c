// kernel virtual memory management

#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/print.h"
#include "lib/str.h"
#include "riscv.h"
#include "memlayout.h"

static pgtbl_t kernel_pgtbl; // 内核页表


// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN PTE_TO_PA PA_TO_PTE
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if(va>VA_MAX)
        return 0;
    for(int level=2;level>0;level--)
    {
        pte_t *pte=&pgtbl[VA_TO_VPN(va,level)];
        if((*pte)&PTE_V)
        {
            pgtbl=(pgtbl_t)PTE_TO_PA(*pte);
        }
        else
        {
            if(!alloc)
                return 0;
            pgtbl=(pgtbl_t)pmem_alloc(KERNEL);
            memset(pgtbl,0,PGSIZE);
            *pte=PA_TO_PTE(pgtbl)|PTE_V;
        }
    }
    return &pgtbl[VA_TO_VPN(va,0)];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    uint64 now =PGROUNDDOWN(va);
    uint64 end=PGROUNDDOWN(va+len-1);
    pte_t *pte;
    while(1)
    {
        if((pte=vm_getpte(pgtbl,now,true))==0)
            panic("vm_mappgaes:getpte");
        /*if((*pte)&PTE_V)
            panic("vm_mappgaes:remap");*/
        *pte=PA_TO_PTE(pa)|PTE_V|perm;
        if(now==end)
            break;
        now+=PGSIZE;
        pa+=PGSIZE;
    }
    return ;
}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    uint64 now =PGROUNDDOWN(va);
    uint64 end=PGROUNDDOWN(va+len-1);
    pte_t *pte;
    while(1)
    {
        if((pte=vm_getpte(pgtbl,now,false))==0)
            panic("vm_unmappgaes:getpte");
        if(!(*pte)&PTE_V)
            panic("vm_unmappgaes:not mapped");
        if(freeit)
            pmem_free(PTE_TO_PA(*pte),USER);
        *pte=0;
        if(now==end)
            break;
        now+=PGSIZE;
    }
    return ;
}

// 完成 UART CLINT PLIC 内核代码区 内核数据区 可分配区域 的映射
// 相当于填充kernel_pgtbl
void kvm_init()
{
    printf("memset start\n");
    kernel_pgtbl=pmem_alloc(KERNEL);
    memset(kernel_pgtbl,0,PGSIZE);
    printf("memset_end\n");
    vm_mappages(kernel_pgtbl,UART_BASE,UART_BASE,PGSIZE,PTE_R|PTE_W);
    printf("memset over\n");
    vm_mappages(kernel_pgtbl,PLIC_BASE,PLIC_BASE,0x400000,PTE_R|PTE_W);

    vm_mappages(kernel_pgtbl,CLINT_BASE,CLINT_BASE,0x10000,PTE_R|PTE_W);

    vm_mappages(kernel_pgtbl,KERNEL_BASE,KERNEL_BASE,(uint64)KERNEL_DATA-KERNEL_BASE,PTE_R|PTE_X);

    vm_mappages(kernel_pgtbl,(uint64)KERNEL_DATA,(uint64)KERNEL_DATA,(uint64)ALLOC_BEGIN-(uint64)KERNEL_DATA,PTE_R|PTE_W);

    vm_mappages(kernel_pgtbl,(uint64)ALLOC_BEGIN,(uint64)ALLOC_BEGIN,(uint64)ALLOC_END-(uint64)ALLOC_BEGIN,PTE_R|PTE_W);

}

// 使用新的页表，刷新TLB
void kvm_inithart()
{
  //写入satp寄存器
  w_satp(MAKE_SATP(kernel_pgtbl));
  //清空TLB
  sfence_vma();
}

// for debug
// 输出页表内容
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for(int i = 0; i < PGSIZE / sizeof(pte_t); i++) 
    {
        pte = pgtbl_2[i];
        if(!((pte) & PTE_V)) continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);
        
        for(int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if(!((pte) & PTE_V)) continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_2);

            for(int k = 0; k < PGSIZE / sizeof(pte_t); k++) 
            {
                pte = pgtbl_0[k];
                if(!((pte) & PTE_V)) continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));                
            }
        }
    }
}

void kernel_test_print()
{
    vm_print(kernel_pgtbl);
}