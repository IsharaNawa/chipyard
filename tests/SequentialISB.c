#include "mmio.h"

/**********************************************************************
    Start : Defining the addresses of the SequentialISB module registers
**********************************************************************/
#define GET_ISB_STATUS 0x4000   // check the status of the module 

#define SET_ISB_INPUT_DATA 0x4004   // set the data to add to fifo
#define SET_ISB_INPUT_SEQUENTIAL_NUMBER 0x4008 // set the sequential number of the data for enquing
#define SET_ISB_INPUT_VALID 0x400C  // make that data valid

#define GET_ISB_OUTPUT_DATA 0x4010      // get the data from the fifo
#define SET_ISB_OUTPUT_READY 0x04014    // make output ready to accept data
#define SET_ISB_OUTPUT_SEQUENTIAL_NUMBER 0x4018 // set the sequentail number of the data for dequing
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
    Start : Current Depth of the ISB
***********************************************************************/
#define ISB_DEPTH 10
/***********************************************************************
    End : Current Depth of the ISB
***********************************************************************/

/***********************************************************************
    Start : Global variables to set sequential number
***********************************************************************/
uint32_t enque_sequential_number = 0;
uint32_t deque_sequential_number = 0;
/***********************************************************************
    End : Global variables to set sequential number
***********************************************************************/

/***********************************************************************
    Start : Global variable to set debug mode
***********************************************************************/
uint8_t debug_mode = 1;
/***********************************************************************
    End : Global variable to set debug mode
***********************************************************************/

//--------------------------------------------------------------------------------
//                               Start : UTILITY FUNCTIONS
//--------------------------------------------------------------------------------

/***********************************************************************
    Start : Get the value of the status
***********************************************************************/
uint8_t get_status_value(){
    uint8_t status = reg_read8(GET_ISB_STATUS);
    return status;
}
/***********************************************************************
    End : Get the value of the status
***********************************************************************/

/***********************************************************************
    Start : Print the value of the status
***********************************************************************/
void print_status_value(){
    uint8_t status = reg_read8(GET_ISB_STATUS);
    printf("Status = %d \n",status);
}
/***********************************************************************
    End : Print the value of the status
***********************************************************************/

