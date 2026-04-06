/**
 * core0_producer.c - Producer running on Core 0
 *
 * Sends 10 values (100, 200, ..., 1000) to Core 1 via the RoCC ISB.
 * Uses custom0 instructions to push data into the ISB FIFO.
 * Must be pinned to Core 0 (which has the ISBWriterRoCC accelerator).
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
#define CORE_ID 0

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
            printf("[Core %d Producer] Successfully pinned to Core %d\n",
                   core_id, core_id);
        } else {
            fprintf(stderr, "[Core %d Producer] Warning: core pinning not verified\n",
                    core_id);
        }
    }
}

int main(void)
{
    printf("=== Core 0 Producer Started ===\n");

    pin_to_core(CORE_ID);

    /* Generate the values: 100, 200, 300, ..., 1000 */
    uint64_t values[NUM_VALUES];
    printf("\n[Core 0] Values to send:\n");
    for (int i = 0; i < NUM_VALUES; i++) {
        values[i] = (uint64_t)(i + 1) * 100;
        printf("  [%d] = %lu\n", i, values[i]);
    }

    /* Timing */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Send all values through the RoCC ISB (custom0, funct=0 blocking write) */
    printf("\n[Core 0] Sending %d values to Core 1 via RoCC ISB...\n", NUM_VALUES);
    for (int i = 0; i < NUM_VALUES; i++) {
        isb_write(values[i]);
        printf("[Core 0]   Sent [%d] = %lu\n", i, values[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n[Core 0] All %d values sent successfully!\n", NUM_VALUES);
    printf("[Core 0] Elapsed time: %.6f seconds\n", elapsed);
    printf("\n=== Core 0 Producer Finished ===\n");

    return 0;
}
