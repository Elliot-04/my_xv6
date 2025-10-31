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

struct {
  // struct spinlock lock;
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf head[NBUCKET];
} bcache;

// 哈希函数，将块号映射到桶索引
static inline int hash(uint blockno)
{
  return blockno % NBUCKET;
}

// 将一个缓冲区从其LRU链表中移除
static void bcache_remove(struct buf *b)
{
  b->next->prev = b->prev;
  b->prev->next = b->next;
}

// 将一个缓冲区插入到桶的LRU列表头部 (最近使用)
static void bcache_insert_head(int i, struct buf *b)
{
  b->next = bcache.head[i].next;
  b->prev = &bcache.head[i];
  bcache.head[i].next->prev = b;
  bcache.head[i].next = b;
}

void
binit(void)
{
  struct buf *b;
  int i;

  // 初始化每个桶的锁
  for (i = 0; i < NBUCKET; i++) {
    initlock(&bcache.lock[i], "bcache");
  }

  // 初始化每个桶的LRU列表
  for (i = 0; i < NBUCKET; i++) {
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // 初始时将所有缓冲区添加到第一个桶的列表中
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    bcache_insert_head(0, b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int i = hash(blockno);
  acquire(&bcache.lock[i]);

  // 该块是否已经缓存在这个桶中了？
  for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
    if (b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // 尝试分配该桶中的LRU缓冲区
  for (b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      // 从list当前位置移除并移动到头部
      bcache_remove(b);
      bcache_insert_head(i, b);
      release(&bcache.lock[i]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 这个桶中没有空闲缓冲区
  // 释放这个锁，尝试从其他桶窃取一个。
  release(&bcache.lock[i]);

  struct buf *victim = 0; // 指向牺牲品
  int victim_bucket = -1; // 牺牲品所在桶

  // 按序遍历其他桶，寻找牺牲品
  for (int j = 1; j < NBUCKET; j++) {
    int b_idx = (i + j) % NBUCKET; // 从下一个桶开始遍历
    acquire(&bcache.lock[b_idx]);

    for (b = bcache.head[b_idx].prev; b != &bcache.head[b_idx]; b = b->prev) {
      if (b->refcnt == 0) {
        victim = b;
        victim_bucket = b_idx;
        b->refcnt = 1;      // 标记为已分配
        bcache_remove(b);   // 从牺牲桶中分离
        release(&bcache.lock[b_idx]);
        goto victim_found;  // 找到牺牲品，停止搜索
      }
    }

    // 这个桶没有牺牲品，释放锁并尝试下一个
    release(&bcache.lock[b_idx]);
  }

victim_found:
  if (victim) {
    acquire(&bcache.lock[i]); // 获取接收桶的锁

    // 重新检查接收桶，是否仍需要接收牺牲品
    for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
      if (b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        release(&bcache.lock[i]);

        // 不再需要牺牲品，将其放回原处
        acquire(&bcache.lock[victim_bucket]);
        victim->refcnt = 0;
        bcache_insert_head(victim_bucket, victim);
        release(&bcache.lock[victim_bucket]);

        acquiresleep(&b->lock);
        return b;
      }
    }

    // 重新检查未命中，仍需要牺牲品
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    bcache_insert_head(i, victim); // 插入到接收桶list
    release(&bcache.lock[i]);
    acquiresleep(&victim->lock);
    return victim;
  }

  panic("bget: no buffers");
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

  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt--;

  if (b->refcnt == 0) {
    // no one is waiting for it.
    // 更新时间戳并插入到其桶list
    b->timestamp = ticks;
    bcache_remove(b);
    bcache_insert_head(i, b);
  }
  
  release(&bcache.lock[i]);
}

void
bpin(struct buf *b) {
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt++;
  release(&bcache.lock[i]);
}

void
bunpin(struct buf *b) {
  int i = hash(b->blockno);
  acquire(&bcache.lock[i]);
  b->refcnt--;
  release(&bcache.lock[i]);
}


