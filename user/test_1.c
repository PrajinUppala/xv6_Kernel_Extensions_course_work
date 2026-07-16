#include "kernel/types.h"
#include "user/user.h"

#define PAGES 1000

struct vmstats info;

int main()
{
    printf("=== Test 1: Page Fault + Replacement ===\n");

    char *p = sbrk(PAGES * 4096);

    // Trigger faults
    for(int i = 0; i < PAGES; i++){
        p[i * 4096] = i;
    }

    if(getvmstats(getpid(), &info) < 0){
        printf("ERROR: getvmstats failed\n");
        exit(1);
    }

    printf("Faults: %d\n", info.page_faults);
    printf("Evicted: %d\n", info.pages_evicted);
    printf("Swap Out: %d\n", info.pages_swapped_out);
    printf("Resident: %d\n", info.resident_pages);

    // Validation
    if(info.page_faults == 0)
        printf("ERROR: No faults\n");

    if(info.pages_evicted == 0)
        printf("ERROR: No eviction\n");

    if(info.resident_pages <= 0)
        printf("ERROR: Resident pages wrong\n");

    printf("Test 1 Complete\n");

    exit(0);
}