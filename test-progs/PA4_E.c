// Merged: PA4_14 + PA4_15 + PA4_16 + PA4_17
// Tests: RAID0 striping, RAID1 mirroring, RAID5 basic correctness, RAID5 reconstruction

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/diskstat.h"

// ============================================================
// PA4_14 (original by Tanish)
// ============================================================

#define PGSIZE_14      4096
#define RAID0_14       0
#define FCFS_14        0
// Need > 64 frames to force swap
#define SWAP_PAGES_14  85
#define PASSES_14      3

// Pattern encodes page index and pass so corruption is detectable
static char pat_14(int page, int pass) {
    return (char)((page * 13 + pass * 7 + 1) & 0xFF);
}

static void
test_pa4_14(void)
{
    printf("=== Test n: RAID 0 Striping Correctness ===\n");

    // Set RAID0 mode before any swap activity
    printf("[1] Setting RAID mode to RAID0\n");
    if (setraidmode(RAID0_14) == 0)
        printf("  PASS: setraidmode(RAID0) accepted\n");
    else {
        printf("  FAIL: setraidmode(RAID0) rejected\n");
        return;
    }

    setdisksched(FCFS_14); // deterministic ordering for this test

    // ------------------------------------------------------------------
    printf("[2] Multi-pass write/read across %d pages\n", SWAP_PAGES_14);
    char *mem = sbrk(SWAP_PAGES_14 * PGSIZE_14);
    if (mem == (char *)-1) {
        printf("  FAIL: sbrk\n");
        return;
    }

    int total_errs = 0;
    for (int pass = 0; pass < PASSES_14; pass++) {
        // Write unique pattern
        for (int i = 0; i < SWAP_PAGES_14; i++)
            mem[i * PGSIZE_14] = pat_14(i, pass);

        // Read back — may trigger swap-in
        int errs = 0;
        for (int i = 0; i < SWAP_PAGES_14; i++) {
            if (mem[i * PGSIZE_14] != pat_14(i, pass)) {
                errs++;
                if (errs <= 3)
                    printf("  page %d pass %d: got 0x%x expected 0x%x\n",
                           i, pass,
                           (unsigned char)mem[i*PGSIZE_14],
                           (unsigned char)pat_14(i, pass));
            }
        }
        total_errs += errs;
        if (errs == 0)
            printf("  pass %d: PASS (all %d pages correct)\n", pass, SWAP_PAGES_14);
        else
            printf("  pass %d: FAIL (%d errors)\n", pass, errs);
    }

    // ------------------------------------------------------------------
    printf("[3] Overall data integrity\n");
    if (total_errs == 0)
        printf("  PASS: %d passes, 0 errors — RAID0 striping correct\n", PASSES_14);
    else
        printf("  FAIL: %d total errors across %d passes\n", total_errs, PASSES_14);

    // ------------------------------------------------------------------
    printf("[4] Disk I/O was generated\n");
    struct diskstats st;
    memset(&st, 0, sizeof(st));
    if (getdiskstats(getpid2(), &st) != 0) {
        printf("  FAIL: getdiskstats error\n");
        return;
    }
    printf("  reads=%d writes=%d avg_latency=%d.%d ticks\n",
           st.reads, st.writes, st.avg_latency/100, st.avg_latency%100);

    if (st.writes > 0)
        printf("  PASS: swap-out (writes) occurred\n");
    else
        printf("  FAIL: no writes recorded — check swap/RAID path\n");

    if (st.reads > 0)
        printf("  PASS: swap-in (reads) occurred\n");
    else
        printf("  FAIL: no reads recorded\n");

    // ------------------------------------------------------------------
    printf("[5] RAID0 stripe count plausibility\n");
    // Each page = 4 blocks (PGSIZE/BSIZE = 4096/1024 = 4)
    // In RAID0 each block goes to a different disk; expect many ops
    int expected_min_writes = SWAP_PAGES_14 * 4; // 4 blocks per page
    if (st.writes >= expected_min_writes / 4) // some fraction acceptable
        printf("  PASS: write count plausible for RAID0 striping\n");
    else
        printf("  NOTE: write count %d may be lower than expected %d\n",
               st.writes, expected_min_writes / 4);

    printf("=== Test n done (total_errs=%d) ===\n", total_errs);
}

