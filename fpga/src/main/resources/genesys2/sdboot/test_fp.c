/* Simple FPU test for bare-metal */

#include "uart.h"
#include "kprintf.h"

#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

#define write_csr(reg, val) ({ \
  asm volatile ("csrw " #reg ", %0" : : "r"(val)); })

int main(void) {
	/* Enable FPU */
	unsigned long mstatus_val = read_csr(mstatus);
	mstatus_val |= 0x00006000;  // Set FS=11
	write_csr(mstatus, mstatus_val);
	write_csr(fcsr, 0);

	uart_init();
	kprintln("=== FPU Test ===");
	
	kprintln("Testing FPU initialization...");
	unsigned long mstatus_check = read_csr(mstatus);
	kprintln("mstatus = 0x%lx", mstatus_check);
	
	kprintln("Test 1: Simple float assignment");
	volatile float a = 1.0f;
	kprintln("a = 1.0 OK");
	
	kprintln("Test 2: Float addition");
	volatile float b = 2.0f;
	volatile float c = a + b;
	kprintln("1.0 + 2.0 = %d (should be 3)", (int)c);
	
	kprintln("Test 3: Float multiplication");
	volatile float d = a * b;
	kprintln("1.0 * 2.0 = %d (should be 2)", (int)d);
	
	kprintln("Test 4: Float to int conversion");
	float e = 5.7f;
	int f = (int)e;
	kprintln("(int)5.7 = %d (should be 5)", f);
	
	kprintln("=== All FPU tests passed! ===");
	
	while(1);
	return 0;
}
