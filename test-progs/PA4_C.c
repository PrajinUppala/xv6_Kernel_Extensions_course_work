// Merged: PA4_5 + PA4_9 + PA4_10
// Tests: scheduler-aware eviction, multi-process memory pressure, PTE isolation across children

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vmstats.h"

// ============================================================
// PA4_5 (original by Tanish)
// ============================================================

#define PAGE_SIZE_5  4096
#define WORK_PAGES 40      // working set per child
#define SPIN_ITERS 800000000 // enough to burn through MLFQ quanta

static void
test_pa4_5(void)
{
    int pipe_lo[2], pipe_hi[2];
    pipe(pipe_lo);
    pipe(pipe_hi);

    printf("=== test_sched_aware: Scheduler-Aware Eviction Test ===\n");

    // --- Fork LOW-priority child ---
    int pid_lo = fork();
    if (pid_lo == 0) {
        close(pipe_lo[0]);
        close(pipe_hi[0]); close(pipe_hi[1]);

        // Burn CPU to drop MLFQ priority
        volatile int x = 0;
        for (int i = 0; i < SPIN_ITERS; i++) x++;

        // Allocate and touch working set
        char *mem = sbrk((uint64)WORK_PAGES * PAGE_SIZE_5);
        for (int i = 0; i < WORK_PAGES; i++)
            mem[i * PAGE_SIZE_5] = (char)i;

        // Signal parent we're ready
        int mypid = getpid();
        write(pipe_lo[1], &mypid, sizeof(mypid));
        close(pipe_lo[1]);

        // Keep working set alive while parent applies pressure
        pause(20);

        // Verify data integrity after potential evictions
        int errs = 0;
        for (int i = 0; i < WORK_PAGES; i++)
            if (mem[i * PAGE_SIZE_5] != (char)i) errs++;
        if (errs == 0)
            printf("[LO-child] PASS: data intact after pressure\n");
        else
            printf("[LO-child] WARN: %d pages corrupted\n", errs);

        exit(0);
    }

    // --- Fork HIGH-priority child ---
    int pid_hi = fork();
    if (pid_hi == 0) {
        close(pipe_hi[0]);
        close(pipe_lo[0]); close(pipe_lo[1]);

        // Immediately allocate working set (stays at top MLFQ queue)
        char *mem = sbrk((uint64)WORK_PAGES * PAGE_SIZE_5);
        for (int i = 0; i < WORK_PAGES; i++)
            mem[i * PAGE_SIZE_5] = (char)(i + 100);

        int mypid = getpid();
        write(pipe_hi[1], &mypid, sizeof(mypid));
        close(pipe_hi[1]);

        pause(20);

        int errs = 0;
        for (int i = 0; i < WORK_PAGES; i++)
            if (mem[i * PAGE_SIZE_5] != (char)(i + 100)) errs++;
        if (errs == 0)
            printf("[HI-child] PASS: data intact after pressure\n");
        else
            printf("[HI-child] WARN: %d pages corrupted\n", errs);

        exit(0);
    }

    // --- Parent: wait for both children to be ready ---
    close(pipe_lo[1]); close(pipe_hi[1]);

    int cpid_lo, cpid_hi;
    read(pipe_lo[0], &cpid_lo, sizeof(cpid_lo));
    read(pipe_hi[0], &cpid_hi, sizeof(cpid_hi));
    close(pipe_lo[0]); close(pipe_hi[0]);

    printf("LO-priority child PID: %d\n", cpid_lo);
    printf("HI-priority child PID: %d\n", cpid_hi);

    // Apply memory pressure: allocate a large region to push others out
    int pressure_pages = 80;
    char *pressure = sbrk((uint64)pressure_pages * PAGE_SIZE_5);
    for (int i = 0; i < pressure_pages; i++)
        pressure[i * PAGE_SIZE_5] = (char)i;

    pause(5); // let eviction settle

    // Read stats for both children
    struct vmstats st_lo, st_hi;
    if (getvmstats(cpid_lo, &st_lo) != 0) {
        printf("FAIL: getvmstats for lo-child %d failed\n", cpid_lo);
    } else {
        printf("\n[LO-priority child %d]\n", cpid_lo);
        printf("  pages_evicted   : %d\n", st_lo.pages_evicted);
        printf("  pages_swapped_out:%d\n", st_lo.pages_swapped_out);
        printf("  resident_pages  : %d\n", st_lo.resident_pages);
    }

    if (getvmstats(cpid_hi, &st_hi) != 0) {
        printf("FAIL: getvmstats for hi-child %d failed\n", cpid_hi);
    } else {
        printf("[HI-priority child %d]\n", cpid_hi);
        printf("  pages_evicted   : %d\n", st_hi.pages_evicted);
        printf("  pages_swapped_out:%d\n", st_hi.pages_swapped_out);
        printf("  resident_pages  : %d\n", st_hi.resident_pages);
    }

    if (st_lo.pages_evicted >= st_hi.pages_evicted)
        printf("\nPASS: LO-priority process lost >= pages than HI-priority\n");
    else
        printf("\nFAIL: HI-priority process lost more pages than LO — check scheduler-aware eviction\n");

    // Reap children
    wait(0); wait(0);
    printf("=== test_sched_aware done ===\n");
}

