// Use below on bare metal : comment below and add includes for platform, uart and kprintln implementations
#include "uart.h"
#include "kprintf.h"
#include "platform.h"
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

  // Use below on host machine , spike
// #include <stdio.h>
// #define kprintf(...) printf(__VA_ARGS__)
// #define kputc(c) putchar(c)
// #define kprintln(fmt, ...) do { printf(fmt, ##__VA_ARGS__); putchar('\n'); } while (0)
// #define uart_init() ((void)0)

int main() {
  uart_init();
  kprintln("My Hello From Ishara its me for csr test");

  /* Read the hardware cycle counter CSR (mcycle) and print it */
  // unsigned long cycles = read_csr(mcycle);
  /* kprintf supports %ld/%lx (with 'l' modifier) and %x; it does NOT support %u/%lu.
    Also kprintln doesn't forward variadic args correctly, so use kprintf + explicit newline. */
  long start_cycles = read_csr(mcycle);
  kprintf("mcycle new = %lx", start_cycles);
  kputc('\r'); kputc('\n');

  int a = 10 + 20 * 2;
  kprintln("Computed value: %d", a);

  kprintln("My Hello From Ishara its me");

  /* print again for verification in hex */
  long end_cycles = read_csr(mcycle);
  kprintf("mcycle = %lx", end_cycles);
  kputc('\r'); kputc('\n');

  kprintln("Number of cycles = %ld", end_cycles - start_cycles);

  /* Additional minimal float tests (kept small and non-invasive):
   * - print individual operands b and c bits
   * - print sum/product/quotient bit patterns
   * - print an int conversion of 'b' (truncation)
   * - print individual operands b and c bits
   * - print sum/product/quotient bit patterns
   * - print an int conversion of 'b' (truncation)
   */

  float b = 3.5f;
  float c = 2.5f;
  float total = b + c;

  /* print raw IEEE-754 bits of b as hex */
  union { float f; uint32_t u; } fu;
  fu.f = total;

  /* kprintf doesn't support width/flags like %08x — use plain %x which prints
   * the full 8 hex digits for a 32-bit unsigned int on this platform. */
  kprintf("b bits = 0x%x", fu.u);
  kputc('\r'); kputc('\n');

  /* Additional minimal float tests (kept small and non-invasive):
   * - print individual operands b and c bits
   * - print sum/product/quotient bit patterns
   * - print an int conversion of 'b' (truncation)
   */
  union { float f; uint32_t u; } conv;
  conv.f = b; kprintf("b raw = 0x%x", conv.u); kputc('\r'); kputc('\n');
  conv.f = c; kprintf("c raw = 0x%x", conv.u); kputc('\r'); kputc('\n');

  float s = b + c;
  conv.f = s; kprintf("b+c = 0x%x", conv.u); kputc('\r'); kputc('\n');
  float p = b * c;
  conv.f = p; kprintf("b*c = 0x%x", conv.u); kputc('\r'); kputc('\n');
  float q = (c == 0.0f) ? 0.0f : b / c;
  conv.f = q; kprintf("b/c = 0x%x", conv.u); kputc('\r'); kputc('\n');

  int bi = (int)b; kprintf("(int)b = %d", bi); kputc('\r'); kputc('\n');

  /* keep running so the message remains visible */
  while (1);
    return 0;
}