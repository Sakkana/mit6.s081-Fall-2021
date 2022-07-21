// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// ====================并发链表数据结构==================== 

// 一个结点，指向下一个自己这种类型的结点
struct run {
  struct run *next;
};

// 用 [自旋锁] 和 [链表] 管理空闲内存
struct {
  struct spinlock lock;   // 自旋锁防止并发访问出现竞态条件
  struct run *freelist;   // 空闲链表的头节点
} kmem;

// =========================================================

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// 申请分配 4096-byte 物理内存，返回一个供内核使用的指针（申请失败返回 0）
// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);  // 上锁

  r = kmem.freelist;    // 获得空闲链表头结点
  if(r)
    kmem.freelist = r->next;

  release(&kmem.lock);  // 解锁

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}

// lab2 sysinfo -> count free memory
uint64
sysinfo_free_mem()
{
  acquire(&kmem.lock);  // 上锁

  int free_mem = 0;
  struct run *r = kmem.freelist;        // 定义一个结点类型,获得空闲链表头结点
  while (r) {
    free_mem += PGSIZE;
    r = r->next;
  }
  
  release(&kmem.lock);  // 解锁

  return free_mem;
}