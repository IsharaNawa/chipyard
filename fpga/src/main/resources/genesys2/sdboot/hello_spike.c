// Spike bare-metal version using tohost for output
#include <stdint.h>

/* CSR reading macro */
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

// Spike's tohost/fromhost for console output
volatile uint64_t tohost __attribute__((section(".tohost")));
volatile uint64_t fromhost __attribute__((section(".fromhost")));

#define SYS_write 64

static void tohost_exit(int code) {
  tohost = (code << 1) | 1;
  while (1);
}

static void write_char(char c) {
  // Write directly to tohost - Spike's HTIF console
  while (tohost) {
    fromhost = 0;
  }
  tohost = 0x0101000000000000ULL | (unsigned char)c;
  while (tohost) {
    fromhost = 0;
  }
}

static void print_str(const char *s) {
  while (*s) write_char(*s++);
}

static void print_hex(unsigned long val) {
  char buf[17];
  buf[16] = 0;
  for (int i = 15; i >= 0; i--) {
    int digit = val & 0xf;
    buf[i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
    val >>= 4;
  }
  print_str("0x");
  print_str(buf);
}

int main() {
  print_str("My Hello From Ishara - CSR & FPU test\r\n");

  /* Enable FPU by setting FS bits in mstatus */
  asm volatile ("li t0, 0x6000"); // FS = 0b11 (dirty)
  asm volatile ("csrs mstatus, t0"); // add these two lines to enable FPU

  
  print_str("FPU enabled in mstatus\r\n");

  /* Read misa to detect F/D extensions */
  unsigned long misa = read_csr(misa);
  print_str("misa = ");
  print_hex(misa);
  print_str("\r\n");
  
  /* extension bit positions: bit = 1 << (letter - 'A') */
  unsigned long has_D = (misa >> ('D' - 'A')) & 1UL;
  unsigned long has_F = (misa >> ('F' - 'A')) & 1UL;
  print_str("misa has F = ");
  write_char('0' + has_F);
  print_str(", D = ");
  write_char('0' + has_D);
  print_str("\r\n");

  if (!has_F && !has_D) {
    print_str("No floating point extensions present (no F/D) - exiting\r\n");
    tohost_exit(1);
  }

  print_str("Starting FPU operations...\r\n");

  /* Perform a few double-precision FP ops and print their bit patterns */
  volatile double d1 = 3.141592653589793;
  print_str("d1 assigned\r\n");
  
  volatile double d2 = 2.718281828459045;
  print_str("d2 assigned\r\n");
  
  double d_sum = d1 + d2;
  print_str("d_sum calculated\r\n");
  
  double d_mul = d1 * d2;
  print_str("d_mul calculated\r\n");
  
  double d_div = d1 / d2;
  print_str("d_div calculated\r\n");

  union { double d; uint64_t u; } du;
  print_str("Starting to print results...\r\n");

  du.d = d1;
  print_str("d1 bits = ");
  print_hex(du.u);
  print_str("\r\n");

  du.d = d2;
  print_str("d2 bits = ");
  print_hex(du.u);
  print_str("\r\n");

  du.d = d_sum;
  print_str("d1+d2 bits = ");
  print_hex(du.u);
  print_str("\r\n");

  du.d = d_mul;
  print_str("d1*d2 bits = ");
  print_hex(du.u);
  print_str("\r\n");

  du.d = d_div;
  print_str("d1/d2 bits = ");
  print_hex(du.u);
  print_str("\r\n");

  print_str("FPU test complete!\r\n");
  tohost_exit(0);
  return 0;
}
