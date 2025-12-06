#include "mmio.h"
#include <stdio.h>

/*************************************************************************
    Start : Defining the addresses of the SequentialISB module registers
*************************************************************************/
#define GET_ISB_STATUS 0x4000   // check the status of the module
#define SET_ISB_INPUT_DATA 0x4004   // set the data to add to fifo
#define GET_ISB_OUTPUT_DATA 0x4008  // get the data from the fifo
/**********************************************************************
    End : Defining the addresses of the SequentialISB module registers
**********************************************************************/

/***********************************************************************
    Start : Status Reading
***********************************************************************/
#define STATUS_WRONG_IMPL 0
#define STATUS_VALID_DATA_FIFO_FULL 1
#define STATUS_FIFO_EMPTY 2
#define STATUS_VALID_DATA_FIFO_NOT_FULL 3
/***********************************************************************
    End : Status Reading
***********************************************************************/

/***********************************************************************
    Start : Print the value of the status
***********************************************************************/
void print_status_value(){
    uint32_t fifo_status = reg_read32(GET_ISB_STATUS) & 0b11;
    printf("Status = %d \n",fifo_status);
}
/***********************************************************************
    End : Print the value of the status
***********************************************************************/

int main(void)
{
    // Disable output buffering for embedded Linux
    setbuf(stdout, NULL);
    
    printf("Running v4 tests...\n");
    fflush(stdout);

    uint8_t status = reg_read32(GET_ISB_STATUS) & 0b11;

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
    fflush(stdout);

    reg_write32(SET_ISB_INPUT_DATA, 10);

    status = reg_read32(GET_ISB_STATUS) & 0b11;

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
    fflush(stdout);

    uint32_t data = reg_read32(GET_ISB_OUTPUT_DATA);

    printf("Dequeued : %d\n", data);
    fflush(stdout);



    return 0;
}