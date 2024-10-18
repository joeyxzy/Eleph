#include "mem/pmem.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "lib/str.h"

//区分取锁时的判断参数
#define KERNEL true
#define USER false

extern char end[];
//经过kernel.ld连接器操作后，KERNBASE起始往后的RAM已经被用掉一部分，新的开头是end

// 物理页节点
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 许多物理页构成一个可分配的区域
typedef struct alloc_region {
    uint64 begin;          // 起始物理地址
    uint64 end;            // 终止物理地址
    spinlock_t lk;         // 自旋锁(保护下面两个变量)
    uint32 allocable;      // 可分配页面数    
    page_node_t list_head; // 可分配链的链头节点
} alloc_region_t;

// 内核和用户可分配的物理页分开
static alloc_region_t kern_region, user_region;

#define KERN_PAGES 1024 // 内核可分配空间占1024个pages

// 物理内存初始化
void pmem_init(void)
{
  //先初始化内核可分配区域
  kern_region.begin = (uint64)end;
  kern_region.end = kern_region.begin + KERN_PAGES * PGSIZE;
  kern_region.allocable = KERN_PAGES;
  spinlock_init(&kern_region.lk, "kern_region");
  freerange((void*)kern_region.begin, (void*)kern_region.end, KERNEL);
  //再初始化用户可分配区域

}

//释放一个范围的物理页，集体调用pmem_free
void freerange(void* begin, void* end, bool in_kernel)
{
    char* p;
    p = (char*)PGROUNDUP((uint64)begin);
    for(; p + PGSIZE <= (char*)end; p += PGSIZE)
    {
        pmem_free((uint64)p, in_kernel);
    }
}

// 返回一个可分配的干净物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{
    page_node_t *node;
    spinlock_acquire(in_kernel?&kern_region.lk:&user_region.lk);
    node = in_kernel?kern_region.list_head.next:user_region.list_head.next;
    if(node == NULL)
    {
        panic("pmem_alloc: out of memory");
    }
    in_kernel?kern_region.list_head.next = node->next:user_region.list_head.next = node->next;
    in_kernel?kern_region.allocable--:user_region.allocable--;
    spinlock_release(in_kernel?&kern_region.lk:&user_region.lk);
    memset(node,2,PGSIZE);
    return (void*)node;
}

// 释放物理页,将物理页放回链表
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    page_node_t *node;
    if(page%PGSIZE || (in_kernel && (page >= kern_region.end||(char*)page<end))||(!in_kernel && (page >= user_region.end||page<kern_region.end)))
    //确保在用户和内核各自的内存范围以内
    {
        panic("pmem_free: wrong page");
    }
    memset(page, 1 ,PGSIZE);
    node = (page_node_t*)page;
    spinlock_acquire(in_kernel?&kern_region.lk:&user_region.lk);
    node->next = in_kernel?kern_region.list_head.next:user_region.list_head.next;
    in_kernel?kern_region.list_head.next = node:user_region.list_head.next = node;
    in_kernel?kern_region.allocable++:user_region.allocable++;
    //维护个数变量
    spinlock_release(in_kernel?&kern_region.lk:&user_region.lk);
    //有没有可能另外的并行程序同时放入一个物理页？后续添加维护？
}   