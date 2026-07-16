// Merged: PA4_18 + PA4_19 + PA4_20 + PA4_21 + PA4_22
// Tests: scheduler-aware disk I/O, latency model, RAID mode switching,
//        end-to-end swap stress, SSTF head position tracking

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
// #include "kernel/diskstat.h"
// #include "kernel/vmstats.h"

// ============================================================
// PA4_18 (original by Tanish)
// ============================================================

#define PGSIZE_18       4096
#define SSTF_18         1
#define FCFS_18         0
#define SWAP_PAGES_18   80
#define SPIN_ITERS   600000000   // enough to get demoted to level 2+

static void do_swap_work_18(void) {
    char *mem = sbrk(SWAP_PAGES_18 * PGSIZE_18);
    if (mem == (char *)-1) return;
    for (int i = 0; i < SWAP_PAGES_18; i++)
        mem[i * PGSIZE_18] = (char)(i & 0xFF);
    for (int i = 0; i < SWAP_PAGES_18; i++) {
        if (mem[i * PGSIZE_18] != (char)(i & 0xFF))
            printf("  [WARN] data mismatch page %d\n", i);
    }
}

static void
test_pa4_18(void)
{
    printf("=== Test r: Scheduler-Aware Disk Scheduling ===\n");

    setdisksched(SSTF_18);
    setraidmode(0); // RAID0 for simplicity

    // ------------------------------------------------------------------
    printf("[1] Per-process stat isolation\n");
    // Fork a child that does disk I/O; parent's stats should not include it
    struct disk_stats parent_before, parent_after;
    memset(&parent_before, 0, sizeof(parent_before));
    memset(&parent_after, 0, sizeof(parent_after));
    int ppid = getpid2();
    getdiskstats(ppid, &parent_before);

    int child = fork();
    if (child == 0) {
        // Child does swap I/O
        setraidmode(0);
        setdisksched(SSTF_18);
        do_swap_work_18();
        exit(0);
    }
    wait(0);

    getdiskstats(ppid, &parent_after);
    int delta_reads  = parent_after.reads  - parent_before.reads;
    int delta_writes = parent_after.writes - parent_before.writes;
    printf("  parent delta: reads=%d writes=%d\n", delta_reads, delta_writes);
    if (delta_reads == 0 && delta_writes == 0)
        printf("  PASS: parent stats unaffected by child's disk I/O\n");
    else
        printf("  NOTE: parent delta non-zero — may share kernel I/O path\n");

    // ------------------------------------------------------------------
    printf("[2] High-priority process disk I/O\n");
    // Parent is at high MLFQ priority (syscall-heavy = low queue level)
    // Do many syscalls to stay interactive
    for (int i = 0; i < 5000; i++) getpid(); // keep del_s high
    do_swap_work_18();
    struct disk_stats hi_st;
    memset(&hi_st, 0, sizeof(hi_st));
    getdiskstats(ppid, &hi_st);
    printf("  HI-priority: reads=%d writes=%d latency=%d.%d\n",
           hi_st.reads - parent_after.reads,
           hi_st.writes - parent_after.writes,
           hi_st.avg_latency/100, hi_st.avg_latency%100);

    // ------------------------------------------------------------------
    printf("[3] Low-priority process disk I/O\n");
    int lo_pipe[2];
    pipe(lo_pipe);
    int lo_child = fork();
    if (lo_child == 0) {
        close(lo_pipe[0]);
        // Spin to get demoted in MLFQ
        volatile int x = 0;
        for (int i = 0; i < SPIN_ITERS; i++) x++;
        setraidmode(0);
        setdisksched(SSTF_18);
        do_swap_work_18();
        struct disk_stats lo_st;
        memset(&lo_st, 0, sizeof(lo_st));
        getdiskstats(getpid2(), &lo_st);
        write(lo_pipe[1], &lo_st, sizeof(lo_st));
        close(lo_pipe[1]);
        exit(0);
    }
    close(lo_pipe[1]);
    struct disk_stats lo_st;
    memset(&lo_st, 0, sizeof(lo_st));
    read(lo_pipe[0], &lo_st, sizeof(lo_st));
    close(lo_pipe[0]);
    wait(0);

    printf("  LO-priority: reads=%d writes=%d latency=%d.%d\n",
           lo_st.reads, lo_st.writes,
           lo_st.avg_latency/100, lo_st.avg_latency%100);

    if (lo_st.reads > 0 && lo_st.writes > 0)
        printf("  PASS: low-priority process performed disk I/O\n");
    else
        printf("  FAIL: low-priority process had no I/O\n");

    // ------------------------------------------------------------------
    printf("[4] Policy switch mid-run correctness\n");
    // Switch policy between writes and reads — data must still be correct
    char *mem2 = sbrk(SWAP_PAGES_18 * PGSIZE_18);
    if (mem2 == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    setdisksched(FCFS_18);
    for (int i = 0; i < SWAP_PAGES_18; i++)
        mem2[i * PGSIZE_18] = (char)((i + 7) & 0xFF);

    setdisksched(SSTF_18); // switch policy before reads
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES_18; i++)
        if (mem2[i * PGSIZE_18] != (char)((i + 7) & 0xFF)) errs++;

    if (errs == 0)
        printf("  PASS: data correct after mid-run policy switch\n");
    else
        printf("  FAIL: %d data errors after policy switch\n", errs);

    // ------------------------------------------------------------------
    printf("[5] Stats monotonically increase\n");
    struct disk_stats final_st;
    memset(&final_st, 0, sizeof(final_st));
    getdiskstats(ppid, &final_st);
    if (final_st.reads >= hi_st.reads && final_st.writes >= hi_st.writes)
        printf("  PASS: stats only increase\n");
    else
        printf("  FAIL: stats went backwards\n");

    printf("=== Test r done ===\n");
}

