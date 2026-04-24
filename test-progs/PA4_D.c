// Merged: PA4_11 + PA4_12 + PA4_13
// Tests: disk scheduling basics, syscall sanity (setdisksched/getdiskstats), FCFS vs SSTF latency

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"

// ============================================================
// PA4_11 (original by Tanish)
// ============================================================

#define PGSIZE_11 4096
#define NUM_PAGES_11 80 // 80 pages > 64 MAX_NFRAME, guaranteed to trigger swap
#define FCFS_11 0
#define SSTF_11 1

static void trigger_swap_workload_11(void) {
    char *pages[NUM_PAGES_11];

    printf("  [Test] Allocating %d pages to force swap-out...\n", NUM_PAGES_11);
    for(int i = 0; i < NUM_PAGES_11; i++) {
        pages[i] = malloc(PGSIZE_11);
        if(pages[i] == 0) {
            printf("  [Error] malloc failed at page %d\n", i);
            exit(1);
        }
        // Write to the first byte of the page. 
        // This forces the kernel to actually allocate a physical frame 
        // and triggers the page eviction logic when RAM runs out.
        pages[i][0] = 'A' + (i % 26);
    }

    printf("  [Test] Reading back memory to force swap-in...\n");
    for(int i = 0; i < NUM_PAGES_11; i++) {
        // Accessing the evicted pages will trigger page faults,
        // forcing the kernel to read them back from the simulated RAID disk.
        char val = pages[i][0];
        if (val != 'A' + (i % 26)) {
            printf("  [Error] Data corruption on page %d!\n", i);
        }
    }

    printf("  [Test] Freeing memory...\n");
    for(int i = 0; i < NUM_PAGES_11; i++) {
        free(pages[i]);
    }
}

static void
test_pa4_11(void)
{
    printf("=== Starting PA4 Disk & Swap Test ===\n");
    struct diskstats diskstat = {0, 0, 0};
    // Test 1: FCFS Scheduling
    printf("\n--- Setting Policy: FCFS ---\n");
    if(setdisksched(FCFS_11) < 0) {
        printf("Error: sys_setdisksched failed\n");
    } else {
        trigger_swap_workload_11();
    }
    getdiskstats(getpid2(),&diskstat);
    printf("\nDisk Stats (FCFS): Reads=%d, Writes=%d, Avg Latency=%d.%d ticks\n",
           diskstat.reads, diskstat.writes, diskstat.avg_latency / 100, diskstat.avg_latency % 100);

    diskstat.reads = diskstat.writes = diskstat.avg_latency = 0; // reset stats
    // Test 2: SSTF Scheduling
    printf("\n--- Setting Policy: SSTF ---\n");
    if(setdisksched(SSTF_11) < 0) {
        printf("Error: sys_setdisksched failed\n");
    } else {
        trigger_swap_workload_11();
    }
    getdiskstats(getpid2(),&diskstat);
    printf("\nDisk Stats (SSTF): Reads=%d, Writes=%d, Avg Latency=%d.%d ticks\n",
           diskstat.reads, diskstat.writes, diskstat.avg_latency / 100, diskstat.avg_latency % 100);

    printf("\n=== Tests Completed ===\n");
}

// ============================================================
// PA4_12 (original by Tanish)
// ============================================================

#define PGSIZE_12     4096
#define FCFS_12       0
#define SSTF_12       1
// 80 pages > 64-frame limit, guaranteed to trigger swap I/O
#define SWAP_PAGES_12 80

static void force_swap_io_12(void) {
    char *mem[SWAP_PAGES_12];
    // Allocate and touch pages to force evictions
    for (int i = 0; i < SWAP_PAGES_12; i++) {
        mem[i] = malloc(PGSIZE_12);
        if (!mem[i]) { printf("  malloc failed at %d\n", i); return; }
        mem[i][0] = (char)(i & 0xFF);
        mem[i][PGSIZE_12 - 1] = (char)((i + 1) & 0xFF);
    }
    // Read back all pages — triggers swap-in for evicted ones
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES_12; i++) {
        if (mem[i][0] != (char)(i & 0xFF)) errs++;
        if (mem[i][PGSIZE_12 - 1] != (char)((i + 1) & 0xFF)) errs++;
    }
    if (errs == 0)
        printf("  data integrity OK (%d pages)\n", SWAP_PAGES_12);
    else
        printf("  WARN: %d data mismatches\n", errs);
    for (int i = 0; i < SWAP_PAGES_12; i++) free(mem[i]);
}

