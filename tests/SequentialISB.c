#include "mmio.h"

// defining the addresses of the SequentialISB module registers
// check the status of the module
#define ISB_STATUS 0x4000 
// 1 : deq data is valid, 
// 2 : buffer is ready to accept data(buffer has space)
// 3 : buffer is ready to accept data and also has valid bits on the line
// 0 : invalid value , i.e. this number should never be outputted

#define SET_ISB_INPUT_DATA 0x4004   // set the data to add to fifo
#define SET_ISB_INPUT_SEQUENTIAL_NUMBER 0x4008 // set the sequential number of the data
#define SET_ISB_INPUT_VALID 0x400C  // make that data valid


#define ISB_OUTPUT_DATA 0x4010      // get the data from the fifo
#define SET_ISB_OUTPUT_READY 0x04014    // make output ready to accept data

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
    uint32_t sequential_number=0;

    // print the initial status
    print_status();

    // write deque is not ready(blocking output)
    reg_write32(SET_ISB_OUTPUT_READY,0);

    // check whether the buffer can accept data
    if(reg_read8(ISB_STATUS)==STATUS_FIFO_EMPTY || reg_read8(ISB_STATUS)==STATUS_VALID_DATA_FIFO_NOT_FULL){
        
        // write the data to the register for adding into fifo
        reg_write32(SET_ISB_INPUT_DATA, enq_data);

        // write the sequential number
        sequential_number += 1;
        reg_write32(SET_ISB_INPUT_SEQUENTIAL_NUMBER, sequential_number);

        // now make the data valid
        reg_write32(SET_ISB_INPUT_VALID, 1);

    }

    // now check the status
    print_status();

    // check the data at the deque side
    print_deque_data();

    int counter = 0;

    for(int i=0;i<ISB_DEPTH;i++){
        // check whether the buffer can accept data
        if(reg_read8(ISB_STATUS)==STATUS_FIFO_EMPTY || reg_read8(ISB_STATUS)==STATUS_VALID_DATA_FIFO_NOT_FULL){
            
            // write the data to the register for adding into fifo
            reg_write32(SET_ISB_INPUT_DATA, i);

            // write the sequential number
            sequential_number += 1;
            reg_write32(SET_ISB_INPUT_SEQUENTIAL_NUMBER, sequential_number);

            // now make the data valid
            reg_write32(SET_ISB_INPUT_VALID, 1);

            // increment the counter
            counter += 1;

        }
    }

    printf("Written %d number of  data\n",counter + 1); // +1 because we have already enqueed a data(10)

}

//--------------------------------------------------------------------------------
//                               End : TESTING FUNCTIONS
//--------------------------------------------------------------------------------

int main(void)
{
    adding_only_1_value();

    // print_status();

    return 0;
}