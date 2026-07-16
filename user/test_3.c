#include "kernel/types.h"
#include "user/user.h"

#define PAGES 1000

struct vmstats info;

int main()
{
    printf("=== Test 3: Priority-Based Eviction ===\n");

    int pid = fork();

    if(pid == 0){
        // LOW priority (CPU-bound)
        char *p = sbrk(PAGES * 4096);

        for(int i = 0; i < PAGES; i++){
            for(int j = 0; j < 1000000; j++); // CPU heavy
            p[i * 4096] = i;
        }

        getvmstats(getpid(), &info);
        printf("Child Evicted: %d\n", info.pages_evicted);

        exit(0);
    } else {
        // HIGH priority (syscall heavy)
        char *p = sbrk(PAGES * 4096);

        for(int i = 0; i < PAGES; i++){
            getpid(); // syscall heavy
            p[i * 4096] = i;
        }

        getvmstats(getpid(), &info);
        printf("Parent Evicted: %d\n", info.pages_evicted);

        wait(0);
    }

    printf("Test 3 Complete\n");
    exit(0);
}