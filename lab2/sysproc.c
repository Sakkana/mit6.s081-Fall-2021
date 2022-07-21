#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
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

// lab2 trace
uint64
sys_trace(void)
{
  int mask;
  // 从寄存器中取值 (控制台入参)，存入 mask
  if (argint(0, &mask) < 0) {
    return -1;
  }
  
  // 将寄存器中的 mask 给 this 进程的 mask
  myproc()->mask = mask;
  return 0;
}

// lab2 sysinfo
uint64
sys_sysinfo(void)
{
  
  // 从用户态读取用户的结构体指针
  uint64 addr;
  if(argaddr(0, &addr) < 0) {
    return -1;
  }

  // 定义一个存储 sysinfo 的结构体
  struct sysinfo info;

  // 调用自己编写的统计函数并赋值
  info.freemem = sysinfo_free_mem();
  info.nproc = sysinfo_free_proc();

  // 获得当前进程的控制信息
  struct proc *p = myproc();

  // 复制 sysinfo 结构体 info 到用户传来的地址
  // 用户传进来的虚拟地址是 addr，我们通过 pagetable 将数据复制到物理地址
  if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0) {
    return -1;
  }
  
  // printf("小夫，我要进来 sysproc.c 了！\n");
  // printf("小夫，我要进来 sysproc.c 了！\n");
  // printf("小夫，我要进来 sysproc.c 了！\n");

  return 0;
}