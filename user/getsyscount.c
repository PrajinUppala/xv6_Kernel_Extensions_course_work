#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void){
    getpid();
    getpid();
    getpid();

    printf("System call count of the calling process: %d\n", getsyscount());
    exit(0);
}