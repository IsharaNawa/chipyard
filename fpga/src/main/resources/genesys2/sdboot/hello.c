#include "uart.h"
#include "kprintf.h"
#include "platform.h"

// CSR reading macros
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

int main() {
    uart_init();
  kprintln("My Hello From Ishara its me for csr test");

  /* Read the hardware cycle counter CSR (mcycle) and print it */
  unsigned long cycles = read_csr(mcycle);
  /* kprintf supports %ld/%lx (with 'l' modifier) and %x; it does NOT support %u/%lu.
    Also kprintln doesn't forward variadic args correctly, so use kprintf + explicit newline. */
  kprintf("mcycle new = %lx", cycles);
  kputc('\r'); kputc('\n');

  int a = 10 + 20 * 2;
  kprintln("Computed value: %d", a);

  kprintln("My Hello From Ishara its me");

  /* print again for verification in hex */
  kprintf("mcycle = %lx", read_csr(mcycle));
  kputc('\r'); kputc('\n');

  /* keep running so the message remains visible */
  while (1);
    return 0;
}