// ============================================================
// PA4_9 (original by Tanish)
// ============================================================

#define PGSIZE_9     4096
#define NPAGES_9     64    // per child; 3 children = 105 pages >> NFRAME=64
#define NCHILDREN_9  3

// Each child uses a different pattern so cross-contamination is visible
#define PATTERN_9(child, page) ((char)((child) * 37 + (page) * 11 + 1))

static int
child_work_9(int id)
{
    char *mem = sbrklazy(NPAGES_9 * PGSIZE_9);
    if (mem == SBRK_ERROR) {
        printf("FAIL: child %d sbrklazy failed\n", id);
        return 1;
    }

    // Write
    for (int i = 0; i < NPAGES_9; i++) {
        mem[i * PGSIZE_9] = PATTERN_9(id, i);
    }

    // Read back -- may trigger swap-in if frames were stolen
    int ok = 1;
    for (int i = 0; i < NPAGES_9; i++) {
        char got = mem[i * PGSIZE_9];
        if (got != PATTERN_9(id, i)) {
            printf("FAIL: child %d page %d: expected 0x%x got 0x%x\n",
                   id, i,
                   (unsigned char)PATTERN_9(id, i),
                   (unsigned char)got);
            ok = 0;
        }
    }

    if (ok)
        printf("PASS: child %d all %d pages correct under pressure\n",
               id, NPAGES_9);
    else
        printf("FAIL: child %d data corrupted under pressure\n", id);

    // Print this child's own stats
    struct vmstats s;
    if (getvmstats(getpid(), &s) == 0) {
        printf("INFO: child %d -- faults=%d evicted=%d swapped_out=%d swapped_in=%d resident=%d\n",
               id, s.page_faults, s.pages_evicted,
               s.pages_swapped_out, s.pages_swapped_in, s.resident_pages);
    }

    return ok ? 0 : 1;
}

static void
test_pa4_9(void)
{
    printf("=== vmswap5: Multi-Process Memory Pressure ===\n");

    int pids[NCHILDREN_9];

    for (int i = 0; i < NCHILDREN_9; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            printf("FAIL: fork %d failed\n", i);
            exit(1);
        }
        if (pids[i] == 0) {
            exit(child_work_9(i));
        }
    }

    int all_ok = 1;
    for (int i = 0; i < NCHILDREN_9; i++) {
        int status = -1;
        wait(&status);
        if (status != 0) {
            printf("FAIL: child %d exited with status %d\n", i, status);
            all_ok = 0;
        }
    }

    if (all_ok)
        printf("PASS: all %d children survived memory pressure with correct data\n",
               NCHILDREN_9);
    else
        printf("FAIL: one or more children reported corruption under pressure\n");

    // Parent's own stats (may show eviction if it also touched memory)
    struct vmstats ps;
    if (getvmstats(getpid(), &ps) == 0) {
        printf("INFO: parent -- faults=%d evicted=%d swapped_out=%d swapped_in=%d resident=%d\n",
               ps.page_faults, ps.pages_evicted,
               ps.pages_swapped_out, ps.pages_swapped_in, ps.resident_pages);
    }

    printf("=== vmswap5 done ===\n");
}

// ============================================================
// PA4_10 (original by Tanish)
// ============================================================

#define PAGE_SIZE_10 4096
#define MAXFRAMES_10 64
#define N_CHILDREN_10 3
#define PAGES_EACH 30 // slightly over each child's share

typedef struct
{
    int pid;
    int page_faults;
    int pages_evicted;
    int pages_swapped_out;
    int pages_swapped_in;
    int resident_pages;
    int errors;
} ChildReport_10;

