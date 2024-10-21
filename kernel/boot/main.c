#include "riscv.h"
#include "lib/print.h"
#include "lib/str.h"
#include "proc/cpu.h"
#include "mem/pmem.h"
#include "mem/vmem.h"


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
        printf("\n");
        printf("Eleph kernel is booting\n");
        printf("\n");
    }
    while (1);    
}