// in initcode.c
#include "sys.h"

int main()
{
    int L[5];
    char* s = "hello, world"; 
    syscall(SYS_copyout, L);
    syscall(SYS_copyin, L, 5);
    syscall(SYS_copyinstr, s);
    long long heap_top = syscall(SYS_brk, 0);

    heap_top = syscall(SYS_brk, heap_top + 4096 * 10);

    heap_top = syscall(SYS_brk, heap_top - 4096 * 10);

    while(1);
    return 0;
}