
// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

// #define PAGES 128  // increase if needed

// int main() {
//     printf("\n=== Disk Stats Test ===\n");

//     // Allocate large memory to force swapping
//     char *arr[PAGES];

//     for(int i = 0; i < PAGES; i++){
//         arr[i] = sbrk(4096);   // allocate 1 page
//         if(arr[i] == (char*)-1){
//             printf("sbrk failed at %d\n", i);
//             break;
//         }
//     }

//     // WRITE phase → will cause swap-out later
//     printf("Writing to pages...\n");
//     for(int i = 0; i < PAGES; i++){
//         arr[i][0] = i;
//         // printf("$\n");
//     }

//     // READ phase → will cause swap-in
//     printf("Reading pages...\n");
//     int sum = 0;
//     for(int i = 0; i < PAGES; i++){
//         sum += arr[i][0];
//     }

//     printf("Sum = %d\n", sum);

//     // Get stats
//     struct vmstats st;
//     if(getvmstats(getpid(), &st) < 0){
//         printf("getvmstats failed\n");
//         exit(1);
//     }

//     printf("\n=== VM STATS ===\n");
//     printf("disk_reads        = %d\n", st.disk_reads);
//     printf("disk_writes       = %d\n", st.disk_writes);
//     printf("avg_disk_latency  = %d\n", st.avg_disk_latency);
//     printf("pages_evicted     = %d\n", st.pages_evicted);
//     printf("pages_swapped_in  = %d\n", st.pages_swapped_in);
//     printf("pages_swapped_out = %d\n", st.pages_swapped_out);

//     printf("========================\n");

//     exit(0);
// }

// #include "kernel/types.h"
// #include "user/user.h"

// #define N 400

// int main() {
//     printf("\n=== RAID 0 Test ===\n");

//     setraidmode(0);  // RAID 0

//     char *arr[N];

//     // allocate
//     for(int i = 0; i < N; i++){
//         arr[i] = sbrk(4096);
//         if(arr[i] == (char*)-1){
//             printf("alloc failed\n");
//             exit(1);
//         }
//     }

//     // write pattern
//     for(int i = 0; i < N; i++){
//         arr[i][0] = (char)(i + 1);
//     }

//     // read back
//     int ok = 1;
//     for(int i = 0; i < N; i++){
//         if(arr[i][0] != (char)(i + 1)){
//             ok = 0;
//             printf("Mismatch at %d\n", i);
//             break;
//         }
//     }

//     if(ok) printf("PASS: RAID 0 correct\n");
//     else printf("FAIL: RAID 0 wrong\n");

//     exit(0);
// }

// #include "kernel/types.h"
// #include "user/user.h"

// #define N 400

// int main() {
//     printf("\n=== RAID 1 Test (N=400) ===\n");

//     setraidmode(1);

//     char *arr[N];

//     for(int i = 0; i < N; i++){
//         arr[i] = sbrk(4096);
//         if(arr[i] == (char*)-1){
//             printf("alloc failed at %d\n", i);
//             exit(1);
//         }
//     }

//     // write initial
//     for(int i = 0; i < N; i++){
//         arr[i][0] = (char)(i % 100);
//     }

//     // overwrite pattern
//     for(int i = 0; i < N; i += 3){
//         arr[i][0] = 55;
//     }

//     // verify
//     int ok = 1;
//     for(int i = 0; i < N; i++){
//         char expected = (i % 3 == 0) ? 55 : (char)(i % 100);
//         if(arr[i][0] != expected){
//             printf("Mismatch at %d\n", i);
//             ok = 0;
//             break;
//         }
//     }

//     if(ok) printf("PASS: RAID 1 correct\n");
//     else printf("FAIL: RAID 1 wrong\n");

//     exit(0);
// }

#include "kernel/types.h"
#include "user/user.h"

#define N 1000

int main() {
    printf("\n=== RAID 5 Test (N=400) ===\n");

    setraidmode(2);

    char *arr[N];

    for(int i = 0; i < N; i++){
        arr[i] = sbrk(4096);
        if(arr[i] == (char*)-1){
            printf("alloc failed at %d\n", i);
            exit(1);
        }
    }

    // first write
    for(int i = 0; i < N; i++){
        arr[i][0] = (char)(i % 50);
    }

    // second write (forces parity updates)
    for(int i = 0; i < N; i++){
        arr[i][0] += 7;
    }

    // heavy read (forces swap-in)
    int sum = 0;
    for(int i = 0; i < N; i++){
        sum += arr[i][0];
    }

    printf("Sum = %d\n", sum);

    // verify
    int ok = 1;
    sum=0;
    for(int i = 0; i < N; i++){
        char expected = (char)((i % 50) + 7);
        sum+=expected;
        if(arr[i][0] != expected){
            printf("Mismatch at %d\n", i);
            ok = 0;
            break;
        }
    }
    printf("Sum = %d\n", sum);

    if(ok) printf("PASS: RAID 5 correct\n");
    else printf("FAIL: RAID 5 wrong\n");

    exit(0);
}