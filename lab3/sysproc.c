#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
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

// lab3 page table 3 - Detecting which pages have been accessed

/*
Your job is to implement pgaccess(), a system call that reports which pages have been accessed. 
The system call takes three arguments. 
1 it takes the starting virtual address of the first user page to check. 
2 it takes the number of pages to check. 
3 it takes a user address to a buffer to store the results into a bitmask (a datastructure 
that uses one bit per page and where the first page corresponds to the least significant bit). 
*/


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // get user parameters

  // starting virtual address of the first user page to check
  uint64 starting_va;
  if(argaddr(0, &starting_va) < 0)
    return -1;

  // the number of pages to check
  int num;
  if(argint(1, &num) < 0)
    return -1;

  // a user address to a buffer to store the results into a bitmask
  uint64 ans_buff;
  if(argaddr(2, &ans_buff) < 0)
    return -1;

  return pgaccess((void*)starting_va, num, (void*)ans_buff);
}
#endif

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

