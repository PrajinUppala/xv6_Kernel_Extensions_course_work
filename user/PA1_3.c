#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    printf("---- FORK SYSCALL TEST ----\n");

    int pid = fork();

    if(pid == 0){
        int c = getsyscount();
        printf("Child syscall count: %d\n", c);
        exit(0);
    }
    else{
        wait(0);
        int p = getsyscount();
        printf("Parent syscall count: %d\n", p);
    }

    exit(0);
}
