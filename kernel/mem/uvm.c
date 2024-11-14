#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"
#include "memlayout.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        //assert(PTE_CHECK(*pte), "uvm_copy_pgtbl: pte check fail");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
    mmap_region_t* head=myproc()->mmap;
    //mmap_region_t* last=head;
    mmap_region_t* now=head->next;
    printf("***\n");
    for(int index=1;now!=NULL;now=now->next,index++)
    {
      int start=(now->begin-MMAP_BEGIN)/PGSIZE;
      int end=start+now->npages;
      printf("index %d: start:%d end:%d\n",index,start,end);
    }
    printf("***\n");
}

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
  pmem_free((uint64)pgtbl,KERNEL);
}

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

// 用户堆空间增加, 返回新的堆顶地址 (注意堆顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    char* mem;
    uint64 new_heap_top = heap_top + len;
    if(new_heap_top>MMAP_BEGIN) panic("heap_grow out of bound");
    uint64 old_heap_top = PGROUNDUP(heap_top);
    for(uint64 a=old_heap_top;a<new_heap_top;a+=PGSIZE)
    {
        mem=pmem_alloc(USER);
        memset(mem,0,PGSIZE);
        vm_mappages(pgtbl,a,(uint64)mem,PGSIZE,PTE_W|PTE_R|PTE_X|PTE_U);
    }
    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;
    uint64 old_heap_top=heap_top;
    vm_unmappages(pgtbl,PGROUNDUP(new_heap_top),old_heap_top-PGROUNDUP(new_heap_top),true);
    return new_heap_top;
}

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

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && maxlen > 0){
    va0 = PGROUNDDOWN(src);
    pte_t* pte = vm_getpte(pgtbl, va0,false);
    pa0=PTE_TO_PA(*pte);
    if(pa0 == 0)
      panic("not valid pte in uvm_copyin_str");
    n = PGSIZE - (src - va0);
    if(n > maxlen)
      n = maxlen;

    char *p = (char *) (pa0 + (src - va0));
    while(n > 0){
      if(*p == '\0'){
        *(char*)dst = '\0';
        got_null = 1;
        break;
      } else {
        *(char*)dst = *(char*)p;
      }
      --n;
      --maxlen;
      p++;
      dst++;
    }

    src = va0 + PGSIZE;
  }
  if(got_null){
    return;
  } else {
    panic("uvm_copyin_str:out of maxlen");
  }
}