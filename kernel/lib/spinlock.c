#include "lib/lock.h"
#include "lib/print.h"
#include "proc/cpu.h"
#include "riscv.h"

// 带层数叠加的关中断
void push_off(void)
{
  int origin = intr_get();
  //获取当前cpu的中断开启状态，方便后面作为变量记录
  intr_off();
  if(mycpu()->noff == 0)
  {
    mycpu()->origin = origin;
  }
  mycpu()->noff++;
}

// 带层数叠加的开中断
void pop_off(void)
{
  if(mycpu()->noff < 1)
  {
    panic("pop_off");
  }
  mycpu()->noff--;
  //关一个锁就减少一个层数，直到层数归零，就可以真正恢复中断原来的状态
  if(mycpu()->noff == 0)
  {
    if(mycpu()->origin)
    {
      intr_on();
    }
  }
}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{
  return (lk->locked && lk->cpuid == mycpuid());
  //看看这个自旋锁锁上没有同时还要是被这个cpu锁上的
  //如果是的话，就说明这个cpu持有这个自旋锁
  //那么就要返回true，相当于发生了错误
}

// 自选锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpuid = -1;
}

// 获取自旋锁
void spinlock_acquire(spinlock_t *lk)
{    
  push_off();
  if(spinlock_holding(lk))
  {
    printf("locked:%d cpuid:%d real cpu:%d\n",lk->locked,lk->cpuid,mycpuid());
    panic("acquire");
  }
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0);
  //忙等，直到这个自旋锁被释放，类XCHG指令
  __sync_synchronize();
  //给编译器设置一个内存屏障，保证上面的操作不会被重排
  lk->cpuid = mycpuid();
}

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
  if(!spinlock_holding(lk))
  {
    panic("release");
  }
  lk->cpuid = -1;
  __sync_synchronize();
  lk->locked = 0;
  pop_off();
}