#include "mmio.h"

// defining the addresses of the PlusOne module registers
// check the status of the module
// set the x value for the register
// get the Plus One value from the register
#define PLUS_ONE_STATUS 0x4000
#define PLUS_ONE_X 0x4004
#define PLUS_ONE_INPUT_VALID 0x4008 
#define PLUS_ONE_PLUS_ONE 0x400C

// this function computes the plus in software
unsigned int plus_one_ref(unsigned int x) {
  return x + 1;
}

void test_case(uint32_t x){

  // wait for peripheral to be ready
  while ((reg_read8(PLUS_ONE_STATUS) & 0x2) == 0) ;

  // write the x value to PLUS_ONE_X register(using that address) first
  reg_write32(PLUS_ONE_X, x);

  // then write the input valid to PLUS_ONE_INPUT_VALID register
  reg_write32(PLUS_ONE_INPUT_VALID, 1);

  // get the value in the GCD_GCD register and save in the result
  uint32_t result = reg_read32(PLUS_ONE_PLUS_ONE);

  // then write the input valid false to PLUS_ONE_INPUT_VALID register
  reg_write32(PLUS_ONE_INPUT_VALID, 0);

  // get the software implementaion result
  uint32_t ref = plus_one_ref(x);

  // print this if the values are different from sw and hw
  if (result != ref) {
    printf("Hardware result %d does not match reference value %d\n", result, ref);
    return 1;
  }

  // print this if values are not different and successful completion
  printf("Hardware result %d is correct for PlusOne\n", result);

}

// DOC include start: Plus One test
int main(void)
{
    test_case(37);

    test_case(73);

    test_case(10000);

    return 0;
}
// DOC include end: GCD test
