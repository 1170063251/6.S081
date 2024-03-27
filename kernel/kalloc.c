// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages./物理内存分配器，用于用户进程;
//内核栈，页表页
//和管道缓冲区。分配整个4096字节的页面。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

//uint64 rest_memory=0;//剩余
//uint64 total_memory=0;//分配的
void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
                  
struct run //一个链表
{
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");//初始化了一个自旋锁
  freerange(end, (void*)PHYSTOP);//将从end到物理内存的末尾的内存范围添加到空闲列表
}

void
freerange(void *pa_start, void *pa_end)//从start到end将空闲的页面加入到空闲列表
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
kfree(void *pa)//释放一个已经分配的物理页
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
  //total_memory-=PGSIZE;//
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)//分配一页物理页
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
    //total_memory+=PGSIZE;//
  return (void*)r;

}

uint64 get_free_memory(void)
{
  uint64 mem_bytes = 0;
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r)
  {
    mem_bytes+=PGSIZE;
    r=r->next;
  }
   release(&kmem.lock);
   return mem_bytes;
 
}