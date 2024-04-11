#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"


/*
 * the kernel's page table.现在是共享一个内核页表
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();//为内核根页表分配一个完整得页面
   memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));//将kvminit得到的内核页表根目录地址放入SATP寄存器，相当于打开了分页,自此之后虚拟地址需要经过MMU翻译
  sfence_vma();
}

pagetable_t
kvminit_new()
{
   
  pagetable_t k_pagetable = (pagetable_t) kalloc();
  if(k_pagetable==0)
  {
    return k_pagetable;
  }
  memset(k_pagetable,0,PGSIZE);
 // uart registers
  ukvmmap(k_pagetable,UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  ukvmmap(k_pagetable,VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  ukvmmap(k_pagetable,CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  ukvmmap(k_pagetable,PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  ukvmmap(k_pagetable,KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  ukvmmap(k_pagetable,(uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  ukvmmap(k_pagetable,TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return k_pagetable;
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
//返回页表pagetable中PTE的地址
//对应虚拟地址va。= 0,
//创建任何所需的页表页。
//
// risc-v Sv39方案有三个级别的页表
//页面。一个页表页面包含512个64位pte。
// 64位虚拟地址分为5个字段:
// 39 . .63——一定是0。
// 30 . .38—9位二级索引。
// 21 . .29—9位1级索引。
// 12 . .20—9位0级索引。
// 0 . .页面内的11—12位字节偏移量。
pte_t *//为虚拟地址找到PTE
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) //模拟三级页表，一位内第一级页表一定存在
  {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);//右移10位再左移12位，这里直接把物理地址赋给了虚拟地址，因为内核地址采用直接映射
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)//当且仅当低两级页表页(中间级、叶子级页表页)不存在且不需要分配时，walk函数会返回0
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
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
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
//向内核页表添加一个映射。
//仅在启动时使用。
//不刷新TLB或启用分页。
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}
//添加一个映射函数
void
ukvmmap(pagetable_t k_pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(k_pagetable, va, sz, pa, perm) != 0)
    panic("ukvmmap");
}




// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
//将内核虚拟地址转换为
//物理地址。只需要
//堆栈上的地址。
//假设va与页面对齐。
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  


  pte = walk(myproc()->k_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.将范围虚拟地址到同等范围物理地址的映射装载到一个页表中。
//它以页面大小为间隔，为范围内的每个虚拟地址单独执行此操作。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last; // a存储的是当前虚拟地址对应的页
  // last存放的是最后一个应设置的页
  // 当 a==last时，表示a已经设置完了所有页，完成了所有任务
  pte_t *pte;

  a = PGROUNDDOWN(va);// PGROUNDUP(sz)：sz大小的内存至少使用多少页才可以存下，返回的是下一个未使用页的地址
  last = PGROUNDDOWN(va + size - 1);// PGROUNDDOWN(a)：地址a所在页面是多少号页面，拉回所在页面开始地址
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)//被占用了
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)// 设置完当前页之后看看是否到达设置的最后一页，是则跳出循环
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
int k_mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)//考虑修改映射

{
 uint64 a, last; // a存储的是当前虚拟地址对应的页
  // last存放的是最后一个应设置的页
  // 当 a==last时，表示a已经设置完了所有页，完成了所有任务
  pte_t *pte;

  a = PGROUNDDOWN(va);// PGROUNDUP(sz)：sz大小的内存至少使用多少页才可以存下，返回的是下一个未使用页的地址
  last = PGROUNDDOWN(va + size - 1);// PGROUNDDOWN(a)：地址a所在页面是多少号页面，拉回所在页面开始地址
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    // if(*pte & PTE_V)//不考虑被占用
    //   panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)// 设置完当前页之后看看是否到达设置的最后一页，是则跳出循环
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.取消用户进程页中指定范围的映射关系 ，va页对齐
// Optionally free the physical memory.
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
    if(do_free)//释放物理内存
    {
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);//memmove(void *dst, const void *src, uint n)从容src地址拷贝n个字节到dst
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.向内核申请更多的内存
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz; 

  oldsz = PGROUNDUP(oldsz);//这里指向原来使用的下一页
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0)//建立映射
    {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 译：回收用户页，使得进程的内存大小从oldsz变为newsz。oldsz和newsz不一定要是
// 页对齐的，newsz也不一定要大于oldsz。oldsz可以比当前实际所占用的内存大小更大。
// 函数返回进程新占用的内存大小
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);//释放物理内存
  }

  return newsz;
}
// uint64
// kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
// {
//   if(newsz >= oldsz)
//     return oldsz;

//   if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
//     int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
//     uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);//不释放物理内存
//   }

