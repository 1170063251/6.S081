// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NUM_HASHBUCKET 13
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NUM_HASHBUCKET)
//得到一个哈希密钥
struct {
  struct spinlock atom_lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;

//增加哈希桶
struct buf bufmap[NUM_HASHBUCKET];
struct spinlock bufmap_locks[NUM_HASHBUCKET];

} bcache;

void
binit(void)
{
  // struct buf *b;

  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  
  for(int i=0;i<NUM_HASHBUCKET;i++)//初始化哈希桶
  {
    initlock(&bcache.bufmap_locks[i],"bache_bufmap");
    bcache.bufmap->next=0;
  }
  for(int i=0;i<NBUF;i++ )
  {
    struct buf *b=&bcache.buf[i];
    initsleeplock(&b->lock,"buffer");
    b->lastuse=0;
    b->refcnt=0;
    //将所有buffer都加入道map0中
    b->next=bcache.bufmap[0].next;
    bcache.bufmap[0].next=b;
  }
  initlock(&bcache.atom_lock,"atom_lock");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  // struct buf *b;

  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // panic("bget: no buffers");

  struct buf *b;

  uint key=BUFMAP_HASH(dev,blockno);
  acquire(&bcache.bufmap_locks[key]);//获取了桶的锁
//查找是否已经缓存了
  for(b=bcache.bufmap[key].next;b;b=b->next)
  {
    if(b->dev==dev&&b->blockno==blockno)
    {
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }
//为了防止循环等待，释放一下桶的锁
  release(&bcache.bufmap_locks[key]);
  //获得原子锁来处理缓存问题,保证原子操作
  acquire(&bcache.atom_lock);

  //需要再检查一边是否缓存了，因为在释放桶的锁和加上原子锁之间可能出现了其他cpu进行缓存

 for(b=bcache.bufmap[key].next;b;b=b->next)
  {
    if(b->dev==dev&&b->blockno==blockno)
    {
      acquire(&bcache.bufmap_locks[key]);//重新加锁，因为对引用次数进行了改变
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.atom_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  //如果还没有，这时候我们持有原子锁，不持有桶锁
  struct buf *before=0;
  uint holding_bucket=-1;
  for(int i=0;i<NUM_HASHBUCKET;i++)
  {
    acquire(&bcache.bufmap_locks[i]);
    int newfind=0;//在当前桶中时间戳最大的
    for(b=&bcache.bufmap[i];b->next;b=b->next)
    {
      if(b->next->refcnt==0&&(!before||b->next->lastuse<before->next->lastuse))
      {
        before=b;
        newfind=1;
      }
    }
    
    if(newfind==0)
    {
      release(&bcache.bufmap_locks[i]);
    }
    else//如果找到同时需要保持这个bucket的锁
    {
      if(holding_bucket!=-1)
      {
        release(&bcache.bufmap_locks[holding_bucket]);
      }
      holding_bucket=i;
    }
  }
  

  if(!before)
      {
        panic("bget: no buffers");
      }
  b=before->next;

  if(holding_bucket!=key)
    {
      before->next=b->next;
      release(&bcache.bufmap_locks[holding_bucket]);
      acquire(&bcache.bufmap_locks[key]);
      
      b->next=bcache.bufmap[key].next;
      bcache.bufmap[key].next=b;
    }

  b->dev=dev;
  b->blockno=blockno;
  b->valid=0;
  b->refcnt=1;
  release(&bcache.bufmap_locks[key]);
  release(&bcache.atom_lock);
  acquiresleep(&b->lock);
  return b;
}




// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key=BUFMAP_HASH(b->dev,b->blockno);

  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    b->lastuse=ticks;
  }
  
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key=BUFMAP_HASH(b->dev,b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key=BUFMAP_HASH(b->dev,b->blockno);
    acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
 release(&bcache.bufmap_locks[key]);
}