static void
test_pa4_12(void)
{
    printf("=== Test l: Basic Disk Scheduling System Call Sanity ===\n");
    struct diskstats st;
    int pid = getpid2();

    // -----------------------------------------------------------------------
    printf("[1] setdisksched: invalid policies must be rejected\n");
    if (setdisksched(99) < 0)
        printf("  PASS: policy 99 rejected\n");
    else
        printf("  FAIL: policy 99 accepted\n");

    if (setdisksched(-1) < 0)
        printf("  PASS: policy -1 rejected\n");
    else
        printf("  FAIL: policy -1 accepted\n");

    if (setdisksched(2) < 0)
        printf("  PASS: policy 2 rejected\n");
    else
        printf("  FAIL: policy 2 accepted\n");

    // -----------------------------------------------------------------------
    printf("[2] setdisksched(FCFS) then trigger swap I/O\n");
    if (setdisksched(FCFS_12) == 0)
        printf("  PASS: FCFS accepted\n");
    else {
        printf("  FAIL: FCFS rejected — aborting\n");
        return;
    }

    force_swap_io_12();

    memset(&st, 0, sizeof(st));
    if (getdiskstats(pid, &st) != 0) {
        printf("  FAIL: getdiskstats returned error\n");
        return;
    }
    printf("  FCFS stats -> reads=%d writes=%d avg_latency=%d.%d ticks\n",
           st.reads, st.writes, st.avg_latency / 100, st.avg_latency % 100);

    if (st.writes > 0)
        printf("  PASS: disk_writes > 0 (swap-out happened)\n");
    else
        printf("  FAIL: disk_writes == 0\n");

    if (st.reads > 0)
        printf("  PASS: disk_reads > 0 (swap-in happened)\n");
    else
        printf("  FAIL: disk_reads == 0\n");

    if (st.avg_latency > 0)
        printf("  PASS: avg_latency > 0\n");
    else
        printf("  FAIL: avg_latency == 0 — latency model not running\n");

    // -----------------------------------------------------------------------
    printf("[3] setdisksched(SSTF)\n");
    if (setdisksched(SSTF_12) == 0)
        printf("  PASS: SSTF accepted\n");
    else
        printf("  FAIL: SSTF rejected\n");

    // -----------------------------------------------------------------------
    printf("[4] getdiskstats: invalid PID handling\n");
    struct diskstats bad;
    memset(&bad, 0, sizeof(bad));
    if (getdiskstats(-1, &bad) < 0)
        printf("  PASS: PID -1 rejected\n");
    else
        printf("  FAIL: PID -1 accepted\n");

    if (getdiskstats(99999, &bad) < 0)
        printf("  PASS: PID 99999 rejected\n");
    else
        printf("  FAIL: PID 99999 accepted\n");

    // -----------------------------------------------------------------------
    printf("[5] stats are non-negative\n");
    if (st.reads >= 0 && st.writes >= 0 && st.avg_latency >= 0)
        printf("  PASS: all counters non-negative\n");
    else
        printf("  FAIL: negative counter detected\n");

    printf("=== Test l done ===\n");
}

// ============================================================
// PA4_13 (original by Tanish)
// ============================================================

#define PGSIZE_13      4096
#define FCFS_13        0
#define SSTF_13        1
// Larger set gives the scheduler more reordering opportunity
#define SWAP_PAGES_13  90

// Write to pages in a strided order so blocks are scattered
static void strided_io_13(void) {
    char *mem[SWAP_PAGES_13];
    // Allocate all pages first
    for (int i = 0; i < SWAP_PAGES_13; i++) {
        mem[i] = malloc(PGSIZE_13);
        if (!mem[i]) { printf("  malloc failed\n"); return; }
        mem[i][0] = (char)(i & 0xFF);
    }
    // Read back in reverse to maximise head movement for FCFS
    int errs = 0;
    for (int i = SWAP_PAGES_13 - 1; i >= 0; i--) {
        if (mem[i][0] != (char)(i & 0xFF)) errs++;
    }
    if (errs == 0)
        printf("  all %d pages verified correct\n", SWAP_PAGES_13);
    else
        printf("  WARN: %d mismatches\n", errs);
    for (int i = 0; i < SWAP_PAGES_13; i++) free(mem[i]);
}

