#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_dump(void)
{
  return dump();
}

extern struct proc proc[NPROC];

uint64
sys_dump2(void)
{
  int pid;
  int register_num;
  uint64 return_value_addr;
  uint64 return_value;
  struct proc *p;
  struct proc *current = myproc();
  
  argint(0, &pid);
  argint(1, &register_num);
  argaddr(2, &return_value_addr);
  
  if(register_num < 2 || register_num > 11)
    return -3;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->pid == pid) {
      int has_access = 0;
      
      if(p == current) {
        has_access = 1;
      } else {
        struct proc *parent = p->parent;
        while(parent != 0) {
          if(parent == current) {
            has_access = 1;
            break;
          }
          parent = parent->parent;
        }
      }
      
      if(!has_access) {
        release(&p->lock);
        return -1;
      }
      
      if(p->trapframe == 0) {
        release(&p->lock);
        return -2;
      }
      
      uint64* trapframe_regs[] = {
          &p->trapframe->s2,
          &p->trapframe->s3, 
          &p->trapframe->s4,
          &p->trapframe->s5,
          &p->trapframe->s6,
          &p->trapframe->s7,
          &p->trapframe->s8,
          &p->trapframe->s9,
          &p->trapframe->s10,
          &p->trapframe->s11
      };

      int reg_index = register_num - 2;
      return_value = *trapframe_regs[reg_index];
      
      release(&p->lock);
      
      if(copyout(current->pagetable, return_value_addr, (char *)&return_value, sizeof(return_value)) < 0)
        return -4;
      
      return 0;
    }
    release(&p->lock);
  }
  
  return -2;
}