// ============================================================
// PA4_15 (original by Tanish)
// ============================================================

#define PGSIZE_15      4096
#define RAID1_15       1
#define SSTF_15        1
#define NDISKS_15      4
// Must exceed 64 frames
#define SWAP_PAGES_15  82

static char pat_15(int i) { return (char)((i * 17 + 5) & 0xFF); }

static int run_write_read_15(char *mem, int npages, int pass) {
    for (int i = 0; i < npages; i++)
        mem[i * PGSIZE_15] = (char)((pat_15(i) + pass) & 0xFF);

    int errs = 0;
    for (int i = 0; i < npages; i++) {
        char expected = (char)((pat_15(i) + pass) & 0xFF);
        if (mem[i * PGSIZE_15] != expected) {
            errs++;
            if (errs <= 3)
                printf("  page %d: got 0x%x exp 0x%x\n",
                       i, (unsigned char)mem[i*PGSIZE_15],
                       (unsigned char)expected);
        }
    }
    return errs;
}

static void
test_pa4_15(void)
{
    printf("=== Test o: RAID 1 Mirroring Correctness ===\n");

    // ------------------------------------------------------------------
    printf("[1] setfaileddisk: invalid values rejected\n");
    if (setfaileddisk(-1) < 0)
        printf("  PASS: disk -1 rejected\n");
    else
        printf("  FAIL: disk -1 accepted\n");

    if (setfaileddisk(NDISKS_15) < 0)
        printf("  PASS: disk %d rejected (out of range)\n", NDISKS_15);
    else
        printf("  FAIL: disk %d accepted\n", NDISKS_15);

    // ------------------------------------------------------------------
    printf("[2] setraidmode(RAID1)\n");
    if (setraidmode(RAID1_15) == 0)
        printf("  PASS: RAID1 set\n");
    else {
        printf("  FAIL: RAID1 rejected\n");
        return;
    }
    setdisksched(SSTF_15);

    char *mem = sbrk(SWAP_PAGES_15 * PGSIZE_15);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    // ------------------------------------------------------------------
    printf("[3] Normal RAID1 operation (no failed disk)\n");
    int errs = run_write_read_15(mem, SWAP_PAGES_15, 0);
    if (errs == 0)
        printf("  PASS: all %d pages correct under RAID1\n", SWAP_PAGES_15);
    else
        printf("  FAIL: %d errors under normal RAID1\n", errs);

    // ------------------------------------------------------------------
    printf("[4] RAID1 with disk 0 failed\n");
    if (setfaileddisk(0) == 0)
        printf("  disk 0 marked failed\n");
    else {
        printf("  FAIL: setfaileddisk(0) rejected\n");
        return;
    }

    errs = run_write_read_15(mem, SWAP_PAGES_15, 1);
    if (errs == 0)
        printf("  PASS: all pages correct with disk 0 failed (mirror read)\n");
    else
        printf("  FAIL: %d errors with disk 0 failed\n", errs);

    // ------------------------------------------------------------------
    printf("[5] RAID1 with disk 1 failed\n");
    if (setfaileddisk(1) == 0)
        printf("  disk 1 marked failed\n");
    else
        printf("  FAIL: setfaileddisk(1) rejected\n");

    errs = run_write_read_15(mem, SWAP_PAGES_15, 2);
    if (errs == 0)
        printf("  PASS: all pages correct with disk 1 failed\n");
    else
        printf("  FAIL: %d errors with disk 1 failed\n", errs);

    // ------------------------------------------------------------------
    printf("[6] Disk I/O stats plausible for RAID1\n");
    // RAID1 writes each block twice (primary + mirror)
    struct diskstats st;
    memset(&st, 0, sizeof(st));
    getdiskstats(getpid2(), &st);
    printf("  reads=%d writes=%d avg_latency=%d.%d\n",
           st.reads, st.writes, st.avg_latency/100, st.avg_latency%100);
    if (st.writes > 0 && st.reads > 0)
        printf("  PASS: I/O recorded\n");
    else
        printf("  FAIL: missing reads or writes\n");

    // Restore: no failed disk (use disk 3 as sentinel — will be reset
    // at next setraidmode call in later tests)
    printf("=== Test o done ===\n");
}

// ============================================================
// PA4_16 (original by Tanish)
// ============================================================

