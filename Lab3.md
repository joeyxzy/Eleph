# 处理trap的全流程
## start()函数
首先进入start函数，在start函数里面，处于Mmode，对于trap我们要做的是先把能进入Smode处理trap的各种异常和中断“使能”，也就是说可以进入Smode向量入口执行trap处理的汇编代码，但是需要搞清楚的是这个时候这个应该被写在stvec寄存器里的向量入口还没被写好，写好之时就是所有异常和中断可以被响应之时
```c
void
start()
{
  ...

  // 指定所有异常和中断都由smode处理
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  //sie 寄存器中的各个位用于指示哪些类型的中断可以在 S-mode 下被处理器响应
  //使外部，时钟，软件中断都可以在smode被响应
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  //初始化Mmode中断
  timer_init();
  // 在Mmode下，将Mmode以外的模式无法访问的mhartid寄存器写到Smode可以访问的tp寄存器，
  
  ...
```
## timer_init()函数
接下来我们要在start函数中调用timer_init，来完成一些时钟中断的初始化，对于时钟中断来说，我们需要初始化好MTIMECMP，MTIME，INTERVAL，全部初始化进寄存器当中，至于滴答的过程是完全交给硬件在做的，每次滴答到MTIMECMP=MTIME+INTERVAL时，就会呼唤一个时钟中断的处理，对于时钟中断我们要进入Mmode的汇编入口，所以我们也需要初始化这个入口----写入mtvec寄存器，完成这一切之后，我们再使能时钟中断再Mmode下可以执行，最后通过修改mie正式启动定时器，但是到目前为止，虽然计时器已经开始工作，但是当它进入Mmode的汇编代码中时，在更新完MTIMECMP等变量后呼唤更改sip的一位期待进入Mmode，但是发现不知道入口在哪里（因为stvec还没写好），所以这时候虽然在计时，但是没有任何响应，因为响应函数都是在Smode中被call的
```c
// 时钟初始化
// called in start.c
void timer_init()
{
    // 每个 CPU 都有一个单独的定时器中断源
  int id = r_mhartid();

  // 通过CLINT_MTIMECMP(id)传入cpuid得知MTIMECMP寄存器的地址
  //然后初始化它
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + INTERVAL;

  // 准备用于 timervec 的 scratch 区域
  // scratch[0..3] : 为 timervec 保存寄存器的空间
  // scratch[4] : CLINT MTIMECMP 寄存器的地址
  // scratch[5] : 所需的定时器中断间隔（周期）
  uint64 *scratch = mscratch[id];
  //初始化每个hart的MTIMECMP值
  mscratch[id][3] = CLINT_MTIMECMP(id);
  mscratch[id][4] = INTERVAL;
  //scratch寄存器放的压根就不是数组，而是指针，真正的内容被放在mscratch0数组里面
  w_mscratch((uint64)scratch);
  // 设置M_Mode出现中断以后进行的中断处理程序地址
  //这个在对应kernelvec.S这个处理中断的汇编函数
  //将处理异常的入口写入机器模式下的tvec寄存器
  w_mtvec((uint64)timer_vector);
  // 允许 Machine Mode 中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);
  // 允许 Machine Mode 定时器中断
  w_mie(r_mie() | MIE_MTIE);
}
```
## main()函数
这时候我们执行完了start函数，完成了部分初始化，就该进入main函数，同时也从Mmode转换到了Smode了，我们在timer这个文件里面，还有一些记录tick的函数，他们工作咋Smode，只是为了记录tick，真正的计时是硬件完成的，main函数里面就是要通过调用各种初始化函数，完成启动trap的最后一步，包括初始化plic和填好stvec，让时钟中断真的被启动
最后，所有初始化完成，我们前面说的都是在打开各种小开关，真正启动最后还需要通过intr_on函数控制sstatus启动Smode总开关，这时候全部启动
```c
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
```
包括main函数调用的一些函数
```c
// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    assert(mycpuid()==0,"tki cpuid wrong!");
    //printf("trap_krenel_init start!\n");
    plic_init();
    plic_inithart();
    timer_create();
    //printf("trap_kernel_init end!\n");
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    //将smode下的中断入口写在寄存器里
    //printf("have not writen!\n");
    w_stvec((uint64)kernel_vector);
}
```
## trap_kernel_handler()函数
```c
// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    //printf("called\n");
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    //uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    int trap_id = scause & 0xf; 
    //printf("cpuid:%d trapid:%d\n",mycpuid(),trap_id);
    // 中断异常处理核心逻辑
    if((scause&0x8000000000000000L)&&trap_id==9)
    {
        external_interrupt_handler();
    }
    else if(scause==0x8000000000000001L)
    {
        timer_interrupt_handler();
        //这里对于时钟中断可能发生多级中断，所以寄存器可能已经被多级修改了
        //这里很像dfs操作，调用完递归函数后，要恢复现场
        w_sepc(sepc);
        w_sstatus(sstatus);
    }
    else 
    {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }
}
```
# sip,sie,sstatus
## 1. sie 寄存器（Supervisor Interrupt Enable）
功能：
sie 寄存器用于控制 S-mode 下哪些类型的中断是可以被处理器响应的。
它决定了哪些中断请求可以导致处理器进入 S-mode 并执行中断处理程序。
位说明：
sie 寄存器中的各个位对应不同的中断类型
设置某个位为 1 表示使能该类型的中断；设置为 0 表示禁用该类型的中断
## 2. sip 寄存器（Supervisor Interrupt Pending）
功能：
sip 寄存器用于标记当前是否有某个类型的中断请求待处理。
如果某个位被设置为 1，表示有一个相应的中断请求待处理。
位说明：
sip 寄存器中的各个位对应不同的中断请求。
设置某个位为 1 表示有一个相应的中断请求待处理；设置为 0 表示没有该类型的中断请求。
## 3. sstatus 寄存器（Supervisor Status）
功能：
sstatus 寄存器用于控制和监视 S-mode 下的一些状态和特性。
它包含了一些重要的标志位，用于控制和监控处理器的状态。
位说明：
SIE 位（Supervisor Interrupt Enable）：控制 S-mode 下的中断是否使能
这个大开关就是intr_on和intr_off在控制的

