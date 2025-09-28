// Simple Hello World bootram program
#include "uart.h"
#include "kprintf.h"
#include "platform.h"

// CSR reading macros
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

int main() {
    uart_init();
    kprintln("Hello this is bootrom!");
    kprintln("This is a simple test application which replaces SD boot");

    // Print hartid
    kprintf("Hart ID: %ld\n", read_csr(mhartid));
    
    // Loop forever so we can see the output
    int index = 0;
    while(1) {
      kprintf("Value = %d\n", index++);
      if(index % 10000 == 0) {
        index = 10000;
      }
    }
    
    return 0;
}