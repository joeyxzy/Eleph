#include "proc/cpu.h"
#include "riscv.h"

static cpu_t cpus[NCPU];

cpu_t* mycpu(void)
{
  int id=mycpuid();
  cpu_t *c=&cpus[id];
  return c;
}

int mycpuid(void) 
{
  int id = r_tp();
  return id;
}
