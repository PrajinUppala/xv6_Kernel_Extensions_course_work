#include "kernel/types.h"
#include "user/user.h"

void cpu_job(int id)
{
  volatile long x = 0;
  struct mlfqinfo info;

  for(int round = 0; round < 4; round++){

    for(long i = 0; i < 600000000; i++)
      x += i;

    getmlfqinfo(getpid(), &info);

    printf("[CPU ] PID %d Round %d | Level %d | Ticks %d %d %d %d\n",
           getpid(), round, info.level,
           info.ticks[0], info.ticks[1],
           info.ticks[2], info.ticks[3]);

    pause(10);
  }

  exit(0);
}


void syscall_job(int id)
{
  struct mlfqinfo info;

  for(int round = 0; round < 4; round++){

    for(int i = 0; i < 400000; i++)
      getpid();

    getmlfqinfo(getpid(), &info);

    printf("[SYS ] PID %d Round %d | Level %d | Ticks %d %d %d %d\n",
           getpid(), round, info.level,
           info.ticks[0], info.ticks[1],
           info.ticks[2], info.ticks[3]);

    pause(10);
  }

  exit(0);
}


void mixed_job(int id)
{
  volatile long x = 0;
  struct mlfqinfo info;

  for(int round = 0; round < 4; round++){

    for(long i = 0; i < 200000000; i++)
      x += i;

    for(int i = 0; i < 100000; i++)
      getpid();

    getmlfqinfo(getpid(), &info);

    printf("[MIX ] PID %d Round %d | Level %d | Ticks %d %d %d %d\n",
           getpid(), round, info.level,
           info.ticks[0], info.ticks[1],
           info.ticks[2], info.ticks[3]);

    pause(10);
  }

  exit(0);
}


int main()
{
  printf("\n--- Mixed Workload Test ---\n");

  // CPU-bound processes
  for(int i = 0; i < 2; i++){
    if(fork() == 0)
      cpu_job(i);
  }

  // syscall-heavy processes
  for(int i = 0; i < 2; i++){
    if(fork() == 0)
      syscall_job(i);
  }

  // mixed workload processes
  for(int i = 0; i < 2; i++){
    if(fork() == 0)
      mixed_job(i);
  }

  for(int i = 0; i < 6; i++)
    wait(0);

  printf("\n--- Mixed workload test finished ---\n");

  exit(0);
}