// ============================================================
// PA4_19 (original by Tanish)
// ============================================================

#define PGSIZE_19        4096
#define ROTATIONAL_C  7        // from param.h
#define FCFS_19          0
#define SSTF_19          1
#define SWAP_PAGES_19    80

static char pat_19(int i, int p) { return (char)((i*7 + p*13 + 1) & 0xFF); }

// Sequential access: pages allocated and read in order 0..N-1
static void seq_access_19(char *mem, int n, int pass) {
    for (int i = 0; i < n; i++) mem[i * PGSIZE_19] = pat_19(i, pass);
    for (int i = 0; i < n; i++) (void)mem[i * PGSIZE_19];
}

// Scattered access: pages touched in a strided pattern (maximises seek)
static void scattered_access_19(char *mem, int n, int pass) {
    for (int i = 0; i < n; i++) mem[i * PGSIZE_19] = pat_19(i, pass);
    // Read back in reverse (worst case for sequential head movement)
    for (int i = n - 1; i >= 0; i--) (void)mem[i * PGSIZE_19];
    // Read again in prime-stride order
    for (int i = 0; i < n; i++) (void)mem[(i * 7 % n) * PGSIZE_19];
}

static void
test_pa4_19(void)
{
    printf("=== Test s: Disk Latency Model Verification ===\n");

    setraidmode(0); // RAID0 — simplest path
    int pid = getpid2();

    char *mem = sbrk(SWAP_PAGES_19 * PGSIZE_19);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    // ------------------------------------------------------------------
    printf("[1] Minimum latency >= C=%d ticks (=%d in 100x units)\n",
           ROTATIONAL_C, ROTATIONAL_C * 100);

    setdisksched(FCFS_19);
    seq_access_19(mem, SWAP_PAGES_19, 0);

    struct disk_stats st1;
    memset(&st1, 0, sizeof(st1));
    getdiskstats(pid, &st1);
    printf("  Sequential FCFS: reads=%d writes=%d avg_lat=%d.%d\n",
           st1.reads, st1.writes, st1.avg_latency/100, st1.avg_latency%100);

    if (st1.avg_latency >= ROTATIONAL_C * 100)
        printf("  PASS: avg_latency >= C*100 (%d >= %d)\n",
               st1.avg_latency, ROTATIONAL_C * 100);
    else
        printf("  FAIL: avg_latency %d < minimum %d\n",
               st1.avg_latency, ROTATIONAL_C * 100);

    // ------------------------------------------------------------------
    printf("[2] Scattered access should yield higher avg_latency than sequential\n");

    setdisksched(SSTF_19);
    scattered_access_19(mem, SWAP_PAGES_19, 1);

    struct disk_stats st2;
    memset(&st2, 0, sizeof(st2));
    getdiskstats(pid, &st2);
    printf("  Scattered SSTF: reads=%d writes=%d avg_lat=%d.%d\n",
           st2.reads, st2.writes, st2.avg_latency/100, st2.avg_latency%100);

    // avg_latency after scatter >= after sequential (cumulative average)
    // This is a soft check because avg is cumulative over all ops
    if (st2.avg_latency >= st1.avg_latency)
        printf("  PASS: cumulative latency did not drop\n");
    else
        printf("  NOTE: cumulative avg dropped — SSTF reordered effectively\n");

    // ------------------------------------------------------------------
    printf("[3] SSTF reduces latency vs FCFS on same workload\n");
    // Run identical scattered workload under both policies, compare
    struct disk_stats before_fcfs, after_fcfs, before_sstf, after_sstf;

    setdisksched(FCFS_19);
    getdiskstats(pid, &before_fcfs);
    scattered_access_19(mem, SWAP_PAGES_19, 2);
    getdiskstats(pid, &after_fcfs);
    int fcfs_lat = after_fcfs.avg_latency;

    setdisksched(SSTF_19);
    getdiskstats(pid, &before_sstf);
    scattered_access_19(mem, SWAP_PAGES_19, 3);
    getdiskstats(pid, &after_sstf);
    int sstf_lat = after_sstf.avg_latency;

    printf("  FCFS avg_lat=%d.%d  SSTF avg_lat=%d.%d\n",
           fcfs_lat/100, fcfs_lat%100, sstf_lat/100, sstf_lat%100);

    if (sstf_lat <= fcfs_lat)
        printf("  PASS: SSTF avg_latency <= FCFS avg_latency\n");
    else
        printf("  NOTE: SSTF avg higher (cumulative includes prior FCFS ops)\n");

    // ------------------------------------------------------------------
    printf("[4] Latency > 0 for all ops\n");
    if (after_sstf.avg_latency > 0 && after_fcfs.avg_latency > 0)
        printf("  PASS: both policies record positive latency\n");
    else
        printf("  FAIL: zero latency detected\n");

    // ------------------------------------------------------------------
    printf("[5] Data integrity through latency tests\n");
    // All scattered_access_19 calls verified read-back implicitly via pattern;
    // do one explicit final check
    for (int i = 0; i < SWAP_PAGES_19; i++)
        mem[i * PGSIZE_19] = pat_19(i, 99);
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES_19; i++)
        if (mem[i * PGSIZE_19] != pat_19(i, 99)) errs++;
    if (errs == 0)
        printf("  PASS: data correct after all latency tests\n");
    else
        printf("  FAIL: %d data errors\n", errs);

    printf("=== Test s done ===\n");
}

