/**
 * rocc_accum_test.c - Linux test program for AccumulatorExample RoCC accelerator
 *
 * Tests all 4 functions (write/read/load/accumulate) across all 4 registers
 * on the FPGA under Linux. Uses custom0 opcode.
 *
 * Build:  make
 * Run:    ./rocc_accum_test
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rocc_accum.h"

static int test_num   = 0;
static int pass_count = 0;
static int fail_count = 0;

static void check(const char *name, uint64_t got, uint64_t expected)
{
	test_num++;
	if (got == expected) {
		pass_count++;
		printf("  [PASS] Test %2d: %s (got 0x%lx)\n", test_num, name, got);
	} else {
		fail_count++;
		printf("  [FAIL] Test %2d: %s (expected 0x%lx, got 0x%lx)\n",
		       test_num, name, expected, got);
	}
}

/* Memory data for load tests — must be naturally aligned 8-byte values */
static volatile uint64_t mem_data[] = {
	0x3421UL,
	0xDEADBEEFUL,
	0x1234567890ABCDEFUL,
	0x0UL
};

int main(void)
{
	uint64_t result;

	printf("=== AccumulatorExample RoCC Test (Linux / FPGA) ===\n");
	printf("Opcode: custom0 (0b0001011)\n");
	printf("Registers: 4 x 64-bit (indices 0-3)\n\n");

	/* ------------------------------------------------------------------ */
	printf("-- 1. Basic write and read --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 42);
	result = accum_read(0);
	check("write 42 to reg[0], read back", result, 42);

	accum_write(0, 0);
	result = accum_read(0);
	check("write 0 to reg[0], read back", result, 0);

	accum_write(0, 0xFFFFFFFFFFFFFFFFUL);
	result = accum_read(0);
	check("write all-ones to reg[0], read back", result, 0xFFFFFFFFFFFFFFFFUL);

	/* ------------------------------------------------------------------ */
	printf("\n-- 2. All 4 registers independent --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 100);
	accum_write(1, 200);
	accum_write(2, 300);
	accum_write(3, 400);

	check("reg[0] == 100", accum_read(0), 100);
	check("reg[1] == 200", accum_read(1), 200);
	check("reg[2] == 300", accum_read(2), 300);
	check("reg[3] == 400", accum_read(3), 400);

	/* ------------------------------------------------------------------ */
	printf("\n-- 3. Accumulate (add) --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 10);
	accum_add(0, 5);
	result = accum_read(0);
	check("reg[0] = 10, +5 => 15", result, 15);

	accum_add(0, 0);
	result = accum_read(0);
	check("accumulate +0 (no change)", result, 15);

	accum_add(0, 1);
	accum_add(0, 1);
	accum_add(0, 1);
	result = accum_read(0);
	check("three successive +1 => 18", result, 18);

	/* ------------------------------------------------------------------ */
	printf("\n-- 4. Accumulate across all registers --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 0);
	accum_write(1, 0);
	accum_write(2, 0);
	accum_write(3, 0);

	accum_add(0, 1);
	accum_add(1, 10);
	accum_add(2, 100);
	accum_add(3, 1000);

	check("reg[0] = 0 + 1", accum_read(0), 1);
	check("reg[1] = 0 + 10", accum_read(1), 10);
	check("reg[2] = 0 + 100", accum_read(2), 100);
	check("reg[3] = 0 + 1000", accum_read(3), 1000);

	/* ------------------------------------------------------------------ */
	printf("\n-- 5. Load from memory --\n");
	/* ------------------------------------------------------------------ */
	accum_load(0, (void *)&mem_data[0]);
	result = accum_read(0);
	check("load mem_data[0] (0x3421)", result, 0x3421UL);

	accum_load(1, (void *)&mem_data[1]);
	result = accum_read(1);
	check("load mem_data[1] (0xDEADBEEF)", result, 0xDEADBEEFUL);

	accum_load(2, (void *)&mem_data[2]);
	result = accum_read(2);
	check("load mem_data[2] (0x1234567890ABCDEF)", result, 0x1234567890ABCDEFUL);

	accum_load(3, (void *)&mem_data[3]);
	result = accum_read(3);
	check("load mem_data[3] (0x0)", result, 0x0UL);

	/* ------------------------------------------------------------------ */
	printf("\n-- 6. Load then accumulate --\n");
	/* ------------------------------------------------------------------ */
	accum_load(0, (void *)&mem_data[0]);
	accum_add(0, 2);
	result = accum_read(0);
	check("load 0x3421 + 2", result, 0x3421UL + 2);

	accum_load(1, (void *)&mem_data[1]);
	accum_add(1, 0x11111111UL);
	result = accum_read(1);
	check("load 0xDEADBEEF + 0x11111111", result, 0xDEADBEEFUL + 0x11111111UL);

	/* ------------------------------------------------------------------ */
	printf("\n-- 7. Overwrite sequence --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 999);
	accum_add(0, 1);
	result = accum_read(0);
	check("write 999, +1 => 1000", result, 1000);

	accum_load(0, (void *)&mem_data[0]);
	result = accum_read(0);
	check("load overwrites: reg[0] => 0x3421", result, 0x3421UL);

	/* ------------------------------------------------------------------ */
	printf("\n-- 8. Large values and unsigned overflow --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 0x7FFFFFFFFFFFFFFFUL);
	accum_add(0, 1);
	result = accum_read(0);
	check("0x7FFFFFFFFFFFFFFF + 1 => 0x8000000000000000",
	      result, 0x8000000000000000UL);

	accum_write(0, 0xFFFFFFFFFFFFFFFEUL);
	accum_add(0, 1);
	result = accum_read(0);
	check("0xFFFFFFFFFFFFFFFE + 1", result, 0xFFFFFFFFFFFFFFFFUL);

	accum_write(0, 0xFFFFFFFFFFFFFFFFUL);
	accum_add(0, 1);
	result = accum_read(0);
	check("0xFFFFFFFFFFFFFFFF + 1 (wrap to 0)", result, 0x0UL);

	/* ------------------------------------------------------------------ */
	printf("\n-- 9. Register isolation --\n");
	/* ------------------------------------------------------------------ */
	accum_write(0, 0xAAAAAAAAAAAAAAAAUL);
	accum_write(1, 0xBBBBBBBBBBBBBBBBUL);
	accum_write(2, 0xCCCCCCCCCCCCCCCCUL);
	accum_write(3, 0xDDDDDDDDDDDDDDDDUL);

	/* Modify only reg[1] */
	accum_add(1, 0x1111111111111111UL);

	check("reg[0] unchanged after add to reg[1]",
	      accum_read(0), 0xAAAAAAAAAAAAAAAAUL);
	check("reg[1] accumulated",
	      accum_read(1), 0xBBBBBBBBBBBBBBBBUL + 0x1111111111111111UL);
	check("reg[2] unchanged after add to reg[1]",
	      accum_read(2), 0xCCCCCCCCCCCCCCCCUL);
	check("reg[3] unchanged after add to reg[1]",
	      accum_read(3), 0xDDDDDDDDDDDDDDDDUL);

	/* ------------------------------------------------------------------ */
	printf("\n=== Results: %d/%d passed", pass_count, test_num);
	if (fail_count > 0)
		printf(", %d FAILED", fail_count);
	printf(" ===\n");

	return fail_count > 0 ? 1 : 0;
}
