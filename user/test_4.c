#include "kernel/types.h"
#include "user/user.h"

struct vmstats info;

int main()
{
    printf("=== Test 4: Invalid PID ===\n");

    if(getvmstats(99999, &info) != -1){
        printf("ERROR: Invalid PID not handled\n");
    } else {
        printf("Invalid PID handled correctly\n");
    }

    exit(0);
}