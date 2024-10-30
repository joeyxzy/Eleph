#include "riscv.h"
#include "lib/print.h"
#include "lib/str.h"
#include "proc/cpu.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "trap/trap.h"
#include "dev/timer.h"


volatile static int started = 0;

volatile static int over_1 = 0, over_2 = 0;

//static int* mem[1024];

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0) {
        print_init();
        pmem_init();
        kvm_init();
        kvm_inithart();
        printf("\ncpu %d is starting\n",mycpuid());
        trap_kernel_init();
        trap_kernel_inithart();
        __sync_synchronize();
        started=1;
    }
    else{
        while(started==0);
        __sync_synchronize();
        printf("cpu %d is starting\n",mycpuid());
        kvm_inithart();
        trap_kernel_inithart();
    }
    //先把stvec写好，才能intr_on，不然会因为没有找到进入Smode的入口而卡住   
    intr_on();
    while(1);
}