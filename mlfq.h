#ifndef _MLFQ_H_
#define _MLFQ_H_

// Structure returned by getmlfqinfo syscall
struct mlfqinfo {
  int level;              // current queue level (0–3)
  int ticks[4];           // total ticks spent in each queue
  int times_scheduled;    // number of times scheduled
  int total_syscalls;     // total system calls made
};

#endif // _MLFQ_H_