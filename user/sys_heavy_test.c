#include "kernel/types.h"
#include "user/user.h"

void syscall_work(int id)
{
  struct mlfqinfo info;

  for(int round = 0; round < 2; round++){

    // syscall-heavy workload
    for(int i = 0; i < 300000; i++){
      getpid();
    }

    getmlfqinfo(getpid(), &info);

    printf("SYS[%d] PID %d Round %d\n", id, getpid(), round);
    printf("Level: %d\n", info.level);
    printf("Ticks: %d %d %d %d\n",
           info.ticks[0],
           info.ticks[1],
           info.ticks[2],
           info.ticks[3]);

    // small delay so outputs don't collide too much
    pause(10);
  }

  exit(0);
}

int main()
{
  int n = 5;

  for(int i = 0; i < n; i++){
    int pid = fork();
    if(pid == 0){
      syscall_work(i);
    }
  }

  for(int i = 0; i < n; i++)
    wait(0);

  printf("\nSyscall-heavy test finished\n");

  exit(0);
}