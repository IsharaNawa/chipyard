#include "mmio.h"

/*************************************************************************
    Start : Defining the addresses of the SequentialISB module registers
*************************************************************************/
#define GET_ISB_STATUS 0x4000   // check the status of the module

#define SET_ISB_INPUT_DATA 0x4004   // set the data to add to fifo
#define SET_ISB_INPUT_SEQUENTIAL_NUMBER 0x4008  // set the sequential number of the data for enquing

#define GET_ISB_OUTPUT_DATA 0x400C  // get the data from the fifo
#define SET_ISB_OUTPUT_SEQUENTIAL_NUMBER 0x4010 // set the sequentail number of the data for dequing
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
    Start : Set sequential value for enquing
***********************************************************************/
void set_enque_sequential_number(){

    // increment the sequential number
    enque_sequential_number += 1;

    // write the sequentail number back to the register
    reg_write32(SET_ISB_INPUT_SEQUENTIAL_NUMBER, enque_sequential_number);

}
/***********************************************************************
    End : Set sequential value for enquing
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
/***********************************************************************
    End : Enqueue data
***********************************************************************/

/***********************************************************************
    Start : Get data from the enque port
***********************************************************************/
uint32_t get_deque_data(){
    uint32_t data = reg_read32(GET_ISB_OUTPUT_DATA);
    return data;
}
/***********************************************************************
    End : Get data from the enque port
***********************************************************************/

/***********************************************************************
    Start : Set sequential value for dequing
***********************************************************************/
void set_deque_sequential_number(){

    // increment the sequential number
    deque_sequential_number += 1;
    
    // set updated sequential number
    reg_write32(SET_ISB_OUTPUT_SEQUENTIAL_NUMBER,deque_sequential_number);

}
/***********************************************************************
    End : Set sequential value for dequing
***********************************************************************/

/***********************************************************************
    Start : Get deque data
***********************************************************************/
int deque_data(){

    uint8_t status = get_status_value();

    // check whether the buffer has valid data
    if(status==STATUS_VALID_DATA_FIFO_FULL || status==STATUS_VALID_DATA_FIFO_NOT_FULL){

        // get the data
        uint32_t data = get_deque_data();

        // update sequential number
        set_deque_sequential_number();

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
    End : Get deque data
***********************************************************************/

/***********************************************************************
    Start : Make the buffer empty
***********************************************************************/
void make_fifo_empty_without_saving_deque_data(){
    
    // check if the status is not empty
    while(get_status_value()==STATUS_VALID_DATA_FIFO_FULL || get_status_value()==STATUS_VALID_DATA_FIFO_NOT_FULL){
        
        // increment the sequential number for dequeing
        set_deque_sequential_number();
    }

    // check the status after this and if the status is wrong implemnetation
    if(get_status_value()==STATUS_WRONG_IMPL){  

        // print the error
        printf("Error! Wrong Implementation!\n");
    }

    // check if we have the empty status now
    if(get_status_value()==STATUS_FIFO_EMPTY){

        // if so, print the status
        printf("Buffer is successfully emptied\n");
    }
    
}
/***********************************************************************
    End : Make the buffer empty
***********************************************************************/

/***********************************************************************
    Start : Make the buffer full
***********************************************************************/
void make_fifo_full_with_dummy_data(){

    uint32_t counter = 0;

    // check if the status is not full
    while(get_status_value()==STATUS_FIFO_EMPTY||get_status_value()==STATUS_VALID_DATA_FIFO_NOT_FULL){

        // set data for enqueing
        set_enque_data(counter);

        // set data for dequeing
        set_enque_sequential_number();

        // increment the counter
        counter += 1;
    }

    // check the status after this and if the status is wrong implemnetation
    if(get_status_value()==STATUS_WRONG_IMPL){  

        // print the error
        printf("Error! Wrong Implementation!\n");
    }

    // check if we have the full status now
    if(get_status_value()==STATUS_VALID_DATA_FIFO_FULL){

        // if so, print the status
        printf("Buffer is successfully filled until full\n");
    }

}
/***********************************************************************
    End : Make the buffer full
***********************************************************************/


/***********************************************************************
    Start : Set initial setup before testing
***********************************************************************/
void set_default(){

    // empty the buffer
    make_fifo_empty_without_saving_deque_data();

}
/***********************************************************************
    End : Set initial setup before testing
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

    // setting the fifo to default
    set_default();

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

    // setting the fifo to default
    set_default();

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

    // setting the fifo to default
    set_default();

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

    // setting the fifo to default
    set_default();

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

void check_deque_value_test(){
    /*
        Add a value and deque it and compare whether it is the same
    */

    // setting the fifo to default
    set_default();

    printf("Running : check_deque_value_test\n");

    uint32_t enque_data_ = 10;

    // first add data
    if(enque_data(enque_data_)){
        // print the this message when enquing data
        printf("Data was enqued!\n");
    }else{
        // print the error message when enquing data and error occurs
        printf("Data was not enqued!\n");
    }

    // get the deque data
    int deque_data_ = deque_data();

    // print the message if the correct data is not retrieved
    if(deque_data_==-1){
        printf("An error occured while retrieving data\n");
    }

    // check for test passing
    if(deque_data_==enque_data_){
        printf("Test Passed!");
    }else{
        printf("Test Failed! Wrong output value.");
    }
    printf("\n");

}

void fill_buffer_and_empty_buffer_test(){
    /*
        Fill buffer and deque them and compare whether they are the same
    */

    // setting the fifo to default
    set_default();

    printf("Running : fill_buffer_and_empty_buffer_test\n");

    // first get an array
    int data[ISB_DEPTH];

    // fill the array
    for(int i=0;i<ISB_DEPTH;i++){
        data[i] = i;
    }

    // first fill the fifo with array values
    for(int i=0;i<ISB_DEPTH;i++){
        enque_data(data[i]);
    }

    // track how many items have added
    int counter = 0;

    // now deque and check whether the values are the same
    for(int i=0;i<ISB_DEPTH+1;i++){
        int data_ = deque_data();

        // compare the values
        if(data_==data[i]){
            counter += 1;
            printf("%d = %d \n",data_,data[i]);
        }else if(data_==-1){

            if(get_status_value()==STATUS_FIFO_EMPTY && i==ISB_DEPTH){
                printf("Dequed until buffer is empty\n");
                printf("Test Passed!");
            }else{
                printf("Test Failed! An error occured\n");
            }
            break;
        }else{
            printf("Test Failed! Unexpected value received!");
            break;
        }
    }
    printf("\n");
}

void run_basic_test_suit(){

    check_empty_status_test();

    check_for_enqueing_function_test();

    add_one_value_and_check_status_test();

    fill_until_full_fifo_and_check_status_test();

    check_deque_value_test();

    fill_buffer_and_empty_buffer_test();

}

//--------------------------------------------------------------------------------
//                               End : TESTING FUNCTIONS
//--------------------------------------------------------------------------------


int main(void)
{

    run_basic_test_suit();

    return 0;
}