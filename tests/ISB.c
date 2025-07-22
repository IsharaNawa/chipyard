#include "mmio.h"

// defining the addresses of the PlusOne module registers
// check the status of the module
// set the x value for the register
// get the Plus One value from the register
#define ISB_STATUS 0x4000   
// 1 : deq data is valid, 
// 2 : buffer is ready to accept data(buffer has space) 
// 3 : buffer is ready to accept data and also has valid bits on the line
// 0 : invalid value , i.e. this number should never be outputted

#define SET_ISB_INPUT_DATA 0x4004   // set the data to add to fifo
#define SET_ISB_INPUT_VALID 0x4008  // make that data valid


#define ISB_OUTPUT_DATA 0x400C      // get the data from the fifo
#define SET_ISB_OUTPUT_READY 0x04010    // make output ready to accept data

#define STATUS_WRONG_IMPL 0
#define STATUS_VALID_DATA_FIFO_FULL 1
#define STATUS_FIFO_EMPTY 2
#define STATUS_VALID_DATA_FIFO_NOT_FULL 3

#define ISB_DEPTH 10

//--------------------------------------------------------------------------------
//                               Start : UTILITY FUNCTIONS
//--------------------------------------------------------------------------------

void print_status(){
    
    uint8_t status = reg_read8(ISB_STATUS);

    printf("Status = ");
    if(status==STATUS_WRONG_IMPL){
        printf("%d : Wrong implementation",status);
    }else if(status==STATUS_VALID_DATA_FIFO_FULL){
        printf("%d : Deq data is valid, and the buffer is full",status);
    }else if(status==STATUS_FIFO_EMPTY){
        printf("%d : Buffer is empty and ready to accept data",status);
    }else if(status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        printf("%d : Buffer is ready to accept data(has space) and also has valid data at output",status);
    }
    printf("\n");

}

void print_deque_data(){

    uint32_t deque_data;

    // first check if there are valid values in the data wire
    uint8_t status = reg_read8(ISB_STATUS);

    // if the output has valid data
    if(status==STATUS_VALID_DATA_FIFO_FULL || status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        deque_data = reg_read32(ISB_OUTPUT_DATA);
        // print the data
        printf("Data at output : %d\n",deque_data);
    }else{
        printf("Data is not valid!\n");
    }

}

//--------------------------------------------------------------------------------
//                               End : UTILITY FUNCTIONS
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------
//                               Start : TESTING FUNCTIONS
//--------------------------------------------------------------------------------
void adding_only_1_value(){

    // value to be added to the FIFO
    uint32_t enq_data=10;

    // print the initial status
    print_status();

    // write deque is not ready
    reg_write32(SET_ISB_OUTPUT_READY,0);

    // write the data to the register for adding into fifo
    reg_write32(SET_ISB_INPUT_DATA, enq_data);
    // now make the data valid
    reg_write32(SET_ISB_INPUT_VALID, 1);
    asm volatile("fence" ::: "memory");

    // Wait for hardware to consume
    while (!(reg_read8(ISB_STATUS) & 0x2));  // wait for ready
    // OR wait for ready to toggle if more accurate

    reg_write32(SET_ISB_INPUT_VALID, 0);
    asm volatile("fence" ::: "memory");

    // now check the status
    print_status();

}

void adding_only_1_value_and_make_data_invalid(){

    // value to be added to the FIFO
    uint32_t enq_data=10;

    // print the initial status
    print_status();

    // write the data to the register for adding into fifo
    reg_write32(SET_ISB_INPUT_DATA, enq_data);

    // now make the data valid
    reg_write32(SET_ISB_INPUT_VALID,1);

    // check what happens when we make the valid bit 0
    reg_write32(SET_ISB_INPUT_VALID,0);

    // now check the status
    print_status();

    // output
    // Status = 2 : Buffer is empty and ready to accept data
    // Status = 2 : Buffer is empty and ready to accept data

}

void adding_only_1_value_and_check_value(){

    // value to be added to the FIFO
    uint32_t enq_data=10;

    // print the initial status
    print_status();

    // write the data to the register for adding into fifo
    reg_write32(SET_ISB_INPUT_DATA, enq_data);
    // now make the data valid
    reg_write32(SET_ISB_INPUT_VALID,1);

    // now check the status
    print_status();

    // print the deque data
    print_deque_data();

}

void adding_only_multiple_values_and_check_status_and_values(){

    // print the initial status
    print_status();

    // print make dequeue not ready
    reg_write32(SET_ISB_OUTPUT_READY,0);

    // fill the fifo
    for(uint32_t counter=0;counter<ISB_DEPTH+1;counter++){
        // write the data to the register for adding into fifo
        reg_write32(SET_ISB_INPUT_DATA, counter);
        
        // now make the data valid
        reg_write32(SET_ISB_INPUT_VALID,1);

        // NOTE now we dont sit here and wait for FIFO to be updated. becuase we dont have a busy signal
        // Point for improvement
    }

    // now check the status
    print_status();

    // print the deque data
    print_deque_data();

}

//--------------------------------------------------------------------------------
//                               End : TESTING FUNCTIONS
//--------------------------------------------------------------------------------

int main(void)
{
    adding_only_1_value();

    return 0;
}