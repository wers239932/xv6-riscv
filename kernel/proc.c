#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define S2_NUM 2
#define S11_NUM 11
#define S2_TRAPFRAME_OFFSET 20

enum DUMP_ERROR {
  NO_PERMISSION_ERROR = -1,
  INVALID_PID_ERROR = -2,
  INVALID_REGISTER_ERROR = -3,
  RETURN_IO_ERROR = -4
};

struct cpu cpus[NCPU];

struct proc_wrapper head_wrap;

struct proc *initproc = 0;

int nextpid = 1;
struct spinlock pid_lock;
struct spinlock proc_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  
}

// initialize the proc table.
void
procinit(void)
{
    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    initlock(&proc_lock, "list_lock");

    head_wrap.proc = 0;
    head_wrap.prev_wrap = &head_wrap;
    head_wrap.next_wrap = &head_wrap;
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int
allocpid()
{
    int pid;
  
    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

void
wakeup_nolock(void *chan)
{
    struct proc_wrapper *wrap;
    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap) {
        struct proc *p = wrap->proc;
        if(p != myproc() && p->state == SLEEPING && p->chan == chan) {
            p->state = RUNNABLE;
        }
    }
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
    struct proc_wrapper *wrap;
    struct proc *p;

    int active_count = 0;
    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap) {
        if(wrap->proc && wrap->proc->state != UNUSED)
            active_count++;
    }
    if(active_count >= NPROC)
        return 0;

    acquire(&proc_lock);

    if (!(p = bd_malloc(sizeof(struct proc)))) {
        release(&proc_lock);
        return 0;
    }
    memset(p, 0, sizeof(struct proc));

    if (!(wrap = bd_malloc(sizeof(struct proc_wrapper)))) {
        bd_free(p);
        release(&proc_lock);
        return 0;
    }

    wrap->proc = p;
    wrap->prev_wrap = &head_wrap;
    wrap->next_wrap = head_wrap.next_wrap;
    head_wrap.next_wrap->prev_wrap = wrap;
    head_wrap.next_wrap = wrap;

    p->pid = allocpid();
    p->state = USED;

    if ((p->kstack = (uint64) kalloc()) == 0) {
        freeproc(p);
        bd_free(wrap);
        release(&proc_lock);
        return 0;
    }

    if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
        freeproc(p);
        bd_free(wrap);
        release(&proc_lock);
        return 0;
    }

    p->pagetable = proc_pagetable(p);
    if(p->pagetable == 0) {
        freeproc(p);
        bd_free(wrap);
        release(&proc_lock);
        return 0;
    }

    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
    struct proc_wrapper *wrap;

    if (p->kstack)
        kfree((void *)p->kstack);
    if (p->trapframe)
        kfree((void*)p->trapframe);
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);

    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap){
        if(wrap->proc == p){
            wrap->prev_wrap->next_wrap = wrap->next_wrap;
            wrap->next_wrap->prev_wrap = wrap->prev_wrap;
            bd_free(wrap);
            break;
        }
    }

    bd_free(p);
}

pagetable_t
proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

  // An empty page table.
    pagetable = uvmcreate();
    if(pagetable == 0)
        return 0;


  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
    if(mappages(pagetable, TRAMPOLINE, PGSIZE,
                (uint64)trampoline, PTE_R | PTE_X) < 0){
        uvmfree(pagetable, 0);
        return 0;
    }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
    if(mappages(pagetable, TRAPFRAME, PGSIZE,
                (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
    struct proc *p;

    p = allocproc();
    initproc = p;

  // allocate one user page and copy initcode's instructions
  // and data into it.
    uvmfirst(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&proc_lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
    uint64 sz;
    struct proc *p = myproc();

    sz = p->sz;
    if(n > 0){
        if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
            return -1;
    } else if(n < 0){
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
        return -1;
  }

  // Copy user memory from parent to child.
    if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
        freeproc(np);
        release(&proc_lock);
        return -1;
    }
    np->sz = p->sz;

  // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
    for(i = 0; i < NOFILE; i++)
        if(p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);
    safestrcpy(np->name, p->name, sizeof(np->name));

    pid = np->pid;
    np->parent = p;
    np->state = RUNNABLE;

    release(&proc_lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
    struct proc_wrapper *wrap;

    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap){
        struct proc *pp = wrap->proc;
        if(pp->parent == p){
            pp->parent = initproc;
            wakeup_nolock(initproc);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
    struct proc *p = myproc();

    if(p == initproc)
        panic("init exiting");

  // Close all open files.
    for(int fd = 0; fd < NOFILE; fd++){
        if(p->ofile[fd]){
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&proc_lock);
    reparent(p);
    wakeup_nolock(p->parent);

    p->xstate = status;
    p->state = ZOMBIE;

    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
    struct proc_wrapper *wrap;
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&proc_lock);

    for(;;){
    // Scan through table looking for exited children.
        havekids = 0;
        for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap){
            pp = wrap->proc;
            if(pp->parent == p){
                havekids = 1;
                if(pp->state == ZOMBIE){
                    pid = pp->pid;
                    if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                            sizeof(pp->xstate)) < 0) {
                        release(&proc_lock);
                        return -1;
                    }
                    freeproc(pp);
                    release(&proc_lock);
                    return pid;
                }
            }
        }

        if(!havekids || p->killed){
            release(&proc_lock);
            return -1;
        }

        sleep(p, &proc_lock);
    }
        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          int xstate = pp->xstate;
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          if(addr != 0)
            copyout(p->pagetable, addr, (char *)&xstate, sizeof(xstate));
          return pid;
        }

        sleep(p, &proc_lock);
    }
}


