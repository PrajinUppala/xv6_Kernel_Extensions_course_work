#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    printf("---- CHILD TEST ----\n");

    int pid1 = fork();
    if(pid1 == 0){
        pause(10);
        exit(0);
    }

    int pid2 = fork();
    if(pid2 == 0){
        pause(20);
        exit(0);
    }

    pause(5);

    int n = getnumchild();
    printf("Number of children (expected 2): %d\n", n);

    int sc = getchdsyscnt(pid1);
    printf("Child syscall count (valid child): %d\n", sc);

    int invalid = getchdsyscnt(9999);
    printf("Invalid child syscall (expected -1): %d\n", invalid);

    wait(0);
    wait(0);

    int n2 = getnumchild();
    printf("Number of children after wait (expected 0): %d\n", n2);

    exit(0);
}
