// Credit: Akshat
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main() {
    struct vmstats st;
    printf("\n--- Test 4: Edge Cases ---\n");
    
    // Edge Case 1: sbrk(0) shouldn't change anything
    char *current = sbrk(0);
    printf("Current heap top: %p\n", current);
    
    // Edge Case 2: Shrinking the heap (negative sbrk)
    sbrk(5 * 4096); // grow by 5
    for(int i=0; i<5; i++) current[i * 4096] = 'Z'; // fault them in
    getvmstats(getpid(), &st);
    int res_before = st.resident_pages;
    
    sbrk(-3 * 4096); // shrink by 3
    getvmstats(getpid(), &st);
    printf("Resident before shrink: %d, after shrink: %d\n", res_before, st.resident_pages);
    
    // Edge Case 3: Out of Swap Space
    
    char *huge = sbrk(1000 * 4096);
    if (huge == (char*)-1) {
        printf("Successfully caught Out-Of-Memory error from kernel!\n");
    } else {
        for(int i=0; i<40000; i++) huge[i * 4096] = 'Y';
    }    

    printf("Test 4 Complete.\n\n");
    exit(0);
}