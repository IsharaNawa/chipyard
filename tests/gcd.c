#include "mmio.h"

// defining the addresses of the GCD module registers
// check the status of the module
// set the x value for the register
// set the y value for the register
// get the gcd value from the register
#define GCD_STATUS 0x4000
#define GCD_X 0x4004
#define GCD_Y 0x4008
#define GCD_GCD 0x400C

// this function computes the gcd in software
unsigned int gcd_ref(unsigned int x, unsigned int y) {
  while (y != 0) {
    if (x > y)
      x = x - y;
    else
      y = y - x;
  }
  return x;
}

// DOC include start: GCD test
int main(void)
{
  // create variables for storing the result, software gcd, x and y
  uint32_t result, ref, x = 20, y = 15;

  // wait for peripheral to be ready
  while ((reg_read8(GCD_STATUS) & 0x2) == 0) ;

  // write the x value to GCD_X register(using that address) first
  reg_write32(GCD_X, x);

  // then write the y value to the GCD_Y register
  reg_write32(GCD_Y, y);

  // wait for peripheral to complete
  // this is done using checking the staus via polling
  while ((reg_read8(GCD_STATUS) & 0x1) == 0) ;

  // get the value in the GCD_GCD register and save in the result
  result = reg_read32(GCD_GCD);

  // get the software implementaion result
  ref = gcd_ref(x, y);

  // print this if the values are different from sw and hw
  if (result != ref) {
    printf("Hardware result %d does not match reference value %d\n", result, ref);
    return 1;
  }

  // print this if values are not different and successful completion
  printf("Hardware result %d is correct for GCD\n", result);
  return 0;
}
// DOC include end: GCD test
