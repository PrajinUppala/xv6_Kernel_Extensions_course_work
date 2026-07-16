#include "kernel/types.h"
#include "user/user.h"

void cpu_job(int id)
{
  volatile long x = 0;
  struct mlfqinfo info;

  for(int round = 0; round < 8; round++){

    // heavy CPU work
    for(long i = 0; i < 700000000; i++){
      x += i;
    }

    getmlfqinfo(getpid(), &info);

    printf("[CPU] PID %d Round %d | Level %d | Ticks %d %d %d %d\n",
           getpid(), round, info.level,
           info.ticks[0],
           info.ticks[1],
           info.ticks[2],
           info.ticks[3]);

    pause(20);   // allow boost to happen between prints
  }

  exit(0);
}

int main()
{
  printf("\n===== BOOST TEST START =====\n");

  int n = 3;

  for(int i = 0; i < n; i++){
    int pid = fork();
    if(pid == 0){
      cpu_job(i);
    }
  }

  for(int i = 0; i < n; i++)
    wait(0);

  printf("\n===== BOOST TEST FINISHED =====\n");

  exit(0);
}