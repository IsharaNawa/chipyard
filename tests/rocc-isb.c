/*
 * RoCC ISB (Inter-Stage Buffer) test program.
 *
 * Tests the point-to-point RoCC ISB between two cores:
 *   Core 0 (hart 0): writer — pushes values into the ISB via custom0
 *   Core 1 (hart 1): reader — pops values from the ISB via custom1
 *
 * RoCC instruction encoding:
 *   Writer (custom0): funct=0 → blocking write rs1, funct=1 → query has_space
 *   Reader (custom1): funct=0 → blocking read  → rd, funct=1 → query has_data
 *
 * IMPORTANT: In baremetal mode with htif_nano.specs, secondary harts call
 * __main() (NOT main()). We override __main to run per-core logic.
 * printf is NOT thread-safe over HTIF — only Core 0 does printf.
 *
 * Test sequence:
 *   1. Core 0 writes values 0x100..0x100+N-1 into the ISB
 *   2. Core 1 reads N values and verifies them (no printf)
 *   3. Core 1 signals pass/fail via a shared flag
 *   4. Core 0 waits for core 1 to finish, prints results, and returns
 *
 * Return: 0 = pass, nonzero = fail
 */

#include <stdio.h>
#include <stdint.h>
#include "rocc.h"

#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

/* Number of values to transfer through the ISB */
#define NUM_VALUES 16

/* Base value — writer sends BASE+0, BASE+1, ..., BASE+(N-1) */
#define BASE_VALUE 0x100

/*
 * Shared synchronization variables (volatile for cross-core visibility)
 *   core1_result: 0 = not done, 1 = pass, 2 = fail
 *   core1_fail_idx: which index failed (if core1_result == 2)
 *   core1_fail_got: what value was received (if core1_result == 2)
 */
static volatile uint64_t core1_result   = 0;
static volatile uint64_t core1_fail_idx = 0;
static volatile uint64_t core1_fail_got = 0;

/* ── ISB Writer helpers (custom0) ── */

static inline void isb_write(uint64_t data)
{
    /* funct=0: blocking write — stalls core if FIFO full */
    ROCC_INSTRUCTION_S(0, data, 0);
}

static inline uint64_t isb_write_query(void)
{
    /* funct=1: non-blocking query — returns 1 if FIFO has space */
    uint64_t result;
    ROCC_INSTRUCTION_D(0, result, 1);
    return result;
}

/* ── ISB Reader helpers (custom1) ── */

static inline uint64_t isb_read(void)
{
    /* funct=0: blocking read — stalls core if FIFO empty */
    uint64_t data;
    ROCC_INSTRUCTION_D(1, data, 0);
    return data;
}

static inline uint64_t isb_read_query(void)
{
    /* funct=1: non-blocking query — returns 1 if FIFO has data */
    uint64_t result;
    ROCC_INSTRUCTION_D(1, result, 1);
    return result;
}

/*
 * __main — Override the default __main (WFI loop) from libgloss_htif.
 *
 * In the htif_nano.specs startup:
 *   Hart 0: _start_main → main() → __main()
 *   Hart 1+: _start_secondary → __main() (directly, NOT via main())
 *
 * So ALL harts execute __main. Hart 0 also calls it from main().
 */
void __main(void)
{
    uint64_t hartid = read_csr(mhartid);

    /* Cores beyond 1 just spin */
    if (hartid > 1) {
        while (1);
    }

    if (hartid == 0) {
        /* ── Core 0: Writer ── */
        printf("[Core 0] ISB Writer: sending %d values\n", NUM_VALUES);

        for (int i = 0; i < NUM_VALUES; i++) {
            isb_write((uint64_t)(BASE_VALUE + i));
        }

        printf("[Core 0] ISB Writer: all %d values sent, waiting for reader\n", NUM_VALUES);

        /* Wait for core 1 to report its result */
        while (core1_result == 0) {
            asm volatile ("fence");
        }

        /* Core 1 is done — safe to printf now (Core 1 is spinning, not using HTIF) */
        if (core1_result == 1) {
            printf("[Core 0] Reader verified all %d values. TEST PASSED\n", NUM_VALUES);
        } else {
            printf("[Core 0] Reader FAILED at index %lu: expected 0x%lx got 0x%lx\n",
                   core1_fail_idx, (uint64_t)(BASE_VALUE + core1_fail_idx), core1_fail_got);
        }
    } else {
        /* ── Core 1: Reader (NO printf — HTIF is not multi-core safe) ── */
        for (int i = 0; i < NUM_VALUES; i++) {
            uint64_t expected = (uint64_t)(BASE_VALUE + i);
            uint64_t got = isb_read();

            if (got != expected) {
                core1_fail_idx = (uint64_t)i;
                core1_fail_got = got;
                asm volatile ("fence");
                core1_result = 2; /* fail */
                while (1);
            }
        }

        asm volatile ("fence");
        core1_result = 1; /* pass */

        /* Spin forever — only core 0 returns from __main and then from main */
        while (1);
    }
}

int main(void)
{
    __main();
    return (core1_result == 1) ? 0 : 1;
}
