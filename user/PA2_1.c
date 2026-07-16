#include "kernel/types.h"
#include "user/user.h"

void cpu_bound()
{
    for(int i = 0; i < 2000000000; i++);
    printf("[CPU] PID %d Level: %d\n", getpid(), getlevel());
}

void syscall_heavy()
{
    for(int i = 0; i < 2000; i++){
        getpid();
    }
    printf("[SYSCALL] PID %d Level: %d\n", getpid(), getlevel());
}

void mixed()
{
    for(int i = 0; i < 5000; i++){
        for(int j = 0; j < 100000; j++);
        getpid();
    }
    printf("[MIXED] PID %d Level: %d\n", getpid(), getlevel());
}

int
main()
{
    printf("=== Test 1: Core Scheduling Behavior ===\n");

    printf("Initial Level (parent): %d\n", getlevel());

    if(fork() == 0){
        cpu_bound();
        exit(0);
    }

    if(fork() == 0){
        syscall_heavy();
        exit(0);
    }

    if(fork() == 0){
        mixed();
        exit(0);
    }

    for(int i = 0; i < 3; i++)
        wait(0);

    exit(0);
}