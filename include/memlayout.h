/* memory leyout */
#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

// UART 相关
#define UART_BASE  0x10000000ul
#define UART_IRQ   10

// 内核基地址
#define KERNEL_BASE 0x80000000ul
//从KERNEL_BASE开始，分配128Mb的物理内存
#define PHYSTOP (KERNEL_BASE+128*1024*1024) 

#endif