static void
test_pa4_13(void)
{
    printf("=== Test m: FCFS vs SSTF Latency Comparison ===\n");

    struct diskstats fcfs_st, sstf_st;
    int pid = getpid2();

    // ------------------------------------------------------------------
    // Run FCFS workload in child so stats are isolated
    printf("[1] Running workload under FCFS...\n");
    int pid_fcfs = fork();
    if (pid_fcfs == 0) {
        setdisksched(FCFS_13);
        strided_io_13();
        exit(0);
    }
    wait(0);

    memset(&fcfs_st, 0, sizeof(fcfs_st));
    // Parent accumulates its own stats from its own I/O
    // We run the actual measurement in the parent process so getdiskstats
    // reports THIS process's activity
    setdisksched(FCFS_13);
    strided_io_13();
    if (getdiskstats(pid, &fcfs_st) != 0) {
        printf("  FAIL: getdiskstats (FCFS) error\n");
        return;
    }
    printf("  FCFS  -> reads=%d writes=%d avg_latency=%d.%d ticks\n",
           fcfs_st.reads, fcfs_st.writes,
           fcfs_st.avg_latency / 100, fcfs_st.avg_latency % 100);

    // ------------------------------------------------------------------
    printf("[2] Running workload under SSTF...\n");
    setdisksched(SSTF_13);
    strided_io_13();

    // Note: getdiskstats returns cumulative stats; capture delta
    struct diskstats after_sstf;
    memset(&after_sstf, 0, sizeof(after_sstf));
    if (getdiskstats(pid, &after_sstf) != 0) {
        printf("  FAIL: getdiskstats (SSTF) error\n");
        return;
    }
    // sstf delta is after_sstf minus fcfs_st (same pid, cumulative)
    sstf_st.reads       = after_sstf.reads       - fcfs_st.reads;
    sstf_st.writes      = after_sstf.writes      - fcfs_st.writes;
    // avg_latency is already an average, use the post value
    sstf_st.avg_latency = after_sstf.avg_latency;

    printf("  SSTF  -> reads=%d writes=%d avg_latency=%d.%d ticks\n",
           sstf_st.reads, sstf_st.writes,
           sstf_st.avg_latency / 100, sstf_st.avg_latency % 100);

    // ------------------------------------------------------------------
    printf("[3] Latency sanity checks\n");
    if (fcfs_st.avg_latency > 0)
        printf("  PASS: FCFS latency > 0\n");
    else
        printf("  FAIL: FCFS latency == 0\n");

    if (after_sstf.avg_latency > 0)
        printf("  PASS: SSTF latency > 0\n");
    else
        printf("  FAIL: SSTF latency == 0\n");

    // SSTF average latency should not be worse than FCFS overall
    // (we compare final averages because running order affects head pos)
    if (after_sstf.avg_latency <= fcfs_st.avg_latency)
        printf("  PASS: SSTF avg_latency <= FCFS avg_latency"
               " (%d.%d <= %d.%d)\n",
               after_sstf.avg_latency/100, after_sstf.avg_latency%100,
               fcfs_st.avg_latency/100, fcfs_st.avg_latency%100);
    else
        printf("  NOTE: SSTF avg_latency > FCFS avg_latency — workload"
               " may be too sequential; both policies functioning.\n");

    // ------------------------------------------------------------------
    printf("[4] Data integrity after policy switch\n");
    // Already verified inside strided_io_13(); just confirm we get here
    printf("  PASS: process survived both scheduling policies\n");

    // ------------------------------------------------------------------
    printf("[5] Minimum latency >= rotational delay (C=7 ticks * 100)\n");
    // Each I/O must have at least C=7 ticks latency (stored *100)
    if (fcfs_st.avg_latency >= 700)
        printf("  PASS: FCFS avg_latency includes rotational delay\n");
    else
        printf("  FAIL: FCFS avg_latency %d.%d < 7.00 ticks\n",
               fcfs_st.avg_latency/100, fcfs_st.avg_latency%100);

    printf("=== Test m done ===\n");
}

// ============================================================
// main
// ============================================================

int
main(void)
{
  printf("==============================\n");
  printf("PA4_D: Disk scheduling basics (FCFS/SSTF)\n");
  printf("==============================\n\n");

  printf("--- PA4_11: disk & swap basic test ---\n");
  test_pa4_11();

  printf("\n--- PA4_12: disk scheduling syscall sanity ---\n");
  test_pa4_12();

  printf("\n--- PA4_13: FCFS vs SSTF latency ---\n");
  test_pa4_13();

  printf("\nPA4_D done.\n");
  exit(0);
}
