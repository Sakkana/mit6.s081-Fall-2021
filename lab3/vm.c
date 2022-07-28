#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 * 内核中的页表
 */

// defined in riscv.h
// typedef uint64 pte_t;        // uint64 = unsigned long = 8 byte，一个 PTE 8 字节
// typedef uint64 *pagetable_t; // 512 PTEs

pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
// 为内核创建一个直接映射页表，called in "initkmp()"
pagetable_t
kvmmake(void)
{
  
  pagetable_t kpgtbl;               // 定义 kernel page table，是一个指针

  kpgtbl = (pagetable_t) kalloc();  // 为 root page table 分配一个 4KB 的物理页面
  memset(kpgtbl, 0, PGSIZE);        // 将所有的 PTE 条目初始化为 0

  // 初始化 I/O 设备

  // uart registers
  // #define UART0 0x10000000L
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio - mmio disk interface
  // #define VIRTIO0 0x10001000
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC - platform-level interrupt controlle
  // #define PLIC 0x0c000000L
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map [kernel text] executable and read-only.
  // #define KERNBASE 0x80000000L
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map [kernel data] and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the [trampoline] for trap entry/exit to the highest virtual address in the kernel.
  // #define TRAMPOLINE (MAXVA - PGSIZE)
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  // 映射内核栈，defined in "proc.c"
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
// 初始化一个内核页表， called in "main.c"
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table, and enable paging.
// 启用页表，将初始化的内核页表地址写入 stap 寄存器
// 使得 MMU 可以看到我们设置的页表
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table pages.
// A page-table page contains 512 个 64-bit 的 PTEs.
// 为什么有 512 个 PTE ? 因为一个页表索引有 9 位， 2^9 = 512
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 64 位虚拟地址结构： | 闲置 25 位 | root 页表 9 位 | mid 页表 9 位 | final 页表 9 位 | offset 12 位置 | = 64 位
// PTE 结构： | 物理地址 PPN 44 位 | flags 10 位 | = 54 位
// 合成物理地址： | PPN 44 位置 | offset 12 位置| = 56 位

// 为虚拟地址寻找对应的 PTE 的地址
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  // 如果该虚拟地址超过了合法范围，直接异常
  if(va >= MAXVA)
    panic("walk");

  // 三级页表索引
  for(int level = 2; level > 0; level --) {
    // 定义一个 PTE 指针变量
    // #define PX(level, va) ((((uint64) (va)) >> (12 + (9 * (level))) & 0x1ff)
    // PX 操作后会获取不同 level 页表的 9 位索引
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      // 该 PTE valid
      pagetable = (pagetable_t)PTE2PA(*pte);     // 将 PTE 内容 >> 10 位再 << 12 位 获得物理地址
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)   // boot 的时候为二级和三级页表都分配一页
        return 0;
      memset(pagetable, 0, PGSIZE);
      // #define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
      // 将刚分配的物理地址 >> 12 再 << 10 | flags 获得 pte 的内容
      *pte = PA2PTE(pagetable) | PTE_V; 
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 根据 [虚拟地址] 查找 [物理地址]
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;

  // 将返回的 final table 页表中的 PTE 转换成物理地址格式
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.

// 在启动时调用，增加一个到内核页表的映射
// 入参：页表，虚拟地址，物理地址，大小，权限
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.

// 为新的映射注册 PTE
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be page-aligned. The mappings must exist.
// Optionally free the physical memory.

// 从虚拟地址 va 开始，删除 n 页的映射
// 自动删除这些物理内存

void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.

// 创建一个新的用户级页表
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();   // 为页表分配 4KB 物理内存
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);         // 逐字节初始化为 0
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.

// 加载用户的初始化代码
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to newsz, which need not be page aligned.
// Returns new size or 0 on error.

// 为进程的内存增长分配 PTE 和物理内存
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to newsz.
// oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.

// 释放用户的页
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 递归地释放页表，从最低地 level 开始
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.

  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];   // 拿到这个 PTE

    // 检查 Valid, Readable, Writable, Executable
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){

      // this PTE points to a lower-level page table.
      // 存在后继结点，获取后继节点的物理地址

      uint64 child = PTE2PA(pte);     //  右移 10 位 (消除 flags)，左移 12 位 (补0) 获取后继的开头地址
      freewalk((pagetable_t)child);   // 开始递归
      pagetable[i] = 0;               // 释放该层
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages, then free page-table pages.
// 释放用户的页，然后释放页表
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
// 标记某个 PTE 对于用户不可访问
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
// 拷贝 内核 -> 用户
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
// 拷贝 用户 -> 内核
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

// lab3 - Print a page table
// typedef uint64 pte_t;        // uint64 = unsigned long = 8 byte，一个 PTE 8 字节
// typedef uint64 *pagetable_t; // 512 PTEs
// Each PTE line shows the PTE index in its page-table page, the pte bits, and the physical address extracted from the PTE. 


void
printwalk(pagetable_t pagetable, int level)
{
  for (int i = 0; i < 512; ++ i) {

    // 获取当前 PTE
    pte_t pte = pagetable[i];

    // level
    if (pte & PTE_V) {
      // level
      if (level == 2)
        printf("..");
      else if(level == 1)
        printf(".. ..");
      else
        printf(".. .. ..");

      // index
      printf("%d: ", i);

      // PTE context & PPN
      uint64 children = PTE2PA(pte);
      printf("pte %p pa %p\n", pte, children);
    
      // 开始递归
      if(level != 0) {
        printwalk((pagetable_t)children, level - 1);
      }
    }
  }
}

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  printwalk(pagetable, 2);
}
