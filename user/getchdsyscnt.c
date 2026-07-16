#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void){
  int pid1, pid2;
    // Child 1: stays alive and makes syscalls
    pid1 = fork();
    if(pid1 == 0){
      getpid();
      getpid();
      getpid();
      pause(100);
      exit(0);
    }
    // Child 2: exits immediately (becomes zombie)
    pid2 = fork();
    if(pid2 == 0){
      getpid();
      exit(0);
    }

    // Give time for child 2 to exit
    pause(10);

    // Test syscall count of live child
    int cnt1 = getchdsyscnt(pid1);
    printf("Child %d syscall count: %d\n", pid1, cnt1);

    // Test syscall count of zombie child
    int cnt2 = getchdsyscnt(pid2);
    printf("Child %d syscall count (zombie): %d\n", pid2, cnt2);

    // Test invalid PID
    int cnt3 = getchdsyscnt(9999);
    printf("Invalid PID syscall count: %d\n", cnt3);

    // Reap children
    wait(0);
    wait(0);

    exit(0);
}
