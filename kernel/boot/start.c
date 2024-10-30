#include "riscv.h"
#include"dev/timer.h"

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

  // 指定所有异常和中断都由smode处理
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  //sie 寄存器中的各个位用于指示哪些类型的中断可以在 S-mode 下被处理器响应
  //使外部，时钟，软件中断都可以在smode被响应
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  //初始化Mmode中断
  timer_init();
  // 在Mmode下，将Mmode以外的模式无法访问的mhartid寄存器写到Smode可以访问的tp寄存器，
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  //转换为S态，然后跳转main函数
  asm volatile("mret");
}