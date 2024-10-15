// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";

//也就是初始化print锁
void print_init(void)
{
  uart_init();
  //启动uart驱动
  spinlock_init(&print_lk, "print");
}

//输出一个数的十六进制
void printptr(uint64 x)
{
  uart_putc_sync('0');
  uart_putc_sync('x');
  for(int i=0; i<16; i++)
  {
    uart_putc_sync(digits[x >> 60]);
    x <<= 4;
  }
}

static void
printint(int xx, int base, int sign)
{
  //xx是传入值，base是进制基数
  char buf[16];
  int i;
  uint32 x;

  //判断符号
  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    uart_putc_sync(buf[i]);
}

void printf(const char *fmt, ...)
{
  va_list ap;
  int i, c;
  char *s;

  spinlock_acquire(&print_lk);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);//将可变参数初始化到ap中
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      uart_putc_sync(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        uart_putc_sync(*s);
      break;
    case '%':
      uart_putc_sync('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      uart_putc_sync('%');
      uart_putc_sync(c);
      break;
    }
  }

  spinlock_release(&print_lk);
}

void panic(const char *s)
{
  printf("panic: ");
  printf(s);
  printf("\n");
  panicked = 1; // 设置全局变量，令其它cpu也无法使用
}

void assert(bool condition, const char* warning)
{
  if(!condition)
  {
    panic(warning);
  }
}