//   return newsz;
// }



// Recursively free page-table pages.用来回收页表
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.里通过标志位的设置来判断是否到达了叶级页表
    // 如果有效位为1，且读位、写位、可执行位都是0
    // 说明这是一个高级别(非叶级)页表项，且此项未被释放，应该去递归地释放
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
//用来回收内核页表
void
freewalk_k_pagetable(pagetable_t k_pagetable)
{
  for(int i=0;i<512;i++)
  {
    pte_t pte = k_pagetable[i];
    if(pte & PTE_V)
    {
      k_pagetable[i]=0;
       if ((pte & (PTE_R|PTE_W|PTE_X)) == 0)
       {
        uint64 child=PTE2PA(pte);
        freewalk_k_pagetable((pagetable_t)child);
       }
    }
  }
  kfree((void*)k_pagetable);
}

// Free user memory pages,
// then free page-table pages.
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
// 译：给定一个父进程页表，将其内存拷贝到子进程页表中
// 同时拷贝页表和对应的物理内存
// 返回0表示成功，-1表示失败
// 失败时会释放所有已经分配的内存
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
// Return 0 on success, -1 on error.译：从用户空间向内核空间拷贝数据
// 从页表pagetable的虚拟地址srcva中拷贝len长度的数据到dst
// 成功时返回0，出错时返回-1

int
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
int
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)//dst 是内核地址，SRCVA是用地址
{
  // uint64 n, va0, pa0;

  // while(len > 0){
  //   va0 = PGROUNDDOWN(srcva);
  //   pa0 = walkaddr(pagetable, va0);
  //   if(pa0 == 0)
  //     return -1;
  //   n = PGSIZE - (srcva - va0);
  //   if(n > len)
  //     n = len;
  //   memmove(dst, (void *)(pa0 + (srcva - va0)), n);

  //   len -= n;
  //   dst += n;
  //   srcva = va0 + PGSIZE;
  // }
  // return 0;
  return copyin_new(pagetable,dst,srcva,len);

}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  // uint64 n, va0, pa0;
  // int got_null = 0;

  // while(got_null == 0 && max > 0){
  //   va0 = PGROUNDDOWN(srcva);
  //   pa0 = walkaddr(pagetable, va0);
  //   if(pa0 == 0)
  //     return -1;
  //   n = PGSIZE - (srcva - va0);
  //   if(n > max)
  //     n = max;

  //   char *p = (char *) (pa0 + (srcva - va0));
  //   while(n > 0){
  //     if(*p == '\0'){
  //       *dst = '\0';
  //       got_null = 1;
  //       break;
  //     } else {
  //       *dst = *p;
  //     }
  //     --n;
  //     --max;
  //     p++;
  //     dst++;
  //   }

  //   srcva = va0 + PGSIZE;
  // }
  // if(got_null){
  //   return 0;
  // } else {
  //   return -1;
  // }
  return copyinstr_new(pagetable,dst,srcva,max);
}


void vmprint_deep(pagetable_t pagetable,int deep)
{
  static char* deepneed[]=
  {
    "..",
    ".. ..",
    ".. .. .."
  };
  if(deep<0||deep>2)//
  {
    panic("只有三级页表");
  }
  for(int i=0;i<512;i++)
  {
    pte_t pte=pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0)//不是叶子
    {
      uint64 child = PTE2PA(pte);
      printf("%s %d: pte %p pa %p\n",deepneed[deep],i,pte,PTE2PA(pte));
      vmprint_deep((pagetable_t)child,deep+1);
    }
    else if(pte & PTE_V)
    {
       printf("%s %d: pte %p pa %p\n", deepneed[deep], i, pte, PTE2PA(pte));
    }
  }

}
void vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_deep(pagetable,0);

}

//将用户页表拷贝到内核页表
int
kvmcopy(pagetable_t pagetable,pagetable_t k_pagetabale,uint64 start,uint64 end)
{
   pte_t *pte;
   uint64 pa ,i;
   uint flags;
   start=PGROUNDUP(start);
   for(i=start;i<end;i+=PGSIZE)
   {
    if((pte = walk(pagetable, i, 0)) == 0)
      panic("kvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("kvmcopy: page not present");
    pa = PTE2PA(*pte);
    //将pte标志-u变为0
    flags = PTE_FLAGS(*pte)&(~PTE_U);
     if(k_mappages(k_pagetabale, i, PGSIZE, pa, flags) != 0)
     {
      goto err;
     }
   }
   return 0;
  err:
  uvmunmap(k_pagetabale,0,i/PGSIZE,1);
  return -1;
}