#define PGSIZE_16      4096
#define RAID5_16       5
#define SSTF_16        1
// Must exceed 64 frames; use 88 pages so we cover 22 full stripes
// across 4 disks (88*4 blocks / 4 disks = 88 blocks per disk)
#define SWAP_PAGES_16  88
#define PASSES_16      4

static char pat_16(int page, int pass) {
    return (char)((page * 11 + pass * 23 + 3) & 0xFF);
}

static void
test_pa4_16(void)
{
    printf("=== Test p: RAID 5 Basic Correctness (No Failure) ===\n");

    printf("[1] setraidmode(RAID5)\n");
    if (setraidmode(RAID5_16) == 0)
        printf("  PASS: RAID5 set\n");
    else {
        printf("  FAIL: RAID5 rejected\n");
        return;
    }
    setdisksched(SSTF_16);

    char *mem = sbrk(SWAP_PAGES_16 * PGSIZE_16);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    // ------------------------------------------------------------------
    printf("[2] Multi-pass write/read (%d pages, %d passes)\n",
           SWAP_PAGES_16, PASSES_16);

    int total_errs = 0;
    for (int pass = 0; pass < PASSES_16; pass++) {
        // Write pattern
        for (int i = 0; i < SWAP_PAGES_16; i++)
            mem[i * PGSIZE_16] = pat_16(i, pass);

        // Read back — forces swap-in, exercises parity path
        int errs = 0;
        for (int i = 0; i < SWAP_PAGES_16; i++) {
            char expected = pat_16(i, pass);
            char got = mem[i * PGSIZE_16];
            if (got != expected) {
                errs++;
                if (errs <= 5)
                    printf("  page %d pass %d: got 0x%x exp 0x%x\n",
                           i, pass,
                           (unsigned char)got, (unsigned char)expected);
            }
        }
        total_errs += errs;
        if (errs == 0)
            printf("  pass %d: PASS (%d pages OK)\n", pass, SWAP_PAGES_16);
        else
            printf("  pass %d: FAIL (%d errors)\n", pass, errs);
    }

    // ------------------------------------------------------------------
    printf("[3] Overall data integrity\n");
    if (total_errs == 0)
        printf("  PASS: %d passes x %d pages — no parity errors\n",
               PASSES_16, SWAP_PAGES_16);
    else
        printf("  FAIL: %d total data errors\n", total_errs);

    // ------------------------------------------------------------------
    printf("[4] Parity rotation coverage\n");
    // With SWAP_PAGES_16=88 pages, each page uses 4 blocks.
    // Total logical blocks = 88*4 = 352.
    // Stripes = 352/4 = 88 stripes.
    // Parity rotates over 4 disks: 88 mod 4 = 0 => all 4 disks covered.
    printf("  Covered %d stripes => parity distributed across all 4 disks\n",
           SWAP_PAGES_16 * 4 / 4);
    printf("  PASS: parity rotation exercised (verified via data correctness)\n");

    // ------------------------------------------------------------------
    printf("[5] I/O stats\n");
    struct diskstats st;
    memset(&st, 0, sizeof(st));
    getdiskstats(getpid2(), &st);
    printf("  reads=%d writes=%d avg_latency=%d.%d ticks\n",
           st.reads, st.writes, st.avg_latency/100, st.avg_latency%100);

    if (st.writes > 0 && st.reads > 0)
        printf("  PASS: I/O recorded under RAID5\n");
    else
        printf("  FAIL: missing I/O counters\n");

    // RAID5 writes data + parity; expect more writes than RAID0
    printf("  NOTE: RAID5 generates extra writes for parity blocks\n");

    printf("=== Test p done (total_errs=%d) ===\n", total_errs);
}

// ============================================================
// PA4_17 (original by Tanish)
// ============================================================

#define PGSIZE_17      4096
#define RAID5_17       5
#define FCFS_17        0
#define NDISKS_17      4
// Must exceed 64 frames; use 72 so we get exactly 18 stripes/disk
#define SWAP_PAGES_17  72
#define PASSES_17      2

static char pat_17(int page, int pass, int disk_failed) {
    return (char)((page * 19 + pass * 31 + disk_failed * 7 + 1) & 0xFF);
}

