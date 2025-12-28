//
// Shared memory objects definitions
//

#ifndef SHM_H
#define SHM_H

#include "types.h"
#include "spinlock.h"

#define SHM_NAME_MAX 32
#define MAX_SHM_OBJECTS 16

// Shared memory object structure
struct shm_object {
  char name[SHM_NAME_MAX];    // Name of the shared memory object
  int ref;                    // Reference count
  uint64 size;               // Size in bytes
  void *pages;               // Physical pages allocated
  uint64 npages;             // Number of pages
  struct spinlock lock;      // Protects this structure
  int valid;                 // 1 if object is valid
  int unlinked;              // 1 if object has been unlinked
};

// Global shared memory table
extern struct shm_object shm_table[MAX_SHM_OBJECTS];
extern struct spinlock shm_table_lock;

// Function prototypes
void shm_init(void);
struct shm_object* shm_create(char *name, uint64 size);
struct shm_object* shm_lookup(char *name);
void shm_ref_inc(struct shm_object *shm);
void shm_ref_dec(struct shm_object *shm);
int shm_unlink(char *name);
int shm_map_pages(pagetable_t pagetable, uint64 va, struct shm_object *shm, int perm);
void shm_unmap_pages(pagetable_t pagetable, uint64 va, struct shm_object *shm);

#endif // SHM_H