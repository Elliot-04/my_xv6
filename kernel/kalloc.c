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

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU]; // 为每个CPU核心维护一个空闲页链表和锁

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmems[i].lock, "kmem"); // 初始化每个CPU对应的空闲页链表锁
  }
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

  // 获取cpuid必须先关闭中断
  push_off();
  int cid = cpuid();
  pop_off();

  // 将待释放页添加到当前cpu空闲页链表头
  acquire(&kmems[cid].lock);
  r->next = kmems[cid].freelist;
  kmems[cid].freelist = r;
  release(&kmems[cid].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 获取cpuid必须先关闭中断
  push_off();
  int cid = cpuid();
  pop_off();

  // 先尝试从当前cpu空闲页链表分配页
  acquire(&kmems[cid].lock);
  r = kmems[cid].freelist;
  if(r)
    kmems[cid].freelist = r->next;
  release(&kmems[cid].lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    return (void*)r;
  }

  // 当前cpu空闲页链表为空，需要窃取其他cpu的空闲页
  for (int i = 0; i < NCPU; i++) {
    if (i == cid) continue; // 跳过当前cpu

    int target_cpu = i;
    acquire(&kmems[target_cpu].lock);
    r = kmems[target_cpu].freelist;
    if (r) {
      kmems[target_cpu].freelist = r->next;
    }
    release(&kmems[target_cpu].lock);
    if (r) {
      memset((char*)r, 5, PGSIZE);
      return (void*)r;
    }
  }

  // 所有cpu空闲页链表都为空
  return (void*)r;
}
