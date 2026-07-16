#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(void){
    printf("Parent PID: %d\n",getppid());
    exit(0);
}