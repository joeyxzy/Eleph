#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    // 每个 CPU 都有一个单独的定时器中断源
  int id = r_mhartid();

  // 请求 CLINT 产生定时器中断。
  int interval = 1000000; // 周期间隔，大约相当于 QEMU 中的 1/10 秒
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // 准备用于 timervec 的 scratch 区域
  // scratch[0..3] : 为 timervec 保存寄存器的空间
  // scratch[4] : CLINT MTIMECMP 寄存器的地址
  // scratch[5] : 所需的定时器中断间隔（周期）
  uint64 *scratch = mscratch[32 * id];
  //初始化每个hart的MTIMECMP值
  scratch[4] = CLINT_MTIMECMP(id);
  scratch[5] = interval;
  //scratch寄存器放的压根就不是数组，而是指针，真正的内容被放在mscratch0数组里面
  w_mscratch((uint64)scratch);
  // 设置M_Mode出现中断以后进行的中断处理程序地址
  //这个在对应kernelvec.S这个处理中断的汇编函数
  w_mtvec((uint64)timer_vector);
  // 允许 Machine Mode 中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);
  // 允许 Machine Mode 定时器中断
  w_mie(r_mie() | MIE_MTIE);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
  sys_timer.ticks=0;
  spinlock_init(&sys_timer.lk,"clockticks");
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
  spinlock_acquire(&sys_timer.lk);
  sys_timer.ticks++;
  spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
  return sys_timer.ticks;
}