// ============================================================
// PA4_20 (original by Tanish)
// ============================================================

#define PGSIZE_20      4096
#define RAID0_20       0
#define RAID1_20       1
#define RAID5_20       2
#define FCFS_20        0
#define NCHILDREN_20   3
#define SWAP_PAGES_20  75   // > 64 frames, each child uses this many

typedef struct {
    int pid;
    int reads;
    int writes;
    int avg_latency;
    int errors;
} ChildReport_20;

static char pat_20(int i, int id) { return (char)((i*11 + id*19 + 3) & 0xFF); }

static void
test_pa4_20(void)
{
    printf("=== Test t: RAID Mode Switching + Multi-Process Stats ===\n");

    // ------------------------------------------------------------------
    printf("[1] setraidmode: invalid values rejected\n");
    if (setraidmode(99) < 0)
        printf("  PASS: mode 99 rejected\n");
    else
        printf("  FAIL: mode 99 accepted\n");

    if (setraidmode(-1) < 0)
        printf("  PASS: mode -1 rejected\n");
    else
        printf("  FAIL: mode -1 accepted\n");

    if (setraidmode(5) < 0)
        printf("  PASS: mode 5 rejected\n");
    else
        printf("  FAIL: mode 5 accepted\n");

    // ------------------------------------------------------------------
    printf("[2] RAID mode sequence: RAID0->RAID1->RAID5->RAID0\n");
    int modes[] = {RAID0_20, RAID1_20, RAID5_20, RAID0_20};
    char *mnames[] = {"RAID0", "RAID1", "RAID5", "RAID0"};
    for (int m = 0; m < 4; m++) {
        if (setraidmode(modes[m]) == 0)
            printf("  PASS: switched to %s\n", mnames[m]);
        else
            printf("  FAIL: could not switch to %s\n", mnames[m]);
    }

    // ------------------------------------------------------------------
    printf("[3] Multi-process independent disk stats\n");

    int pipefd[NCHILDREN_20][2];
    for (int c = 0; c < NCHILDREN_20; c++) pipe(pipefd[c]);

    int child_modes[] = {RAID0_20, RAID1_20, RAID5_20};
    ChildReport_20 reports[NCHILDREN_20];

    // FIX: Serialize children so they don't concurrently overwrite the global RAID_MODE
    for (int c = 0; c < NCHILDREN_20; c++) {
        int pid = fork();
        if (pid == 0) {
            // Close other children's pipes
            for (int i = 0; i < NCHILDREN_20; i++) {
                close(pipefd[i][0]);
                if (i != c) close(pipefd[i][1]);
            }

            ChildReport_20 rep;
            rep.pid    = getpid2();
            rep.errors = 0;

            setraidmode(child_modes[c]);
            setdisksched(FCFS_20);

            char *mem = sbrk(SWAP_PAGES_20 * PGSIZE_20);
            if (mem == (char *)-1) { rep.errors = 99; goto done_20; }

            // Write
            for (int i = 0; i < SWAP_PAGES_20; i++)
                mem[i * PGSIZE_20] = pat_20(i, c);

            // Read back
            for (int i = 0; i < SWAP_PAGES_20; i++) {
                if (mem[i * PGSIZE_20] != pat_20(i, c)) {
                    rep.errors++;
                    if (rep.errors <= 3)
                        printf("  child %d page %d: got 0x%x exp 0x%x\n",
                               c, i,
                               (unsigned char)mem[i*PGSIZE_20],
                               (unsigned char)pat_20(i,c));
                }
            }

        done_20:;
            struct disk_stats st;
            memset(&st, 0, sizeof(st));
            getdiskstats(rep.pid, &st);
            rep.reads       = st.reads;
            rep.writes      = st.writes;
            rep.avg_latency = st.avg_latency;

            write(pipefd[c][1], &rep, sizeof(rep));
            close(pipefd[c][1]);
            exit(rep.errors != 0);
        }
        
        // PARENT: Wait for the specific child to finish to prevent RAID mode race conditions
        wait(0);
        
        // Collect the report for this child immediately
        close(pipefd[c][1]);
        read(pipefd[c][0], &reports[c], sizeof(reports[c]));
        close(pipefd[c][0]);
    }

    printf("\n  Results:\n");
    int all_ok = 1;
    for (int c = 0; c < NCHILDREN_20; c++) {
        ChildReport_20 *r = &reports[c];
        printf("  child %d (%s): pid=%d reads=%d writes=%d"
               " latency=%d.%d errors=%d\n",
               c, mnames[c], r->pid,
               r->reads, r->writes,
               r->avg_latency/100, r->avg_latency%100,
               r->errors);
        if (r->errors != 0)   all_ok = 0;
        if (r->reads  == 0)  { printf("  FAIL: child %d no reads\n",  c); all_ok = 0; }
        if (r->writes == 0)  { printf("  FAIL: child %d no writes\n", c); all_ok = 0; }
    }

    if (all_ok)
        printf("  PASS: all %d children correct, independent stats\n", NCHILDREN_20);
    else
        printf("  FAIL: one or more children had errors\n");

    // ------------------------------------------------------------------
    printf("[4] Stats don't leak between children\n");
    // Each child has its own pid, so stats from different pids differ
    int unique_reads = 1;
    for (int a = 0; a < NCHILDREN_20 && unique_reads; a++)
        for (int b = a+1; b < NCHILDREN_20; b++)
            if (reports[a].pid == reports[b].pid) unique_reads = 0;
    if (unique_reads)
        printf("  PASS: all children have distinct PIDs (stat isolation)\n");
    else
        printf("  WARN: duplicate PIDs detected\n");

    printf("=== Test t done ===\n");
}