// Per-CPU process scheduler.
}

// Per-CPU process scheduler.
        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          int xstate = pp->xstate;
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          if(addr != 0)
            copyout(p->pagetable, addr, (char *)&xstate, sizeof(xstate));
          return pid;
        }

        sleep(p, &proc_lock);
    }
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
    struct proc_wrapper *wrap;
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;

    for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
        intr_on();
        int found = 0;

        for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap) {
            acquire(&proc_lock);
            p = wrap->proc;
            if(p->state == RUNNABLE){
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);
                c->proc = 0;
                found = 1;
            }
            release(&proc_lock);
        }

        if(found == 0){
            intr_on();
            asm volatile("wfi");
        }
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
    int intena;
    struct proc *p = myproc();

    if(!holding(&proc_lock))
        panic("sched list_lock");
    if(mycpu()->noff != 1)
        panic("sched locks");
    if(p->state == RUNNING)
        panic("sched running");
    if(intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
    struct proc *p = myproc();
    acquire(&proc_lock);
    p->state = RUNNABLE;
    sched();
    release(&proc_lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
    static int first = 1;
    release(&proc_lock);

    if(first){
        first = 0;
        fsinit(ROOTDEV);
        __sync_synchronize();
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    if(lk != &proc_lock){
        acquire(&proc_lock);
        release(lk);
    }

  // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

  // Tidy up.
    p->chan = 0;

    if(lk != &proc_lock){
        release(&proc_lock);
        acquire(lk);
    }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
    acquire(&proc_lock);
    wakeup_nolock(chan);
    release(&proc_lock);
}
// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
    struct proc_wrapper *wrap;
    struct proc *p;
    acquire(&proc_lock);

    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap){
        p = wrap->proc;
        if(p->pid == pid){
            p->killed = 1;
            if(p->state == SLEEPING)
                p->state = RUNNABLE;
            release(&proc_lock);
            return 0;
        }
    }

    release(&proc_lock);
    return -1;
}

void
setkilled(struct proc *p)
{
    acquire(&proc_lock);
    p->killed = 1;
    release(&proc_lock);
}

int
killed(struct proc *p)
{
    int k;
    acquire(&proc_lock);
    k = p->killed;
    release(&proc_lock);
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    struct proc *p = myproc();
    if(user_dst){
        return copyout(p->pagetable, dst, src, len);
    } else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    struct proc *p = myproc();
    if(user_src){
        return copyin(p->pagetable, dst, src, len);
    } else {
        memmove(dst, (char*)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
    static char *states[] = {
        [UNUSED]    "unused",
        [USED]      "used",
        [SLEEPING]  "sleep ",
        [RUNNABLE]  "runble",
        [RUNNING]   "run   ",
        [ZOMBIE]    "zombie"
    };

    struct proc_wrapper *wrap;
    struct proc *p;
    char *state;

    printf("\n");
    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap){
        p = wrap->proc;
        if(p->state == UNUSED)
            continue;
        if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s\n", p->pid, state, p->name);
    }
}

int dump(void) {
    struct proc *p = myproc();
    uint64 *regs = (uint64*)p->trapframe;
    for (int i = 22; i < 32; i ++) {
        printf("s%d = %d\n", i-20, (uint32)regs[i]);
    }
    return 0;
}

int dump2(int pid, int register_num, uint64* return_value) {
    if (register_num < 2 || register_num > 11) {
        return -3;
    }

    struct proc *curr = myproc();
    int pid_exists = 0;
    struct proc_wrapper *wrap;
    struct proc *p;

    for(wrap = head_wrap.next_wrap; wrap != &head_wrap; wrap = wrap->next_wrap) {
        p = wrap->proc;
        if (p->pid == pid) {
            pid_exists = 1;
            break;
        }
    }
    if (!pid_exists) return -2;
    if (p->parent->pid != curr->pid && p->pid != curr->pid) return -1;

    uint64 *reg = &(p->trapframe->s2) + register_num - 2;
    if (copyout(curr->pagetable, *return_value, (char *)reg, sizeof(uint64)) < 0) {
        return -4;
    }
    
    return 0;
}
