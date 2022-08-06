#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{

  // lab4 - trap
  backtrace();

  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);        // 上锁
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);         // 解锁
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// lab4 - traps
uint64
sys_sigalarm(void) {
  // 用户态执行 alarmtest，调用 sigalarm()
  // 有两个入参：interval，handler
  // 只需要设置好参数就可以了

  int interval;
  if(argint(0, &interval) < 0)
    return -1;

  uint64 handler;
  if(argaddr(1, &handler) < 0)
    return -1;

  if (interval < 0)
    return -1;

  struct proc* p = myproc();
  p->interval = interval;
  p->handler = handler;
  p->pass = 0;

  //printf("sigalrm is invoked!\n");
  return 0;
}

uint64
sys_sigreturn(void) {
  struct proc* p = myproc();
  *(p->trapframe) = *(p->saved_trapframe);
  p->is_running = 0;
  return 0;
}