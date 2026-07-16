#include "kernel/types.h"
#include "user/user.h"

struct mlfqinfo info;

int
main()
{
    printf("=== Test 3: getmlfqinfo + Edge Cases ===\n");

    int pid = getpid();

    // Generate activity
    for(int i = 0; i < 1000; i++){
        getpid(); // syscalls
        for(int j = 0; j < 10000; j++);
    }

    if(getmlfqinfo(pid, &info) < 0){
        printf("ERROR: getmlfqinfo failed\n");
        exit(1);
    }

    printf("Level: %d\n", info.level);
    printf("Times scheduled: %d\n", info.times_scheduled);
    printf("Total syscalls: %d\n", info.total_syscalls);

    for(int i = 0; i < 4; i++){
        printf("Ticks[%d]: %d\n", i, info.ticks[i]);
    }

    // Basic sanity checks
    if(info.total_syscalls == 0){
        printf("ERROR: syscall count not tracked\n");
    }

    // Invalid PID test
    if(getmlfqinfo(99999, &info) != -1){
        printf("ERROR: invalid PID not handled\n");
    } else {
        printf("Invalid PID handled correctly\n");
    }

    exit(0);
}