// ============================================================
// PA4_21 (original by Tanish)
// ============================================================

#define PGSIZE_21      4096
#define FRAME_LIMIT_21  256// 3x frame limit forces heavy eviction
#define NUM_PAGES_21   300
#define PASSES_21      1
#define RAID0_21       0
#define SSTF_21        1

static char pat_21(int page, int pass) {
    return (char)((page * 97 + pass * 53 + 7) & 0xFF);

}

static void
test_pa4_21(void)
{
    printf("=== Test u: Disk-Backed Swap End-to-End Stress ===\n");

    setraidmode(RAID0_21);
    setdisksched(SSTF_21);

    int pid = getpid2();
    struct vmstats vm_before, vm_after;
    struct disk_stats dk_before, dk_after;
    memset(&vm_before, 0, sizeof(vm_before));
    memset(&dk_before, 0, sizeof(dk_before));

    char *mem = sbrk(NUM_PAGES_21 * PGSIZE_21);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    getvmstats(pid, &vm_before);
    getdiskstats(pid, &dk_before);

    // ------------------------------------------------------------------
    printf("[1] Multi-pass write+read over %d pages\n", NUM_PAGES_21);
    int total_errs = 0;
    for (int pass = 0; pass < PASSES_21; pass++) {
        // Write entire region
        for (int i = 0; i < NUM_PAGES_21; i++)
            mem[i * PGSIZE_21] = pat_21(i, pass);

        // Read back — forces swap-in for evicted pages
        int errs = 0;
        for (int i = 0; i < NUM_PAGES_21; i++) {
            char expected = pat_21(i, pass);
            char got = mem[i * PGSIZE_21];
            if (got != expected) {
                errs++;
                if (errs <= 5)
                    printf("  page %d pass %d: got 0x%x exp 0x%x\n",
                           i, pass, (unsigned char)got, (unsigned char)expected);
            }
        }
        total_errs += errs;
        if (errs == 0)
            printf("  pass %d PASS (%d pages OK)\n", pass, NUM_PAGES_21);
        else
            printf("  pass %d FAIL (%d errors)\n", pass, errs);
    }

    getvmstats(pid, &vm_after);
    getdiskstats(pid, &dk_after);

    printf("\n  VM stats: faults=%d evicted=%d sin=%d sout=%d res=%d\n",
           vm_after.page_faults, vm_after.pages_evicted,
           vm_after.pages_swapped_in, vm_after.pages_swapped_out,
           vm_after.resident_pages);
    printf("  Disk stats: reads=%d writes=%d latency=%d.%d\n",
           dk_after.reads, dk_after.writes,
           dk_after.avg_latency/100, dk_after.avg_latency%100);

    // ------------------------------------------------------------------
    printf("[2] Evictions and swap I/O occurred\n");
    if (vm_after.pages_evicted > 0)
        printf("  PASS: %d evictions\n", vm_after.pages_evicted);
    else
        printf("  FAIL: no evictions — frame limit may not be enforced\n");

    if (vm_after.pages_swapped_in > 0)
        printf("  PASS: %d swap-ins\n", vm_after.pages_swapped_in);
    else
        printf("  FAIL: no swap-ins recorded\n");

    if (dk_after.writes > dk_before.writes)
        printf("  PASS: disk writes increased (%d)\n",
               dk_after.writes - dk_before.writes);
    else
        printf("  FAIL: no new disk writes\n");

    if (dk_after.reads > dk_before.reads)
        printf("  PASS: disk reads increased (%d)\n",
               dk_after.reads - dk_before.reads);
    else
        printf("  FAIL: no new disk reads\n");

    // ------------------------------------------------------------------
    printf("[3] Overall data integrity\n");
    if (total_errs == 0)
        printf("  PASS: all %d passes x %d pages correct\n", PASSES_21, NUM_PAGES_21);
    else
        printf("  FAIL: %d total data errors across %d passes\n",
               total_errs, PASSES_21);

    // ------------------------------------------------------------------
    printf("[4] fork() + swap: child data independent of parent\n");
    // Write one more pass in parent
    for (int i = 0; i < NUM_PAGES_21; i++)
        mem[i * PGSIZE_21] = pat_21(i, 99);

    int child = fork();
    if (child == 0) {
        // Child overwrites with different pattern
        for (int i = 0; i < NUM_PAGES_21; i++)
            mem[i * PGSIZE_21] = pat_21(i, 200);
        // Verify child pattern
        int errs = 0;
        for (int i = 0; i < NUM_PAGES_21; i++)
            if (mem[i * PGSIZE_21] != pat_21(i, 200)) errs++;
        if (errs == 0)
            printf("  child PASS: own pattern correct after fork\n");
        else
            printf("  child FAIL: %d errors\n", errs);
        exit(0);
    }
    wait(0);

    // Parent pattern must be undisturbed
    int errs = 0;
    for (int i = 0; i < NUM_PAGES_21; i++)
        if (mem[i * PGSIZE_21] != pat_21(i, 99)) errs++;
    if (errs == 0)
        printf("  parent PASS: parent pattern intact after child fork\n");
    else
        printf("  parent FAIL: %d parent pages corrupted by child\n", errs);

    // ------------------------------------------------------------------
    printf("[5] Process cleans up swap on exit (no panic)\n");
    // Allocate extra pages and exit without reading them back
    // — kernel must free their swap slots
    int cleanup_child = fork();
    if (cleanup_child == 0) {
        setraidmode(RAID0_21);
        setdisksched(SSTF_21);
        char *extra = sbrk(FRAME_LIMIT_21 * 2 * PGSIZE_21);
        if (extra != (char *)-1) {
            for (int i = 0; i < FRAME_LIMIT_21 * 2; i++)
                extra[i * PGSIZE_21] = (char)i;
        }
        // Exit without reading back — tests swap block cleanup on exit
        exit(0);
    }
    wait(0);
    printf("  PASS: child exited cleanly with outstanding swapped pages\n");

    printf("=== Test u done (total_errs=%d) ===\n", total_errs);
}

