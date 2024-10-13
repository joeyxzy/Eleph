#include "riscv.h"

__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void main();

void
start()
{
  //寄存器的读写操作
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  //让mepc寄存器指向main函数的地址，当中断处理完成后，会跳转到main函数
  w_mepc((uint64)main);

  //禁用MMU翻译地址，所有地址一律使用物理地址
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  //转换为S态，然后跳转main函数
  asm volatile("mret");
}