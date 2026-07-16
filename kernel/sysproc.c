#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "mlfq.h"
#include "virtio.h"

extern struct spinlock wait_lock;
extern struct proc proc[NPROC];

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
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
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  int n;
  struct proc *p = myproc();

  argint(0, &n);

  uint64 addr = p->sz;

  if(n < 0){
    if(growproc(n) < 0)
      return -1;
  } else {
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;

    p->sz += n;   // LAZY
  }

  return addr;
}

uint64
sys_pause(void)
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
  return kkill(pid);
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

uint64 sys_hello(void)
{
  printf("Hello from the kernel!\n");
  return 0;
}

uint64 sys_getpid2(void)
{
  return myproc()->pid;
}

uint64 sys_getppid(void)
{
  struct proc *p = myproc();
  if(p->parent==0){
    return -1;
  }
  else{
    return p->parent->pid;
  }
}

uint64 sys_getnumchild(void)
{
  struct proc *p = myproc();
  struct proc *pp;
  uint64 count = 0;
  acquire(&wait_lock);
  for(pp = proc; pp < &proc[NPROC]; pp++){
    acquire(&pp->lock);
    if(pp->parent==p && pp->state!=ZOMBIE){
      count++;
    }
    release(&pp->lock);
  }
  release(&wait_lock);
  return count;
}

uint64 sys_getsyscount(void)
{
  struct proc *p = myproc();
  return p->syscount;
}

uint64 sys_getchdsyscnt(void)
{
  struct proc *p = myproc();
  struct proc *pp;
  int total = 0;
  int found = 0;
  int pid_child;
  argint(0, &pid_child);

  acquire(&wait_lock);
  for(pp = proc; pp < &proc[NPROC]; pp++){
    acquire(&pp->lock);
    if(pp->parent==p && pp->pid==pid_child && pp->state!=ZOMBIE){
      total = pp->syscount;
      found = 1;
      release(&pp->lock);
      break;
    }
    release(&pp->lock);
  }
  release(&wait_lock);
  if(!found){
    return -1;
  }
  return total;
}

uint64 sys_getlevel(void)
{
  struct proc *p = myproc();
  return p->level;
}

uint64
sys_getmlfqinfo(void)
{
  int pid;
  uint64 addr;
  struct proc *p;
  struct mlfqinfo info;

  argint(0, &pid);
  argaddr(1, &addr);

  extern struct proc proc[NPROC];

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);

    if(p->pid == pid){

      info.level = p->level;

      for(int i = 0; i < 4; i++)
        info.ticks[i] = p->queue_ticks[i];

      info.times_scheduled = p->times_scheduled;
      info.total_syscalls = p->syscount;

      release(&p->lock);

      if(copyout(myproc()->pagetable, addr, (char*)&info, sizeof(info)) < 0)
        return -1;

      return 0;
    }

    release(&p->lock);
  }

  return -1;
}

extern struct proc proc[NPROC];

uint64
sys_getvmstats(void)
{
  int pid;
  uint64 addr;
  struct proc *p;
  struct vmstats st;
  int found = 0;

  argint(0, &pid);
  argaddr(1, &addr);
  
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      st.page_faults = p->page_faults;
      st.pages_evicted = p->pages_evicted;
      st.pages_swapped_in = p->pages_swapped_in;
      st.pages_swapped_out = p->pages_swapped_out;
      st.resident_pages = p->resident_pages;
      found = 1;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }

  if(!found)
    return -1;

  if(copyout(myproc()->pagetable, addr, (char*)&st, sizeof(st)) < 0)
    return -1;

  return 0;
}

uint64
sys_setraidmode(void)
{
  int mode;
  argint(0, &mode);

  if(mode != 0 && mode != 1 && mode != 2)
    return -1;

  set_raid_mode(mode);
  return 0;
}
int disk_sched_policy = 0; // 0 = FCFS, 1 = SSTF

uint64
sys_setdisksched(void)
{
  int policy;
  argint(0, &policy);

  if(policy != 0 && policy != 1)
    return -1;

  disk_sched_policy = policy;
  return 0;
}
// struct disk_stats{
//   int reads;
//   int writes;
//   int avg_latency;
//   int total_latency;
//   int total_requests;
// };
extern struct disk_stats d_stats;
uint64
sys_getdiskstats(void)
{  int pid;
  uint64 addr;
  struct proc *p;
  struct disk_stats st;
  int found = 0;

  argint(0, &pid);
  argaddr(1, &addr);
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      st.reads = d_stats.reads;
      st.writes = d_stats.writes;
      if(d_stats.reads + d_stats.writes > 0){
        st.avg_latency =
          d_stats.total_latency / (d_stats.reads + d_stats.writes);
      } else {
        st.avg_latency = 0;
      }
      found = 1;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }
  if(!found)
    return -1;

  if(copyout(myproc()->pagetable, addr, (char*)&st, sizeof(st)) < 0)
    return -1;

  return 0;
}