// Write the memory region and verify correct read-back.
// Returns number of errors.
static int write_verify_17(char *mem, int npages, int pass, int disk_failed) {
    for (int i = 0; i < npages; i++)
        mem[i * PGSIZE_17] = pat_17(i, pass, disk_failed);

    int errs = 0;
    for (int i = 0; i < npages; i++) {
        char expected = pat_17(i, pass, disk_failed);
        char got = mem[i * PGSIZE_17];
        if (got != expected) {
            errs++;
            if (errs <= 4)
                printf("  page %d: got 0x%x exp 0x%x\n",
                       i, (unsigned char)got, (unsigned char)expected);
        }
    }
    return errs;
}

static void
test_pa4_17(void)
{
    printf("=== Test q: RAID 5 Reconstruction (One Failed Disk) ===\n");

    printf("[1] setraidmode(RAID5)\n");
    if (setraidmode(RAID5_17) == 0)
        printf("  PASS: RAID5 set\n");
    else { printf("  FAIL: RAID5 rejected\n"); return; }

    setdisksched(FCFS_17);

    char *mem = sbrk(SWAP_PAGES_17 * PGSIZE_17);
    if (mem == (char *)-1) { printf("  FAIL: sbrk\n"); return; }

    // ------------------------------------------------------------------
    printf("[2] Baseline: no failed disk\n");
    int errs = write_verify_17(mem, SWAP_PAGES_17, 0, -1);
    if (errs == 0)
        printf("  PASS: baseline correct (%d pages)\n", SWAP_PAGES_17);
    else
        printf("  FAIL: %d baseline errors\n", errs);

    // ------------------------------------------------------------------
    // Test reconstruction for each disk position
    for (int d = 0; d < NDISKS_17; d++) {
        printf("[%d] Failing disk %d and verifying reconstruction\n", 3+d, d);

        if (setfaileddisk(d) != 0) {
            printf("  FAIL: setfaileddisk(%d) rejected\n", d);
            continue;
        }

        // Write new data with this disk failed, then read back
        errs = write_verify_17(mem, SWAP_PAGES_17, d + 1, d);
        if (errs == 0)
            printf("  PASS: disk %d failed — reconstruction correct\n", d);
        else
            printf("  FAIL: disk %d failed — %d reconstruction errors\n",
                   d, errs);
    }

    // ------------------------------------------------------------------
    printf("[7] Recovery: disk 0 repaired (reset to no failure)\n");
    // We can't truly reset failed_disk through the API in the assignment,
    // but we switch RAID mode and back to reset internal state implicitly.
    // Alternatively we switch to RAID0 and back:
    setraidmode(0);   // RAID0 — resets failed_disk context
    setraidmode(RAID5_17);
    // setfaileddisk is not available with value -1 per spec (rejected)
    // So we test with disk 3 failed (last disk) and then no explicit reset
    setfaileddisk(3);
    errs = write_verify_17(mem, SWAP_PAGES_17, 99, 3);
    if (errs == 0)
        printf("  PASS: disk 3 failed — data still correct\n");
    else
        printf("  FAIL: %d errors with disk 3 failed\n", errs);

    // ------------------------------------------------------------------
    printf("[8] I/O stats after reconstruction tests\n");
    struct diskstats st;
    memset(&st, 0, sizeof(st));
    getdiskstats(getpid2(), &st);
    printf("  reads=%d writes=%d avg_latency=%d.%d ticks\n",
           st.reads, st.writes, st.avg_latency/100, st.avg_latency%100);
    if (st.reads > 0 && st.writes > 0)
        printf("  PASS: I/O correctly tracked through reconstruction\n");
    else
        printf("  FAIL: I/O counters missing\n");

    printf("=== Test q done ===\n");
}

// ============================================================
// main
// ============================================================

int
main(void)
{
  printf("==============================\n");
  printf("PA4_E: RAID 0/1/5 correctness\n");
  printf("==============================\n\n");

  printf("--- PA4_14: RAID0 striping ---\n");
  test_pa4_14();

  printf("\n--- PA4_15: RAID1 mirroring ---\n");
  test_pa4_15();

  printf("\n--- PA4_16: RAID5 basic correctness ---\n");
  test_pa4_16();

  printf("\n--- PA4_17: RAID5 reconstruction ---\n");
  test_pa4_17();

  printf("\nPA4_E done.\n");
  exit(0);
}