/***********************************************************************
    Start : Print the status
***********************************************************************/
void print_status(){
    
    uint8_t status = reg_read8(GET_ISB_STATUS);

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
/***********************************************************************
    End : Print the status
***********************************************************************/

/***********************************************************************
    Start : Set sequential value
***********************************************************************/
void set_enque_sequential_number(){

    // increment the sequential number
    enque_sequential_number += 1;

    // write the sequentail number back to the register
    reg_write32(SET_ISB_INPUT_SEQUENTIAL_NUMBER, enque_sequential_number);

}
/***********************************************************************
    End : Set sequential value
***********************************************************************/

/***********************************************************************
    Start : Set data for enqueueing
***********************************************************************/
void set_enque_data(uint32_t data){

    // write the data to the register for adding into fifo
    reg_write32(SET_ISB_INPUT_DATA, data);
}
/***********************************************************************
    End : Set data for enqueueing
***********************************************************************/

/***********************************************************************
    Start : Enqueue data
***********************************************************************/
uint8_t enque_data(uint32_t data){

    uint8_t status = get_status_value();

    // check whether the buffer can accept data
    if(status==STATUS_FIFO_EMPTY || status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        
        set_enque_data(data);

        set_enque_sequential_number();

        // now make the data valid
        reg_write32(SET_ISB_INPUT_VALID, 1);

        // print the message
        printf("Enqueued : %d\n", data);

        return 1;

    }else if(status==STATUS_VALID_DATA_FIFO_FULL){
        printf("Enqueing error! Buffer is full \n");

        return 0;
    }else if(status==STATUS_WRONG_IMPL){
        printf("Enqueing error! Wrong Implementation \n");

        return 0;
    }else{
        printf("Enqueing error! Unknown Error! \n");

        return 0;

    }
}

int get_deque_data(){

    uint8_t status = get_status_value();

    // check whether the buffer has valid data
    if(status==STATUS_VALID_DATA_FIFO_FULL || status==STATUS_VALID_DATA_FIFO_NOT_FULL){

        // get the data
        uint32_t data = reg_read32(GET_ISB_OUTPUT_DATA);

        // make the output ready
        reg_write32(SET_ISB_OUTPUT_READY,1);

        // set sequential number
        // increment the sequential number
        deque_sequential_number += 1;
        reg_write32(SET_ISB_OUTPUT_SEQUENTIAL_NUMBER,deque_sequential_number);

        // print the message
        printf("Dequeued : %d\n", data);

        return data;


    }else if(status==STATUS_FIFO_EMPTY){

        printf("Dequeing error! Buffer is Empty \n");

        return -1;

    }else if(status==STATUS_WRONG_IMPL){
        printf("Enqueing error! Wrong Implementation \n");

        return -1;
    }
    else{
        printf("Enqueing error! Unknown Error! \n");

        return -1;
    }


}
/***********************************************************************
    End : Enqueue data
***********************************************************************/

//--------------------------------------------------------------------------------
//                               End : UTILITY FUNCTIONS
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------
//                               Start : TESTING FUNCTIONS
//--------------------------------------------------------------------------------


void check_empty_status_test(){
    /*
        Initially there should not be any elements in the array
    */

    printf("Running : check_empty_status_test\n");

    uint8_t status = get_status_value();

    if(status==STATUS_FIFO_EMPTY){
        print_status();
        printf("Test Passed!");
    }else{
        printf("Test Failed! Initially the buffer should be empty.");
    }
    printf("\n");
    
}

void check_for_enqueing_function_test(){
    /*
        Check whether the enqueing function work without any errors
    */

    printf("Running : check_for_enqueing_function_test\n");

    uint8_t status = get_status_value();

    if(status==STATUS_FIFO_EMPTY || status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        if(enque_data(10)){
            print_status();
            printf("Test Passed!");
        }else{
            printf("Test Failed! There is an error!");
        }
    }else{
        printf("Test Aborted! Fifo does not have space!");
    }
    printf("\n");    
}

void add_one_value_and_check_status_test(){
    /*
        Adding a value and check if the status is changed accordingly
    */

    printf("Running : add_one_value_and_check_status_test\n");

    // first try to enque data
    enque_data(10);

    // now check the statusuint8_t status = get_status_value();
    uint8_t status = get_status_value();

    if(status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        print_status();
        printf("Test Passed!");
    }else{
        printf("Test Failed! Buffer should have valid data and not full.");
    }
    printf("\n");

}

void fill_until_full_fifo_and_check_status_test(){
    /*
        Adding values until the fifo is full and check status
    */

    printf("Running : fill_until_full_fifo_and_check_status_test\n");

    // track how many items have added
    int counter = 0;

    // fill the data values until it runs out
    for(int i=0;i<ISB_DEPTH;i++){
        uint8_t result = enque_data(i);
        if(result==1){
            counter += 1;
        }else{
            break;
        }
    }

    // print how many items have added by the for loop
    printf("%d items newly added\n",counter);
    

    // now check the statusuint8_t status = get_status_value();
    uint8_t status = get_status_value();

    if(status==STATUS_VALID_DATA_FIFO_FULL){
        print_status();
        printf("Test Passed!");
    }else{
        printf("Test Failed! Buffer should be full.");
    }
    printf("\n");
}

void peek_deque_value_test(){

    printf("Running : peek_deque_value_test\n");

    int data = get_deque_data();


    if(data==10){
        printf("Test Passed!");
    }else{
        printf("Test Failed! Wrong output value.");
    }
    printf("\n");

}

void get_all_data_test(){

    printf("Running : get_all_data_test\n");

    // track how many items have added
    int counter = 0;

    // get the data values until it runs out
    for(int i=0;i<ISB_DEPTH;i++){
        int data = get_deque_data();
        if(data==i){
            counter += 1;
        }else if(data==-1){
            printf("An error occured\n");
            break;
        }else{
            printf("Unexpected Error! Unexpected value received!");
            break;
        }
    }

    // print how many items have retrived by the for loop
    printf("%d items retrived\n",counter);
    
    // now check the statusuint8_t status = get_status_value();
    uint8_t status = get_status_value();

    if(status==STATUS_FIFO_EMPTY){
        print_status();
        printf("Test Passed!");
    }else{
        printf("Test Failed! Buffer should be full.");
    }
    printf("\n");

}
//--------------------------------------------------------------------------------
//                               End : TESTING FUNCTIONS
//--------------------------------------------------------------------------------

int main(void)
{

    

    // TODO : set default ports(including data emptying)


    check_empty_status_test();

    check_for_enqueing_function_test();

    add_one_value_and_check_status_test();

    fill_until_full_fifo_and_check_status_test();

    peek_deque_value_test();

    peek_deque_value_test();

    get_all_data_test();

    return 0;
}