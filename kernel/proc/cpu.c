#include "proc/cpu.h"
#include "riscv.h"

static cpu_t cpus[NCPU];

//返回当前cpu对应结构体指针
cpu_t* mycpu(void)
{
  int id=mycpuid();
  cpu_t *c=&cpus[id];
  return c;
}

//获取当前cpu的id
int mycpuid(void) 
{
  int id = r_tp();
  return id;
}
