#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    printf("=== Test 2: Priority Boost & Fairness ===\n");

    for(int i = 0; i < 3; i++){
        if(fork() == 0){
            // Make them CPU heavy so they get demoted
            for(int j = 0; j < 1000000000; j++);

            int lvl_before = getlevel();
            printf("Child %d level BEFORE boost: %d\n", getpid(), lvl_before);

            // Wait for boost (using pause)
            for(int k = 0; k < 200; k++){
                pause(1);
            }

            int lvl_after = getlevel();
            printf("Child %d level AFTER boost: %d\n", getpid(), lvl_after);

            if(lvl_after != 0){
                printf("ERROR: Boost failed for PID %d\n", getpid());
            }

            exit(0);
        }
    }

    for(int i = 0; i < 3; i++)
        wait(0);

    exit(0);
}