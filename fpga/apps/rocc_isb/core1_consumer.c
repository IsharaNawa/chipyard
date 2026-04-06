/**
 * core1_consumer.c - Consumer running on isolated Core 1
 *
 * Reads 10 values from Core 0 via the RoCC ISB and prints them.
 * Uses custom1 instructions to pop data from the ISB FIFO.
 * Must be pinned to Core 1 (which has the ISBReaderRoCC accelerator).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include "rocc_isb.h"

#define NUM_VALUES 10
#define CORE_ID 1

/**
 * Pin this process to a specific CPU core.
 */
static void pin_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("Failed to set CPU affinity");
        exit(1);
    }

    /* Verify */
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpuset), &cpuset) == 0) {
        if (CPU_ISSET(core_id, &cpuset)) {
            printf("[Core %d Consumer] Successfully pinned to Core %d (ISOLATED)\n",
                   core_id, core_id);
        } else {
            fprintf(stderr, "[Core %d Consumer] Warning: core pinning not verified\n",
                    core_id);
        }
    }
}

int main(void)
{
    printf("=== Core 1 Consumer Started ===\n");

    pin_to_core(CORE_ID);

    /* Timing */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /*
     * Read values from the RoCC ISB.
     * IMPORTANT: We must NOT call isb_read() (blocking) when the FIFO is empty.
     * A blocking RoCC read stalls the CPU pipeline at the hardware level,
     * which prevents Linux from handling interrupts/scheduling on this core
     * and causes RCU stall warnings.
     *
     * Instead, poll with isb_read_query() (non-blocking, funct=1) until data
     * is available, then issue the blocking read which will complete instantly.
     */
    printf("\n[Core 1] Waiting for %d values from Core 0 via RoCC ISB...\n", NUM_VALUES);

    uint64_t received[NUM_VALUES];
    for (int i = 0; i < NUM_VALUES; i++) {
        /* Spin-poll until FIFO has data, yielding to kernel between checks */
        while (isb_read_query() == 0) {
            sched_yield();
        }
        /* Data is available — blocking read completes immediately */
        received[i] = isb_read();
        printf("[Core 1]   Received [%d] = %lu\n", i, received[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    /* Print summary */
    printf("\n[Core 1] All %d values received:\n", NUM_VALUES);
    int pass = 1;
    for (int i = 0; i < NUM_VALUES; i++) {
        uint64_t expected = (uint64_t)(i + 1) * 100;
        const char *status = (received[i] == expected) ? "OK" : "MISMATCH";
        if (received[i] != expected) pass = 0;
        printf("  [%d] got=%lu expected=%lu  %s\n", i, received[i], expected, status);
    }

    printf("\n[Core 1] Result: %s\n", pass ? "PASS" : "FAIL");
    printf("[Core 1] Elapsed time: %.6f seconds\n", elapsed);
    printf("\n=== Core 1 Consumer Finished ===\n");

    return pass ? 0 : 1;
}
