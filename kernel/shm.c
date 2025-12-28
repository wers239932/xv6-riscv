//
// Shared memory implementation
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "shm.h"

// Global shared memory table
struct shm_object shm_table[MAX_SHM_OBJECTS];
struct spinlock shm_table_lock;

// Initialize shared memory subsystem
void
shm_init(void)
{
  initlock(&shm_table_lock, "shm_table");
  
  for(int i = 0; i < MAX_SHM_OBJECTS; i++) {
    shm_table[i].valid = 0;
    shm_table[i].ref = 0;
    shm_table[i].size = 0;
    shm_table[i].pages = 0;
    shm_table[i].npages = 0;
    shm_table[i].unlinked = 0;
    memset(shm_table[i].name, 0, SHM_NAME_MAX);
    initlock(&shm_table[i].lock, "shm_object");
  }
}

// Create a new shared memory object
struct shm_object*
shm_create(char *name, uint64 size)
{
  struct shm_object *shm = 0;
  int i;
  
  if(strlen(name) >= SHM_NAME_MAX)
    return 0;
    
  if(size == 0 || size > 1024 * PGSIZE) // Limit to 4MB
    return 0;
    
  acquire(&shm_table_lock);
  
  // Check if object already exists (and is not unlinked)
  for(i = 0; i < MAX_SHM_OBJECTS; i++) {
    if(shm_table[i].valid && !shm_table[i].unlinked && 
       strncmp(shm_table[i].name, name, SHM_NAME_MAX) == 0) {
      shm = &shm_table[i];
      shm_ref_inc(shm);
      release(&shm_table_lock);
      return shm;
    }
  }
  
  // Find empty slot
  for(i = 0; i < MAX_SHM_OBJECTS; i++) {
    if(!shm_table[i].valid) {
      shm = &shm_table[i];
      break;
    }
  }
  
  if(!shm) {
    release(&shm_table_lock);
    return 0;
  }
  
  acquire(&shm->lock);
  
  // Initialize the object
  strncpy(shm->name, name, SHM_NAME_MAX - 1);
  shm->name[SHM_NAME_MAX - 1] = 0;
  shm->size = size;
  shm->npages = PGROUNDUP(size) / PGSIZE;
  shm->ref = 1;
  shm->valid = 1;
  shm->unlinked = 0;
  
  // Allocate physical pages
  shm->pages = kalloc();
  if(!shm->pages) {
    shm->valid = 0;
    release(&shm->lock);
    release(&shm_table_lock);
    return 0;
  }
  
  // For multi-page objects, allocate additional pages
  if(shm->npages > 1) {
    void *first_page = shm->pages;
    void **page_ptrs = (void**)first_page;
    
    for(uint64 p = 1; p < shm->npages; p++) {
      void *page = kalloc();
      if(!page) {
        // Cleanup on failure
        for(uint64 cleanup = 1; cleanup < p; cleanup++) {
          krefdec(page_ptrs[cleanup - 1]);
        }
        krefdec(first_page);
        shm->valid = 0;
        release(&shm->lock);
        release(&shm_table_lock);
        return 0;
      }
      memset(page, 0, PGSIZE);
      if(p == 1) {
        page_ptrs[0] = page;
      } else {
        page_ptrs[p - 1] = page;
      }
    }
  }
  
  memset(shm->pages, 0, PGSIZE);
  
  release(&shm->lock);
  release(&shm_table_lock);
  
  return shm;
}

// Lookup shared memory object by name
struct shm_object*
shm_lookup(char *name)
{
  struct shm_object *shm = 0;
  
  acquire(&shm_table_lock);
  
  for(int i = 0; i < MAX_SHM_OBJECTS; i++) {
    if(shm_table[i].valid && !shm_table[i].unlinked && 
       strncmp(shm_table[i].name, name, SHM_NAME_MAX) == 0) {
      shm = &shm_table[i];
      shm_ref_inc(shm);
      break;
    }
  }
  
  release(&shm_table_lock);
  return shm;
}

// Increment reference count
void
shm_ref_inc(struct shm_object *shm)
{
  if(!shm)
    return;
    
  acquire(&shm->lock);
  shm->ref++;
  release(&shm->lock);
}

// Decrement reference count and cleanup if needed
void
shm_ref_dec(struct shm_object *shm)
{
  if(!shm)
    return;
    
  acquire(&shm->lock);
  shm->ref--;
  
  // Only cleanup if both unlinked AND no references
  if(shm->ref == 0 && shm->unlinked) {
    // Free physical pages
    if(shm->pages) {
      if(shm->npages == 1) {
        krefdec(shm->pages);
      } else {
        void **page_ptrs = (void**)shm->pages;
        for(uint64 p = 1; p < shm->npages; p++) {
          krefdec(page_ptrs[p - 1]);
        }
        krefdec(shm->pages);
      }
    }
    
    // Mark as invalid and clear all fields
    shm->valid = 0;
    shm->pages = 0;
    shm->size = 0;
    shm->npages = 0;
    shm->unlinked = 0;
    memset(shm->name, 0, SHM_NAME_MAX);
  }
  
  release(&shm->lock);
}