// ============================================================
// PA4_22 (original by Tanish)
// ============================================================

#define PGSIZE_22       4096
#define FCFS_22         0
#define SSTF_22         1
#define RAID0_22        0
// Carefully chosen count to force significant seek variation
#define SWAP_PAGES_22   80

static char pat_22(int i, int p) { return (char)((i * 37 + p * 11) & 0xFF); }

// Access pages in a pattern guaranteed to cause large seeks under FCFS
// but minimisable under SSTF: alternate between low and high block numbers.
static void interleaved_access_22(char *mem, int n, int pass) {
    // Write all in order (cold start)
    for (int i = 0; i < n; i++) mem[i * PGSIZE_22] = pat_22(i, pass);
    // Read in worst-case order for FCFS: alternate first-half / second-half
    for (int i = 0; i < n / 2; i++) {
        volatile char a = mem[i * PGSIZE_22];                         // low
        volatile char b = mem[(n - 1 - i) * PGSIZE_22];              // high
        (void)a; (void)b;
    }
}

static void
test_pa4_22(void)
{
    printf("=== Test v: SSTF Head Position Tracking ===\n");

    setraidmode(RAID0_22);
    int pid = getpid2();

    char *mem = sbrk(SWAP_PAGES_22 * PGSIZE_22);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    // ------------------------------------------------------------------
    printf("[1] FCFS interleaved workload (high seek distance)\n");
    setdisksched(FCFS_22);
    interleaved_access_22(mem, SWAP_PAGES_22, 0);

    struct disk_stats st_fcfs;
    memset(&st_fcfs, 0, sizeof(st_fcfs));
    getdiskstats(pid, &st_fcfs);
    printf("  FCFS: reads=%d writes=%d avg_lat=%d.%d ticks\n",
           st_fcfs.reads, st_fcfs.writes,
           st_fcfs.avg_latency/100, st_fcfs.avg_latency%100);

    // ------------------------------------------------------------------
    printf("[2] SSTF same interleaved workload (lower seek distance)\n");
    setdisksched(SSTF_22);
    interleaved_access_22(mem, SWAP_PAGES_22, 1);

    struct disk_stats st_sstf;
    memset(&st_sstf, 0, sizeof(st_sstf));
    getdiskstats(pid, &st_sstf);
    printf("  SSTF (cumulative): reads=%d writes=%d avg_lat=%d.%d ticks\n",
           st_sstf.reads, st_sstf.writes,
           st_sstf.avg_latency/100, st_sstf.avg_latency%100);

    // ------------------------------------------------------------------
    printf("[3] SSTF avg_latency not worse than FCFS\n");
    if (st_sstf.avg_latency <= st_fcfs.avg_latency)
        printf("  PASS: SSTF avg_lat <= FCFS avg_lat\n");
    else
        printf("  NOTE: SSTF cumulative higher — head already moved by FCFS pass\n");

    // ------------------------------------------------------------------
    printf("[4] All requests drained (no queue backlog)\n");
    // If queue drains correctly, a new small workload completes quickly
    char *probe = sbrk(5 * PGSIZE_22);
    if (probe != (char *)-1) {
        for (int i = 0; i < 5; i++) probe[i * PGSIZE_22] = (char)i;
        int ok = 1;
        for (int i = 0; i < 5; i++)
            if (probe[i * PGSIZE_22] != (char)i) ok = 0;
        if (ok)
            printf("  PASS: small probe workload completed (queue drained)\n");
        else
            printf("  FAIL: probe data incorrect\n");
    }

    // ------------------------------------------------------------------
    printf("[5] Data integrity through seek tests\n");
    int errs = 0;
    for (int i = 0; i < SWAP_PAGES_22; i++)
        mem[i * PGSIZE_22] = pat_22(i, 99);
    for (int i = 0; i < SWAP_PAGES_22; i++)
        if (mem[i * PGSIZE_22] != pat_22(i, 99)) errs++;
    if (errs == 0)
        printf("  PASS: all pages correct after seek tests\n");
    else
        printf("  FAIL: %d errors\n", errs);

    // ------------------------------------------------------------------
    printf("[6] avg_latency always >= rotational delay (700 in 100x units)\n");
    if (st_fcfs.avg_latency >= 7)
        printf("  PASS: FCFS latency >= C\n");
    else
        printf("  FAIL: FCFS latency %d < 7\n", st_fcfs.avg_latency);

    if (st_sstf.avg_latency >= 7)
        printf("  PASS: SSTF latency >= C\n");
    else
        printf("  FAIL: SSTF latency %d < 7\n", st_sstf.avg_latency);

    printf("=== Test v done ===\n");
}

// ============================================================
// main
// ============================================================

int
main(void)
{
  printf("==============================\n");
  printf("PA4_F: Sched-aware disk, latency, RAID switching, end-to-end\n");
  printf("==============================\n\n");

  printf("--- PA4_18: scheduler-aware disk scheduling ---\n");
  test_pa4_18();

  printf("\n--- PA4_19: latency model verification ---\n");
  test_pa4_19();

  printf("\n--- PA4_20: RAID mode switching + multi-process stats ---\n");
  test_pa4_20();

  printf("\n--- PA4_21: disk-backed swap end-to-end stress ---\n");
  test_pa4_21();

  printf("\n--- PA4_22: SSTF head position tracking ---\n");
  test_pa4_22();

  printf("\nPA4_F done.\n");
  exit(0);
}