static void
test_pa4_10(void)
{
    printf("=== Test 7: PTE update isolation across %d children ===\n", N_CHILDREN_10);
    printf("    PAGES_EACH=%d  MAXFRAMES=%d\n", PAGES_EACH, MAXFRAMES_10);

    // Shared-memory report array via pre-fork pipe per child
    int pipes[N_CHILDREN_10][2];
    for (int i = 0; i < N_CHILDREN_10; i++)
        pipe(pipes[i]);

    for (int c = 0; c < N_CHILDREN_10; c++)
    {
        int pid = fork();
        if (pid == 0)
        {
            // Close all pipes except ours (write end)
            for (int i = 0; i < N_CHILDREN_10; i++)
            {
                close(pipes[i][0]);
                if (i != c)
                    close(pipes[i][1]);
            }

            ChildReport_10 rep;
            rep.pid = getpid();
            rep.errors = 0;

            // Allocate PAGES_EACH pages, write sentinels
            char *mem = sbrklazy((long)PAGES_EACH * PAGE_SIZE_10);
            if (mem == (char *)-1)
            {
                rep.errors = 99;
                goto done_10;
            }

            for (int i = 0; i < PAGES_EACH; i++)
                mem[i * PAGE_SIZE_10] = (char)((c * 100 + i) & 0xFF);

            // Force some evictions by doing a second pass with different values
            for (int i = 0; i < PAGES_EACH; i++)
                mem[i * PAGE_SIZE_10] = (char)((c * 100 + i + 1) & 0xFF);

            // Verify values (swap-in any evicted pages)
            for (int i = 0; i < PAGES_EACH; i++)
            {
                char exp = (char)((c * 100 + i + 1) & 0xFF);
                if (mem[i * PAGE_SIZE_10] != exp)
                {
                    printf("  child %d ERROR: mem[%d*PAGE_SIZE] = %d != expected %d\n",
                           c, i, mem[i * PAGE_SIZE_10], exp);
                    printf("hello world\n");
                    rep.errors++;
                }
            }

            // Capture stats for THIS child's pid only
            struct vmstats s;
            if (getvmstats(rep.pid, &s) != 0)
            {
                rep.errors++;
            }
            else
            {
                rep.page_faults = s.page_faults;
                rep.pages_evicted = s.pages_evicted;
                rep.pages_swapped_out = s.pages_swapped_out;
                rep.pages_swapped_in = s.pages_swapped_in;
                rep.resident_pages = s.resident_pages;
            }

            // Verify stats are non-zero and plausible
            if (s.page_faults < PAGES_EACH)
                printf("  child %d WARN: page_faults=%d < PAGES_EACH=%d\n",
                       c, s.page_faults, PAGES_EACH);

        done_10:
            printf("child %d writing rep: pid=%d faults=%d evicted=%d\n",
       c, rep.pid, rep.page_faults, rep.pages_evicted);
            write(pipes[c][1], &rep, sizeof(rep));
            close(pipes[c][1]);
            exit(0);
        }
    }

    // Parent: collect reports
    ChildReport_10 reports[N_CHILDREN_10];
    for (int c = 0; c < N_CHILDREN_10; c++)
    {
        close(pipes[c][1]);
        read(pipes[c][0], &reports[c], sizeof(ChildReport_10));
        close(pipes[c][0]);
    }
    for (int c = 0; c < N_CHILDREN_10; c++)
        wait(0);

    printf("\n[results]\n");
    int all_ok = 1;
    for (int c = 0; c < N_CHILDREN_10; c++)
    {
        ChildReport_10 *r = &reports[c];
        printf("  child %d (pid=%d): faults=%d evicted=%d sout=%d sin=%d ",
               c, r->pid,
               r->page_faults, r->pages_evicted,
               r->pages_swapped_out, r->pages_swapped_in);

        printf("res=%d errors=%d\n",
               r->resident_pages, r->errors);
        if (r->errors != 0)
            all_ok = 0;
    }

    // Cross-child isolation: each child's faults should be at least PAGES_EACH
    for (int c = 0; c < N_CHILDREN_10; c++)
    {
        if (reports[c].page_faults < PAGES_EACH)
        {
            printf("  FAIL: child %d faults=%d < expected %d\n",
                   c, reports[c].page_faults, PAGES_EACH);
            all_ok = 0;
        }
        else
        {
            printf("  PASS: child %d faults=%d >= %d\n",
                   c, reports[c].page_faults, PAGES_EACH);
        }
    }

    // getvmstats isolation: a child should NOT see another child's faults
    // (compare counts — if any two children have IDENTICAL non-zero stats,
    //  it may indicate shared accounting, which is a bug)
    for (int a = 0; a < N_CHILDREN_10; a++)
    {
        for (int b = a + 1; b < N_CHILDREN_10; b++)
        {
            if (reports[a].page_faults == reports[b].page_faults &&
                reports[a].page_faults > 0 &&
                reports[a].page_faults != PAGES_EACH)
            { // add this condition
                printf("  WARN: child %d and %d have identical fault counts – check isolation\n",
                       a, b);
            }
        }
    }

    if (all_ok)
        printf("  PASS: all children verified correctly\n");

    printf("=== Test 7 done ===\n");
}

// ============================================================
// main
// ============================================================

int
main(void)
{
  printf("==============================\n");
  printf("PA4_C: Multi-process pressure & stat isolation\n");
  printf("==============================\n\n");

  printf("--- PA4_5: scheduler-aware eviction ---\n");
  test_pa4_5();

  printf("\n--- PA4_9: multi-process memory pressure ---\n");
  test_pa4_9();

  printf("\n--- PA4_10: PTE isolation across children ---\n");
  test_pa4_10();

  printf("\nPA4_C done.\n");
  exit(0);
}
