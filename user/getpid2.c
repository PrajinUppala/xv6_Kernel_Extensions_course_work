#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void){
    printf("PID of calling process: %d\n", getpid2());
    exit(0);
}