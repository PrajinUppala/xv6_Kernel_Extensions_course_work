#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    printf("---- SYSCALL COUNT TEST ----\n");

    int before = getsyscount();
    printf("Initial syscall count: %d\n", before);

    // Make known number of syscalls
    getpid();
    getpid();
    pause(1);
    getpid();

    int after = getsyscount();
    printf("After syscalls: %d\n", after);

    if(after >= before + 4)
        printf("Syscall counter working\n");
    else
        printf("Syscall counter WRONG\n");

    exit(0);
}
