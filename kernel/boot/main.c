#include "riscv.h"
#include "lib/print.h"
#include "proc/cpu.h"

volatile static int started = 0;

int main()
{
    if(mycpuid()==0)
    {
        print_init();
        printf("\n");
        printf("Eleph kernel is booting\n");
        printf("\n");
    }
    while (1);    
}