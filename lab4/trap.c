#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// trampoline.S 保存好一系列信息后，跳转到该函数
// 正式处理异常：分析原因、处理、返回
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 先改变 stvec，因为现在是内核空间
  // 如果再次发生异常，会被 kernelvec 处理 而不是 uservec
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  // 保存 sepc 寄存器的值 (程序计数器)
  // 因为这个程序在执行时可能被切换到另一个进程，进入另一个进程的用户空间
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call
    // 系统调用引起的异常

    // 检查是不是有其他的进程杀掉了当前进程
    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // 保存了用户代码的程序计数器，在 ecall 的下一条指令处继续执行，而不是重复执行 ecall
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    // 在保存好 sstatus, registers 之后再打开中断
    intr_on();

    // 执行系统调用
    syscall();
  } else if((which_dev = devintr()) != 0){
    // 设备中断引起的异常
    // ok
    // 由于这个中断时用户态下的时钟中断引起的
    if (which_dev == 2) {
      struct proc* p = myproc();
      // 存在周期性调用
      if (p->interval > 0 && p->is_running == 0) {
        if (p->interval == p->pass) {
          // while(p->is_running == 1);  // 如果有人正在跑，自旋等待
          p->pass = 0;  // 归零，开始新的周期
          *(p->saved_trapframe) = *(p->trapframe);  // 保存之前的栈帧状态
          p->trapframe->epc = p->handler; // 用户态程序计数器指向周期性处理程序
          p->is_running = 1;  // 拿到标记，自己准备跑
        }
        ++ p->pass; // 每次累计时钟周期
      }
    }
  } else {
    // 非法用户程序引起的异常
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  // 检查处理完异常后有没有被杀掉，如果被杀了就不返回了
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  // 时钟中断，放弃 CPU
  if(which_dev == 2) 
    yield();

  // 处理完异常，返回
  usertrapret();
}

//
// return to user space
// 异常处理结束，返回用户空间
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from kerneltrap() to usertrap(), 
  // so turn off interrupts until we're back in user space, where usertrap() is correct.
  // 在回到用户空间之前关闭中断
  // 因为接下来的操作我们仍然在内核中
  // 如果这时候来了一个中断，后面设置成了用户的异常处理程序后，会走向用户的异常处理代码
  // 即使这个异常是在内核中发生的
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // 使 stvec 重新指向用户空间的异常处理代码 (在 usertrap() 中被设置成了内核对应的异常处理代码)
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  // 这部分存储在用户栈的 trapframe 中
  // 在 trampoline.S 中这些值从寄存器中 trapframe 中存到了寄存器里
  // 现在从寄存器中重新放回用户栈的 trapframe
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // PC 被设置成用户代码中的 PC，之前在 usertrap() 中被保存在了用户栈 trapframe 的 epc 中
  // 后面在 trampoline.S 中会使用 ret 指令，将 sepc 中的值写到 PC 中
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 将用户页表的地址放入 satp
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, 
  // which switches to the user page table, 
  // restores user registers,
  // and switches to user mode with sret.
  // 计算出我们将要跳转到汇编代码的地址: tampoline 中的 userret 函数
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  // 把地址作为函数指针使用，传入两个参数，放在 a0 和 a1 中
  // 像切换内核和用户页表的这种工作只能在汇编代码中实现，因为 trampoline 在内核和用户空间中都存在映射并且映射到了同一个地址
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
// 处理来自内核的异常
// 专门处理 设备中断 + exception 
void 
kerneltrap()
{
  int which_dev = 0;

  // 先保存好程序计数器，状态，原因
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  // SPP = 0, 来自用户的异常
  // 不归我管
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");

  // SIE = 1，允许中断
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  // 处理
  if((which_dev = devintr()) == 0){
    // 发生异常错误
    printf("scause %p\n", scause);                      // 打印原因
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());  // 打印当前 PC 和 stval 是什么
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  // 时钟中断，当前线程放弃 CPU，让其他线程运行
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING) {
    yield();
  }
    
  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  // 恢复信息
  // 因为之前 yield 可能存储了别的线程的信息
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt, and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.
    // 来自 machine-mode 下时钟中断的 软件中断

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

