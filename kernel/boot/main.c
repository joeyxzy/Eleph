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
        int x=199;
        printf("asdasdas\n");
        printf("aaa%d",x);
        //printf("%d %p %x %% %s\n", 123, 128, 128, "hello");
        printf("\n");
    }
    while (1);    
}