// Unlink shared memory object
int
shm_unlink(char *name)
{
  struct shm_object *shm = 0;
  
  acquire(&shm_table_lock);
  
  for(int i = 0; i < MAX_SHM_OBJECTS; i++) {
    if(shm_table[i].valid && !shm_table[i].unlinked && 
       strncmp(shm_table[i].name, name, SHM_NAME_MAX) == 0) {
      shm = &shm_table[i];
      break;
    }
  }
  
  if(!shm) {
    release(&shm_table_lock);
    return -1;
  }
  
  acquire(&shm->lock);
  
  // Mark as unlinked (remove from namespace)
  shm->unlinked = 1;
  
  // If no references, cleanup immediately
  if(shm->ref == 0) {
    if(shm->pages) {
      if(shm->npages == 1) {
        krefdec(shm->pages);
      } else {
        void **page_ptrs = (void**)shm->pages;
        for(uint64 p = 1; p < shm->npages; p++) {
          krefdec(page_ptrs[p - 1]);
        }
        krefdec(shm->pages);
      }
    }
    shm->valid = 0;
    shm->pages = 0;
    shm->size = 0;
    shm->npages = 0;
    shm->unlinked = 0;
    memset(shm->name, 0, SHM_NAME_MAX);
  }
  
  release(&shm->lock);
  release(&shm_table_lock);
  
  return 0;
}

// Map shared memory pages into process address space
int
shm_map_pages(pagetable_t pagetable, uint64 va, struct shm_object *shm, int perm)
{
  if(!shm || !shm->valid)
    return -1;
    
  acquire(&shm->lock);
  
  if(shm->npages == 1) {
    // Single page mapping
    if(mappages(pagetable, va, PGSIZE, (uint64)shm->pages, perm) != 0) {
      release(&shm->lock);
      return -1;
    }
    krefinc(shm->pages);
  } else {
    // Multi-page mapping
    void **page_ptrs = (void**)shm->pages;
    
    // Map first page
    if(mappages(pagetable, va, PGSIZE, (uint64)shm->pages, perm) != 0) {
      release(&shm->lock);
      return -1;
    }
    krefinc(shm->pages);
    
    // Map additional pages
    for(uint64 p = 1; p < shm->npages; p++) {
      if(mappages(pagetable, va + p * PGSIZE, PGSIZE, (uint64)page_ptrs[p - 1], perm) != 0) {
        // Cleanup on failure
        for(uint64 cleanup = 0; cleanup < p; cleanup++) {
          uvmunmap(pagetable, va + cleanup * PGSIZE, 1, 1);
        }
        release(&shm->lock);
        return -1;
      }
      krefinc(page_ptrs[p - 1]);
    }
  }
  
  release(&shm->lock);
  return 0;
}

// Unmap shared memory pages from process address space
void
shm_unmap_pages(pagetable_t pagetable, uint64 va, struct shm_object *shm)
{
  if(!shm || !shm->valid)
    return;
    
  acquire(&shm->lock);
  
  // Unmap all pages
  uvmunmap(pagetable, va, shm->npages, 1);
  
  release(&shm->lock);
}

// Read from shared memory object
int
shm_read(struct shm_object *shm, uint64 addr, uint off, int n)
{
  if(!shm || !shm->valid)
    return -1;
    
  acquire(&shm->lock);
  
  if(off >= shm->size) {
    release(&shm->lock);
    return 0;
  }
  
  if(off + n > shm->size)
    n = shm->size - off;
    
  if(n <= 0) {
    release(&shm->lock);
    return 0;
  }
  
  int bytes_read = 0;
  uint64 page_off = off % PGSIZE;
  uint64 page_idx = off / PGSIZE;
  
  while(bytes_read < n && page_idx < shm->npages) {
    void *page_addr;
    
    if(page_idx == 0) {
      page_addr = shm->pages;
    } else {
      void **page_ptrs = (void**)shm->pages;
      page_addr = page_ptrs[page_idx - 1];
    }
    
    uint64 bytes_in_page = PGSIZE - page_off;
    uint64 bytes_to_read = n - bytes_read;
    if(bytes_to_read > bytes_in_page)
      bytes_to_read = bytes_in_page;
      
    if(copyout(myproc()->pagetable, addr + bytes_read, 
               (char*)page_addr + page_off, bytes_to_read) < 0) {
      release(&shm->lock);
      return -1;
    }
    
    bytes_read += bytes_to_read;
    page_idx++;
    page_off = 0; // After first page, start from beginning
  }
  
  release(&shm->lock);
  return bytes_read;
}

// Write to shared memory object
int
shm_write(struct shm_object *shm, uint64 addr, uint off, int n)
{
  if(!shm || !shm->valid)
    return -1;
    
  acquire(&shm->lock);
  
  if(off >= shm->size) {
    release(&shm->lock);
    return -1;
  }
  
  if(off + n > shm->size)
    n = shm->size - off;
    
  if(n <= 0) {
    release(&shm->lock);
    return 0;
  }
  
  int bytes_written = 0;
  uint64 page_off = off % PGSIZE;
  uint64 page_idx = off / PGSIZE;
  
  while(bytes_written < n && page_idx < shm->npages) {
    void *page_addr;
    
    if(page_idx == 0) {
      page_addr = shm->pages;
    } else {
      void **page_ptrs = (void**)shm->pages;
      page_addr = page_ptrs[page_idx - 1];
    }
    
    uint64 bytes_in_page = PGSIZE - page_off;
    uint64 bytes_to_write = n - bytes_written;
    if(bytes_to_write > bytes_in_page)
      bytes_to_write = bytes_in_page;
      
    if(copyin(myproc()->pagetable, (char*)page_addr + page_off,
              addr + bytes_written, bytes_to_write) < 0) {
      release(&shm->lock);
      return -1;
    }
    
    bytes_written += bytes_to_write;
    page_idx++;
    page_off = 0; // After first page, start from beginning
  }
  
  release(&shm->lock);
  return bytes_written;
}