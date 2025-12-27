// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct spinlock kreflock;
int kref_count[PHYSTOP / PGSIZE];

void
kinit()
{
  initlock(&kreflock, "ref_counter");
  bd_init((char*)PGROUNDUP((uint64)end), (void*)PHYSTOP);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  bd_free(pa);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  void *r = bd_malloc(PGSIZE);
  
  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&kreflock);
    kref_count[(uint64)r / PGSIZE] = 1;
    release(&kreflock);
  }
  return (void*)r;
}

void
krefinc(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return;
  
  acquire(&kreflock);
  kref_count[(uint64)pa / PGSIZE]++;
  release(&kreflock);
}

void
krefdec(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return;
  
  acquire(&kreflock);
  kref_count[(uint64)pa / PGSIZE]--;

  if(kref_count[(uint64)pa / PGSIZE] == 0) {
    kfree(pa);
  }
  release(&kreflock);
}

int
krefget(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return 0;
    
  int refs = kref_count[(uint64)pa / PGSIZE];

  return refs;
}
