#include <stdio.h>
#include "rocc.h"

static inline void accum_write(int idx, unsigned long data)
{
	ROCC_INSTRUCTION_SS(0, data, idx, 0);
}

static inline unsigned long accum_read(int idx)
{
	unsigned long value;
	ROCC_INSTRUCTION_DSS(0, value, 0, idx, 1);
	return value;
}

static inline void accum_load(int idx, void *ptr)
{
	asm volatile ("fence");
	ROCC_INSTRUCTION_SS(0, (uintptr_t) ptr, idx, 2);
}

static inline void accum_add(int idx, unsigned long addend)
{
	ROCC_INSTRUCTION_SS(0, addend, idx, 3);
}

static int test_num = 0;
static int pass_count = 0;
static int fail_count = 0;

static void check(const char *name, unsigned long got, unsigned long expected)
{
	test_num++;
	if (got == expected) {
		pass_count++;
		printf("  [PASS] Test %d: %s (got 0x%lx)\n", test_num, name, got);
	} else {
		fail_count++;
		printf("  [FAIL] Test %d: %s (expected 0x%lx, got 0x%lx)\n",
		       test_num, name, expected, got);
	}
}

unsigned long data[] = { 0x3421L, 0xDEADBEEFL, 0x1234567890ABCDEFL, 0x0L };

int main(void)
{
	unsigned long result;

	printf("=== AccumulatorExample RoCC Test Suite ===\n\n");

	/* ---- Test 1: Basic write and read ---- */
	printf("-- Test group: Basic write/read --\n");
	accum_write(0, 42);
	result = accum_read(0);
	check("write 42 to reg[0], read back", result, 42);

	accum_write(0, 0);
	result = accum_read(0);
	check("write 0 to reg[0], read back", result, 0);

	accum_write(0, 0xFFFFFFFFFFFFFFFFUL);
	result = accum_read(0);
	check("write all-ones to reg[0], read back", result, 0xFFFFFFFFFFFFFFFFUL);

	/* ---- Test 2: All 4 registers independent ---- */
	printf("\n-- Test group: All 4 registers --\n");
	accum_write(0, 100);
	accum_write(1, 200);
	accum_write(2, 300);
	accum_write(3, 400);

	check("reg[0] == 100", accum_read(0), 100);
	check("reg[1] == 200", accum_read(1), 200);
	check("reg[2] == 300", accum_read(2), 300);
	check("reg[3] == 400", accum_read(3), 400);

	/* ---- Test 3: Accumulate (add) ---- */
	printf("\n-- Test group: Accumulate --\n");
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

	/* ---- Test 4: Accumulate on different registers ---- */
	printf("\n-- Test group: Accumulate across registers --\n");
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

	/* ---- Test 5: Load from memory ---- */
	printf("\n-- Test group: Load from memory --\n");
	accum_load(0, &data[0]);
	result = accum_read(0);
	check("load data[0] (0x3421)", result, 0x3421L);

	accum_load(1, &data[1]);
	result = accum_read(1);
	check("load data[1] (0xDEADBEEF)", result, 0xDEADBEEFL);

	accum_load(2, &data[2]);
	result = accum_read(2);
	check("load data[2] (0x1234567890ABCDEF)", result, 0x1234567890ABCDEFL);

	accum_load(3, &data[3]);
	result = accum_read(3);
	check("load data[3] (0x0)", result, 0x0L);

	/* ---- Test 6: Load then accumulate ---- */
	printf("\n-- Test group: Load + accumulate --\n");
	accum_load(0, &data[0]);
	accum_add(0, 2);
	result = accum_read(0);
	check("load 0x3421 + 2", result, 0x3421L + 2);

	accum_load(1, &data[1]);
	accum_add(1, 0x11111111L);
	result = accum_read(1);
	check("load 0xDEADBEEF + 0x11111111", result, 0xDEADBEEFL + 0x11111111L);

	/* ---- Test 7: Write then accumulate then load (overwrite) ---- */
	printf("\n-- Test group: Overwrite sequence --\n");
	accum_write(0, 999);
	accum_add(0, 1);
	result = accum_read(0);
	check("write 999, +1 => 1000", result, 1000);

	accum_load(0, &data[0]);
	result = accum_read(0);
	check("load overwrites: reg[0] => 0x3421", result, 0x3421L);

	/* ---- Test 8: Large values and overflow ---- */
	printf("\n-- Test group: Large values --\n");
	accum_write(0, 0x7FFFFFFFFFFFFFFFUL);
	accum_add(0, 1);
	result = accum_read(0);
	check("0x7FFFFFFFFFFFFFFF + 1 (unsigned overflow to 0x8000..)",
	      result, 0x8000000000000000UL);

	accum_write(0, 0xFFFFFFFFFFFFFFFEUL);
	accum_add(0, 1);
	result = accum_read(0);
	check("0xFFFFFFFFFFFFFFFE + 1", result, 0xFFFFFFFFFFFFFFFFUL);

	accum_write(0, 0xFFFFFFFFFFFFFFFFUL);
	accum_add(0, 1);
	result = accum_read(0);
	check("0xFFFFFFFFFFFFFFFF + 1 (wrap to 0)", result, 0x0UL);

	/* ---- Test 9: Register isolation ---- */
	printf("\n-- Test group: Register isolation --\n");
	accum_write(0, 0xAAAAAAAAAAAAAAAAUL);
	accum_write(1, 0xBBBBBBBBBBBBBBBBUL);
	accum_write(2, 0xCCCCCCCCCCCCCCCCUL);
	accum_write(3, 0xDDDDDDDDDDDDDDDDUL);

	accum_add(1, 0x1111111111111111UL);

	check("reg[0] unchanged after add to reg[1]",
	      accum_read(0), 0xAAAAAAAAAAAAAAAAUL);
	check("reg[1] accumulated",
	      accum_read(1), 0xBBBBBBBBBBBBBBBBUL + 0x1111111111111111UL);
	check("reg[2] unchanged after add to reg[1]",
	      accum_read(2), 0xCCCCCCCCCCCCCCCCUL);
	check("reg[3] unchanged after add to reg[1]",
	      accum_read(3), 0xDDDDDDDDDDDDDDDDUL);

	/* ---- Summary ---- */
	printf("\n=== Results: %d/%d passed", pass_count, test_num);
	if (fail_count > 0)
		printf(", %d FAILED", fail_count);
	printf(" ===\n");

	return fail_count > 0 ? 1 : 0;
}
