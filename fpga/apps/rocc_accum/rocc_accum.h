/**
 * rocc_accum.h - RoCC AccumulatorExample instruction macros for Linux userspace
 *
 * Provides inline assembly wrappers for the AccumulatorExample RoCC accelerator
 * mapped to the custom0 opcode (0b0001011).
 *
 * The accelerator has a 4-entry (indices 0-3) 64-bit register file.
 *
 * Functions (funct field):
 *   funct=0 (write):  reg[rs2] = rs1
 *   funct=1 (read):   rd = reg[rs2]            (returns current value)
 *   funct=2 (load):   reg[rs2] = mem[rs1]      (loads 8 bytes via D$ port)
 *   funct=3 (accum):  reg[rs2] = reg[rs2] + rs1
 */

#ifndef ROCC_ACCUM_H
#define ROCC_ACCUM_H

#include <stdint.h>

/* ── RoCC custom instruction encoding helpers ── */

#define STR1(x) #x
#define STR(x)  STR1(x)
#define EXTRACT(a, size, offset) (((~(~0 << size) << offset) & a) >> offset)

#define CUSTOM_0 0b0001011

#define CUSTOMX(X, xd, xs1, xs2, rd, rs1, rs2, funct) \
  (CUSTOM_ ## X)                              |       \
  ((rd)                 << (7))               |       \
  ((xs2)                << (7+5))             |       \
  ((xs1)                << (7+5+1))           |       \
  ((xd)                 << (7+5+2))           |       \
  ((rs1)                << (7+5+3))           |       \
  ((rs2)                << (7+5+3+5))         |       \
  (EXTRACT(funct, 7, 0) << (7+5+3+5+5))

/**
 * ROCC_INSTRUCTION_SS - send rs1 and rs2, no rd output
 * Used for: write (funct=0), load (funct=2), accum (funct=3)
 */
#define ROCC_INSTRUCTION_SS(X, rs1, rs2, funct) {                             \
    register uint64_t rs1_ asm ("x11") = (uint64_t)(rs1);                     \
    register uint64_t rs2_ asm ("x12") = (uint64_t)(rs2);                     \
    asm volatile (                                                            \
        ".word " STR(CUSTOMX(X, 0, 1, 1, 0, 11, 12, funct)) "\n\t"           \
        :: [_rs1] "r" (rs1_), [_rs2] "r" (rs2_));                             \
  }

/**
 * ROCC_INSTRUCTION_DSS - send rs1 and rs2, receive rd
 * Used for: read (funct=1)
 */
#define ROCC_INSTRUCTION_DSS(X, rd, rs1, rs2, funct) {                        \
    register uint64_t rd_  asm ("x10");                                       \
    register uint64_t rs1_ asm ("x11") = (uint64_t)(rs1);                     \
    register uint64_t rs2_ asm ("x12") = (uint64_t)(rs2);                     \
    asm volatile (                                                            \
        ".word " STR(CUSTOMX(X, 1, 1, 1, 10, 11, 12, funct)) "\n\t"          \
        : "=r" (rd_)                                                          \
        : [_rs1] "r" (rs1_), [_rs2] "r" (rs2_));                              \
    rd = rd_;                                                                 \
  }

/* ── Convenience wrappers ── */

/** Write value to accumulator register idx (0-3) */
static inline void accum_write(int idx, uint64_t data)
{
	ROCC_INSTRUCTION_SS(0, data, idx, 0);
}

/** Read current value of accumulator register idx (0-3) */
static inline uint64_t accum_read(int idx)
{
	uint64_t value;
	ROCC_INSTRUCTION_DSS(0, value, 0, idx, 1);
	return value;
}

/** Load 8 bytes from memory address ptr into accumulator register idx (0-3) */
static inline void accum_load(int idx, void *ptr)
{
	asm volatile ("fence");
	ROCC_INSTRUCTION_SS(0, (uintptr_t)ptr, idx, 2);
}

/** Add addend to accumulator register idx (0-3): reg[idx] += addend */
static inline void accum_add(int idx, uint64_t addend)
{
	ROCC_INSTRUCTION_SS(0, addend, idx, 3);
}

#endif /* ROCC_ACCUM_H */
