/**
 * rocc_isb.h - RoCC ISB (Inter-Stage Buffer) instruction macros for Linux userspace
 *
 * Provides inline assembly wrappers for the RoCC custom instructions
 * used by the ISBWriterRoCC (custom0) and ISBReaderRoCC (custom1) accelerators.
 *
 * Writer (Core 0, custom0):
 *   funct=0: blocking write — pushes rs1 into the FIFO (stalls if full)
 *   funct=1: query status  — returns 1 in rd if FIFO has space, 0 if full
 *
 * Reader (Core 1, custom1):
 *   funct=0: blocking read  — pops data from the FIFO into rd (stalls if empty)
 *   funct=1: query status   — returns 1 in rd if FIFO has data, 0 if empty
 */

#ifndef ROCC_ISB_H
#define ROCC_ISB_H

#include <stdint.h>

/* ── RoCC custom instruction encoding helpers ── */

#define STR1(x) #x
#define STR(x) STR1(x)
#define EXTRACT(a, size, offset) (((~(~0 << size) << offset) & a) >> offset)

#define CUSTOM_0 0b0001011
#define CUSTOM_1 0b0101011

#define CUSTOMX(X, xd, xs1, xs2, rd, rs1, rs2, funct) \
  (CUSTOM_ ## X)                              |       \
  ((rd)                 << (7))               |       \
  ((xs2)                << (7+5))             |       \
  ((xs1)                << (7+5+1))           |       \
  ((xd)                 << (7+5+2))           |       \
  ((rs1)                << (7+5+3))           |       \
  ((rs2)                << (7+5+3+5))         |       \
  (EXTRACT(funct, 7, 0) << (7+5+3+5+5))

/* Write with rs1 as source register (no rd output) */
#define ROCC_INSTRUCTION_S(X, rs1, funct) {                                   \
    register uint64_t rs1_ asm ("x11") = (uint64_t)(rs1);                     \
    asm volatile (                                                            \
        ".word " STR(CUSTOMX(X, 0, 1, 0, 0, 11, 0, funct)) "\n\t"            \
        :: [_rs1] "r" (rs1_));                                                \
  }

/* Read with rd as destination register (no rs1/rs2 input) */
#define ROCC_INSTRUCTION_D(X, rd, funct) {                                    \
    register uint64_t rd_ asm ("x10");                                        \
    asm volatile (                                                            \
        ".word " STR(CUSTOMX(X, 1, 0, 0, 10, 0, 0, funct)) "\n\t"            \
        : "=r" (rd_));                                                        \
    rd = rd_;                                                                 \
  }

/* ── ISB Writer API (for Core 0, uses custom0) ── */

/**
 * Blocking write: push a 64-bit value into the ISB FIFO.
 * Stalls the core if the FIFO is full until space is available.
 */
static inline void isb_write(uint64_t data)
{
    ROCC_INSTRUCTION_S(0, data, 0);
}

/**
 * Non-blocking status query: check if the ISB FIFO has space.
 * Returns 1 if the FIFO can accept a write, 0 if full.
 */
static inline uint64_t isb_write_query(void)
{
    uint64_t result;
    ROCC_INSTRUCTION_D(0, result, 1);
    return result;
}

/* ── ISB Reader API (for Core 1, uses custom1) ── */

/**
 * Blocking read: pop a 64-bit value from the ISB FIFO.
 * Stalls the core if the FIFO is empty until data is available.
 */
static inline uint64_t isb_read(void)
{
    uint64_t data;
    ROCC_INSTRUCTION_D(1, data, 0);
    return data;
}

/**
 * Non-blocking status query: check if the ISB FIFO has data.
 * Returns 1 if the FIFO has data to read, 0 if empty.
 */
static inline uint64_t isb_read_query(void)
{
    uint64_t result;
    ROCC_INSTRUCTION_D(1, result, 1);
    return result;
}

#endif /* ROCC_ISB_H */
