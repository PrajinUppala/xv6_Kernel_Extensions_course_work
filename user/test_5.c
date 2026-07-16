#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define NPAGES 500

struct vmstats before, after;


void get_stats(int pid, struct vmstats *s) {
  if(getvmstats(pid, s) < 0){
    printf("ERROR: getvmstats failed for pid %d\n", pid);
    exit(1);
  }
}

void print_mlfq(int pid, char *name){
  struct mlfqinfo info;

  if(getmlfqinfo(pid, &info) < 0){
    printf("ERROR: getmlfqinfo failed for pid %d\n", pid);
    return;
  }

  printf("%s (PID %d): level=%d, times_scheduled=%d\n",
         name, pid, info.level, info.times_scheduled);

  printf("  ticks: [0]=%d [1]=%d [2]=%d [3]=%d\n",
         info.ticks[0], info.ticks[1],
         info.ticks[2], info.ticks[3]);
}

void print_result(char *test, int pass) {
  if(pass)
    printf("[PASS] %s\n", test);
  else
    printf("[FAIL] %s\n", test);
}

int main() {
  printf("\n===== STRICT VM TEST START =====\n");

  int pid = getpid();

  char *base = sbrk(NPAGES * PGSIZE);
  print_result("sbrk allocation", base != SBRK_ERROR);

  get_stats(pid, &before);

  for(int i = 0; i < NPAGES; i++){
    base[i * PGSIZE] = 1;
  }

  get_stats(pid, &after);

  int pf = after.page_faults - before.page_faults;
  print_result("page faults triggered", pf >= NPAGES);

  printf("Expected page_faults >= %d, Got = %d\n", NPAGES, pf);

  get_stats(pid, &before);

  for(int i = 0; i < NPAGES; i++){
    base[i * PGSIZE] += 1;
  }

  get_stats(pid, &after);

  int ev = after.pages_evicted - before.pages_evicted;
  print_result("page eviction triggered", ev > 0);

  printf("Expected pages_evicted > 0, Got = %d\n", ev);

  get_stats(pid, &before);

  for(int i = NPAGES-1; i >= 0; i--){
    base[i * PGSIZE] += 1;
  }

  get_stats(pid, &after);

  int si = after.pages_swapped_in - before.pages_swapped_in;
  print_result("swap-in works", si > 0);

  printf("Expected pages_swapped_in > 0, Got = %d\n", si);

  get_stats(pid, &before);

  for(int i = 0; i < NPAGES; i++){
    base[i * PGSIZE] += 1;
  }

  get_stats(pid, &after);

  int reuse = after.pages_swapped_in - before.pages_swapped_in;
  print_result("reuse of evicted pages", reuse > 0);

  printf("Expected swapped_in increase again, Got = %d\n", reuse);

  // ---------------------------
  // MLFQ TEST
  // ---------------------------
  printf("\n[TEST] MLFQ behavior\n");

  int child = fork();

  if(child == 0){
    int cpid = getpid();

    for(int i = 0; i < 200000000; i++){
        volatile int x = 0; // prevent optimization
        x++;
    }

    print_mlfq(cpid, "Child BEFORE access");

    get_stats(cpid, &before);

    for(int i = 0; i < NPAGES; i++){
      base[i * PGSIZE] += 1;
    }

    get_stats(cpid, &after);

    int child_ev = after.pages_evicted - before.pages_evicted;

    print_mlfq(cpid, "Child AFTER access");

    printf("Child evictions = %d\n", child_ev);
    exit(0);

  } else {
    pause(10);

    print_mlfq(pid, "Parent BEFORE access");

    get_stats(pid, &before);

    for(int i = 0; i < NPAGES; i++){
      base[i * PGSIZE] += 1;
    }

    get_stats(pid, &after);

    int parent_ev = after.pages_evicted - before.pages_evicted;

    print_mlfq(pid, "Parent AFTER access");

    wait(0);

    printf("Parent evictions = %d\n", parent_ev);

    printf("\n[INFO] EXPECTED:\n");
    printf("  Child level > Parent level\n");
  }

  printf("\n===== STRICT VM TEST END =====\n");
  exit(0);
}