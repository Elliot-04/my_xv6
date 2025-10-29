#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int flags;
  if (argaddr(0, &p) < 0 || argint(1, &flags) < 0) return -1;
  return wait(p, flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

uint64 sys_yield(void) {
  struct proc *p = myproc();
  uint64 context_start = (uint64)&p->context;
  uint64 context_end = context_start + sizeof(struct context);

  // 打印当前进程上下文保存的地址范围
  printf("Save the context of the process to the memory region from address %p to %p\n", context_start, context_end);
  // 打印当前进程的pid和用户态pc值
  printf("Current running process pid is %d and user pc is %p\n", p->pid, p->trapframe->epc);

  int current_idx = p - proc; // 计算当前进程在proc[]中的索引
  for (int i = 1; i < NPROC; i++) {
    int next_idx = (current_idx + i) % NPROC;
    struct proc *np = &proc[next_idx];
    acquire(&np->lock);
    if (np->state == RUNNABLE) {
      printf("Next runnable process pid is %d and user pc is %p\n", np->pid, np->trapframe->epc);
      release(&np->lock);
      break; 
    }
    release(&np->lock);
  }

  // 调用内核已有的yield()，真正让出CPU
  yield();
  return 0;
}