// Credit: Akshat
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    struct vmstats st;
    int pgsz = 4096;
    
    printf("\n--- Test 1: Allocation and Page Faults ---\n");
    getvmstats(getpid(), &st);
    printf("Initial: faults=%d, resident=%d\n", st.page_faults, st.resident_pages);

    // 1. Allocate large memory region
    char *mem = sbrk(50 * pgsz); 
    if(mem == (char*)-1) {
        printf("sbrk failed\n");
        exit(1);
    }
    
    getvmstats(getpid(), &st);
    printf("After sbrk (Lazy): faults=%d, resident=%d\n", st.page_faults, st.resident_pages);

    // 2. Trigger page faults
    for(int i = 0; i < 50; i++) {
        mem[i * pgsz] = 'A'; // Write to force allocation
    }

    getvmstats(getpid(), &st);
    printf("After touching: faults=%d, resident=%d\n", st.page_faults, st.resident_pages);
    printf("Test 1 Complete.\n\n");
    exit(0);
}