#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void){
    int pid;
    pid = fork();
    if(pid==0){     // sleeping child
        pause(100);
        exit(0);
    }
    pid = fork();
    if(pid==0){     // running child
        while(1);
    }
    pid = fork();
    if(pid==0){     // zombie child
        pause(1);
        exit(0);
    }
    pause(10);
    printf("Number of children: %d\n", getnumchild());  // should print 2
    wait(0);    // clean up zombie
    exit(0);
}