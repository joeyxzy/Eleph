#include "lib/print.h"
#include "lib/str.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "../include/memlayout.h"

// 包装 mmap_region_t 用于仓库组织
typedef struct mmap_region_node {
    mmap_region_t mmap;
    struct mmap_region_node* next;
} mmap_region_node_t;

#define N_MMAP 256

// mmap_region_node_t 仓库(单向链表) + 指向链表头节点的指针 + 保护仓库的锁
static mmap_region_node_t list_mmap_region_node[N_MMAP];
static mmap_region_node_t* list_head;
static spinlock_t list_lk;

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

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t* mmap)
{
    spinlock_acquire(&list_lk);
    mmap->begin=MMAP_BEGIN;
    mmap->npages=0;
    mmap->next=NULL;
    int id=((uint64)mmap-(uint64)&list_mmap_region_node->mmap)/(sizeof(mmap_region_node_t));
    list_mmap_region_node[id].next=list_head->next;
    list_head->next=&list_mmap_region_node[id];
    spinlock_release(&list_lk);
}

// 输出仓库里可用的 mmap_region_node_t
// for debug
void mmap_show_mmaplist()
{
    spinlock_acquire(&list_lk);
    
    mmap_region_node_t* tmp = list_head;
    int node = 1, index = 0;
    while (tmp)
    {
        index = tmp - list_head;
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}