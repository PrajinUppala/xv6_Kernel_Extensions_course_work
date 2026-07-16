#include "kernel/types.h"
#include "user/user.h"

void cpu_work(int id)
{
  volatile int x = 0;

  for(int round = 0; round < 3; round++){

    for(long i = 0; i < 500000000; i++){
      x += i;
    }

    struct mlfqinfo info;
    getmlfqinfo(getpid(), &info);
    printf("CPU[%d] PID %d Round %d\n", id, getpid(), round);
    printf("Level: %d\n", info.level);
    printf("Ticks: %d %d %d %d\n",
           info.ticks[0],
           info.ticks[1],
           info.ticks[2],
           info.ticks[3]);
    pause(10);
  }
          
  exit(0);
}

int main()
{
  int n = 6;

  for(int i = 0; i < n; i++){
    if(fork() == 0){
      cpu_work(i);
    }
  }

  for(int i = 0; i < n; i++)
    wait(0);

  exit(0);
}