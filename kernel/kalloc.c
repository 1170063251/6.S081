// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STEALPAGE 16

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
//为每个cpu都创建一个锁
void
kinit()
{
  //initlock(&kmem.lock, "kmem");
  //freerange(end, (void*)PHYSTOP);
  static char lock_name[NCPU][7];
  for(int i=0;i<NCPU;i++)
  {
    snprintf(lock_name[i],7,"kmem_%d",i);
    initlock(&kmem[i].lock,lock_name[i]);
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

push_off();
int cpu=cpuid();

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  // struct run *r;

  // acquire(&kmem.lock);
  // r = kmem.freelist;
  // if(r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);

  // if(r)
  //   memset((char*)r, 5, PGSIZE); // fill with junk
  // return (void*)r;

  struct run *r;
  //关中断获取cpuid
  push_off();
  int cpu=cpuid();

  acquire(&kmem[cpu].lock);
  r=kmem[cpu].freelist;
  if(r)
  {
    kmem[cpu].freelist=r->next;
    release(&kmem[cpu].lock);
  }
  else//需要偷页
  {
    release(&kmem[cpu].lock);
    int steal_page=STEALPAGE;
    struct run *steal_list=0;//偷到的链表
    for(int i=0;i<NCPU;i++)
    {
      if(i!=cpu)
      {
        acquire(&kmem[i].lock);
        for(struct run *r=kmem[i].freelist;r&&steal_page;r=kmem[i].freelist)
        {
          kmem[i].freelist=r->next;
          r->next=steal_list;
          steal_list=r;
          steal_page--;
        }
        release(&kmem[i].lock);
        if(steal_page==0)
        {break;}
      }
    }
    if(steal_list!=0)
    {
      r=steal_list;
      acquire(&kmem[cpu].lock);
      kmem[cpu].freelist=r->next;
      release(&kmem[cpu].lock);
    }
  }
  pop_off();
  if(r)
  {
     memset((char*)r, 5, PGSIZE);
  }
  return (void*)r;


}


