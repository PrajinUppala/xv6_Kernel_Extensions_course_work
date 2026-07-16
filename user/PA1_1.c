#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    printf("---- BASIC TEST ----\n");

    hello();

    int pid1 = getpid();
    int pid2 = getpid2();

    if(pid1 == pid2)
        printf("getpid2() correct\n");
    else
        printf("getpid2() WRONG\n");

    int ppid = getppid();
    printf("PID: %d  PPID: %d\n", pid1, ppid);

    exit(0);
}
