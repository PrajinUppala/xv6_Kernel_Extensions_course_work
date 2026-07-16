#include "kernel/types.h"
#include "user/user.h"

#define PAGES 3000

struct vmstats info;

int main()
{
    printf("=== Test 2: Swap In + Reuse ===\n");

    char *p = sbrk(PAGES * 4096);

    for(int i = 0; i < PAGES; i++){
        p[i * 4096] = i;
    }
    // Re-access first pages (likely evicted)
    int error = 0;
    for(int i = 0; i < 20; i++){
        if(p[i * 4096] != i){
            error = 1;
        }
    }
    
    if(getvmstats(getpid(), &info) < 0){
        printf("ERROR: getvmstats failed\n");
        exit(1);
    }
    
    printf("Swap out: %d\n", info.pages_swapped_out);
    printf("Swap In: %d\n", info.pages_swapped_in);

    if(error)
        printf("ERROR: Data corruption\n");

    if(info.pages_swapped_in == 0)
        printf("ERROR: No swap-in happened\n");

    printf("Test 2 Complete\n");

    exit(0);
}