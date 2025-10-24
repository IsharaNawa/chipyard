// Simple Hello World bootram program
#include "uart.h"
#include "kprintf.h"
#include "platform.h"
#include "mmio.h"

// CSR reading macros
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

/*************************************************************************
    Start : Defining the addresses of the SequentialISB module registers
*************************************************************************/
#define GET_ISB_STATUS 0x4000   // check the status of the module
#define SET_ISB_INPUT_DATA 0x4004   // set the data to add to fifo
#define GET_ISB_OUTPUT_DATA 0x4008  // get the data from the fifo
/***********************************************************************
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
    Start : Sequential Number Toggling
***********************************************************************/
#define SEQUENTIAL_NUMBER_0 0
#define SEQUENTIAL_NUMBER_1 1
/***********************************************************************
    End : Sequential Number Toggling
***********************************************************************/

/***********************************************************************
    Start : Enque Function Return Codes
***********************************************************************/
#define SUCCESSFUL_ENQUE_OPERATION 0
#define ENQUE_FAILED_DUE_TO_FIFO_FULL 1
#define ENQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION 2
#define ENQUE_FAILED_DUE_TO_COUNTER_NOT_INCREMENTING 3
#define ENQUE_FAILED_DUE_TO_UNKNOWN_ERROR 4
/***********************************************************************
    End : Enque Function Return Codes
***********************************************************************/

/***********************************************************************
    Start : Deque Function Return Codes
***********************************************************************/
#define DEQUE_FAILED_DUE_TO_FIFO_EMPTY -1
#define DEQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION -2
#define DEQUE_FAILED_DUE_TO_COUNTER_NOT_DECREMENTING -3
#define DEQUE_FAILED_DUE_TO_UNKNOWN_ERROR -4
/***********************************************************************
    End : Deque Function Return Codes
***********************************************************************/

/***********************************************************************
    Start : Current Depth of the ISB
***********************************************************************/
#define ISB_DEPTH 10
/***********************************************************************
    End : Current Depth of the ISB
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
    uint32_t fifo_status = reg_read32(GET_ISB_STATUS) & 0b11;
    return fifo_status;
}
/***********************************************************************
    End : Get the value of the status
***********************************************************************/

/***********************************************************************
    Start : Print the value of the status
***********************************************************************/
void print_status_value(){
    uint32_t fifo_status = reg_read32(GET_ISB_STATUS) & 0b11;
    kprintln("Status = %d", fifo_status);
}
/***********************************************************************
    End : Print the value of the status
***********************************************************************/

/***********************************************************************
    Start : Print the status
***********************************************************************/
void print_status(){
    
    uint8_t status = reg_read32(GET_ISB_STATUS) & 0b11;

    kprintf("Status = ");
    if(status==STATUS_WRONG_IMPL){
        kprintf("%d : Wrong implementation",status);
    }else if(status==STATUS_VALID_DATA_FIFO_FULL){
        kprintf("%d : Deq data is valid, and the buffer is full",status);
    }else if(status==STATUS_FIFO_EMPTY){
        kprintf("%d : Buffer is empty and ready to accept data",status);
    }else if(status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        kprintf("%d : Buffer is ready to accept data(has space) and also has valid data at output",status);
    }
    kprintln("");
}
/***********************************************************************
    End : Print the status
***********************************************************************/

/***********************************************************************
    Start : Get the count of the fifo
***********************************************************************/
uint8_t get_count(){
    uint32_t count = (reg_read32(GET_ISB_STATUS) & 0xFFFFFFFC) >> 2;
    return count;
}
/***********************************************************************
    End : Get the count of the fifo
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

    // get the status
    uint8_t status = get_status_value();

    // get the count
    uint32_t count = get_count();

    // check whether the buffer can accept data
    if(status==STATUS_FIFO_EMPTY || status==STATUS_VALID_DATA_FIFO_NOT_FULL){

        // set the data
        set_enque_data(data);

        // check if the current count is incremented than previous count
        if(get_count()==count+1){
            // print the message
            kprintln("Enqueued : %d", data);

            return SUCCESSFUL_ENQUE_OPERATION;
        }
        else{
            // print the message
            kprintf("Enqueing error! Count is not incremented after enquing.");

            return ENQUE_FAILED_DUE_TO_COUNTER_NOT_INCREMENTING;
        }
    }else if(status==STATUS_VALID_DATA_FIFO_FULL){
    kprintln("Enqueing error! Buffer is full");

        return ENQUE_FAILED_DUE_TO_FIFO_FULL;
    }else if(status==STATUS_WRONG_IMPL){
    kprintln("Enqueing error! Wrong Implementation");

        return ENQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION;
    }else{
    kprintln("Enqueing error! Unknown Error!");

        return ENQUE_FAILED_DUE_TO_UNKNOWN_ERROR;
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
    Start : Get deque data
***********************************************************************/
int deque_data(){

    // get the status
    uint8_t status = get_status_value();

    // get the count
    uint32_t count = get_count();

    // check whether the buffer has valid data
    if(status==STATUS_VALID_DATA_FIFO_FULL || status==STATUS_VALID_DATA_FIFO_NOT_FULL){

        // get the data
        uint32_t data = get_deque_data();

        // check if the current count is decremented than previous count
        if(get_count()==count-1){
            // print the message
            kprintln("Dequeued : %d", data);

            return data;
        }

        else{
            // print the error message
            kprintf("Enqueing error! Count is not decremented after dequeing.");

            return DEQUE_FAILED_DUE_TO_COUNTER_NOT_DECREMENTING;
        }

    }else if(status==STATUS_FIFO_EMPTY){

    kprintln("Dequeing error! Buffer is Empty");

        return DEQUE_FAILED_DUE_TO_FIFO_EMPTY;

    }else if(status==STATUS_WRONG_IMPL){
    kprintln("Dequeing error! Wrong Implementation");

        return DEQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION;
    }
    else{
    kprintln("Dequeing error! Unknown Error!");

        return DEQUE_FAILED_DUE_TO_UNKNOWN_ERROR;
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
        deque_data();
    }

    // check the status after this and if the status is wrong implemnetation
    if(get_status_value()==STATUS_WRONG_IMPL){  

        // print the error
    kprintln("Error while emptying! Wrong Implementation!");
    }

    // check if we have the empty status now
    if(get_status_value()==STATUS_FIFO_EMPTY){

        // if so, print the status
    kprintln("Buffer is successfully emptied");
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

        // increment the counter
        counter += 1;
    }

    // check if both the counters are the same
    if(get_count()!=ISB_DEPTH){
        
        // if not print the error message
    kprintln("Error! Counter is not correct after filling");
    }

    // check the status after this and if the status is wrong implemnetation
    if(get_status_value()==STATUS_WRONG_IMPL){  

        // print the error
    kprintln("Error! Wrong Implementation!");
    }

    // check if we have the full status now
    if(get_status_value()==STATUS_VALID_DATA_FIFO_FULL){

        // if so, print the status
    kprintln("Buffer is successfully filled until full");
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

    // setting the fifo to default(empty fifo)

    kprintln("Running : check_empty_status_test");

    uint8_t status = get_status_value();

    if(status==STATUS_FIFO_EMPTY){
        print_status();
        kprintf("Test Passed!");
    }else{
        kprintf("Test Failed! Initially the buffer should be empty.");
    }
    kprintln("");
    
}

void check_for_enqueing_function_for_successful_operation_test(){
    /*
        Check whether the enqueing function work without any errors
    */

    // setting the fifo to default(emptying)
    set_default();

    // print the message
    kprintln("Running : check_for_enqueing_function_for_successful_operation_test");

    // get the count
    uint32_t count = get_count();

    // get the enque function result
    uint8_t enque_operation_status = enque_data(10);

    // if enque is successful
    if(enque_operation_status==SUCCESSFUL_ENQUE_OPERATION){
        // check if the counter is incremented
        if(get_count()==count+1){
            print_status();
            kprintf("Test Passed!");
        }else{
            // print the test failed error message
            kprintf("Test Failed! Counter is not incremented after enqueing");
        }
    }else{
        kprintf("Test Failed! There is an error! Error Code : %d",enque_operation_status);
    }
    kprintln("");    
}

void add_one_value_and_check_status_test(){
    /*
        Adding only one value after emptying and check if the status is changed accordingly
    */

    // setting the fifo to default
    set_default();

    // print the message
    kprintln("Running : add_one_value_and_check_status_test");

    print_status();

    // first try to enque data
    enque_data(10);

    // now check the statusuint8_t status = get_status_value();
    uint8_t status = get_status_value();

    if(status==STATUS_VALID_DATA_FIFO_NOT_FULL){
        print_status();
        kprintf("Test Passed!");
    }else{
        kprintf("Test Failed! Buffer should have valid data and not full.");
    }
    kprintln("");
}

void fill_until_full_fifo_and_check_status_test(){
    /*
        Adding values until the fifo is full and check status
    */

    // setting the fifo to default
    set_default();

    kprintln("Running : fill_until_full_fifo_and_check_status_test");

    // track how many items have added
    int counter = 0;

    // fill the data values until it runs out
    for(int i=0;i<ISB_DEPTH;i++){
        uint8_t result = enque_data(i);
        if(result==SUCCESSFUL_ENQUE_OPERATION){
            counter += 1;
        }else{
            break;
        }
    }

    // print how many items have added by the for loop
    kprintln("%d items newly added",counter);
    

    // now check the status
    uint8_t status = get_status_value();

    // get the counter
    uint32_t count = get_count();

    if(status==STATUS_VALID_DATA_FIFO_FULL && count==ISB_DEPTH){
        print_status();
        kprintf("Test Passed!");
    }else{
        kprintf("Test Failed! Buffer should be full.");
    }
    kprintln("");
}

void check_deque_value_test(){
    /*
        Add a value and deque it and compare whether it is the same
    */

    // setting the fifo to default
    set_default();

    kprintln("Running : check_deque_value_test");

    uint32_t enque_data_ = 10;

    // first add data
    if(enque_data(enque_data_)==SUCCESSFUL_ENQUE_OPERATION){
    // print the this message when enquing data
    kprintln("Data was enqued!");

        // get the deque data
        int deque_data_ = deque_data();

        // print the message if the correct data is not retrieved
        if(deque_data_== DEQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION || deque_data_ == DEQUE_FAILED_DUE_TO_FIFO_EMPTY ||
            deque_data_==DEQUE_FAILED_DUE_TO_UNKNOWN_ERROR || deque_data_ == DEQUE_FAILED_DUE_TO_COUNTER_NOT_DECREMENTING
        ){

            kprintln("An error occured while retrieving data with Error code : %d",deque_data_);
        }

        // check for test passing
        if(deque_data_==enque_data_ && get_count()==0){
            kprintf("Test Passed!");
        }else{
            kprintf("Test Failed! Wrong output value and/or Wrong counter value.");
        }
    kprintln("");

    }else{
        // print the error message when enquing data and error occurs
    kprintln("Test Aborted! Data was not enqued!");
    }
}

void fill_buffer_and_empty_buffer_test(){
    /*
        Fill buffer and deque them and compare whether they are the same
    */

    // setting the fifo to default
    set_default();

    kprintln("Running : fill_buffer_and_empty_buffer_test");

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
            kprintln("%d = %d",data_,data[i]);
        }else if(data_==-1){

            if(get_status_value()==STATUS_FIFO_EMPTY){
                kprintln("Dequed until buffer is empty");
                kprintln("Test Passed!");
            }else{
                kprintln("Test Failed! An error occured");
            }
            break;
        }else{
            kprintf("Test Failed! Unexpected value received!");
            break;
        }
    }
    kprintln("");
}

void check_for_duplicate_value_enque_and_deque_test(){

    // empty the buffer
    set_default();

    kprintln("Running : check_for_duplicate_value_enque_and_deque_test");

    // check the buffer is empty
    if(get_count()!=0){
    kprintln("Test Failed! Buffer is not empty");
    }

    // enque data value
    int enque_data_value = 10;

    // now enque this data
    enque_data(enque_data_value);

    // now check the status and count
    if(get_status_value()==STATUS_VALID_DATA_FIFO_NOT_FULL && get_count()==1){
    kprintln("First enque operation is successful");
    }else{
    kprintln("Test Failed! Status or Count is not correct");
    }

    // now add this value again
    enque_data(enque_data_value);

    // now check the status and count
    if(get_status_value()==STATUS_VALID_DATA_FIFO_NOT_FULL && get_count()==2){
    kprintln("Second enque operation is successful");
    }else{
    kprintln("Test Failed! Status or Count is not correct");
    }

    // now check the values using deque
    // get the deque data
    int deque_data_ = deque_data();

    // print the message if the correct data is not retrieved
    if(deque_data_== DEQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION || deque_data_ == DEQUE_FAILED_DUE_TO_FIFO_EMPTY ||
        deque_data_==DEQUE_FAILED_DUE_TO_UNKNOWN_ERROR || deque_data_ == DEQUE_FAILED_DUE_TO_COUNTER_NOT_DECREMENTING
    ){

    kprintln("An error occured while retrieving data with Error code : %d",deque_data_);
    }

    // check the deque data, counter , and status
    if(get_status_value()==STATUS_VALID_DATA_FIFO_NOT_FULL && get_count()==1 && deque_data_==enque_data_value){
    kprintln("First deque operation is successful");
    }else{
    kprintln("Test Failed! Status or Count or Deque value is not correct");
    }

    // again deque
    // get the deque data
    deque_data_ = deque_data();

    // print the message if the correct data is not retrieved
    if(deque_data_== DEQUE_FAILED_DUE_TO_WRONG_IMPLEMENTATION || deque_data_ == DEQUE_FAILED_DUE_TO_FIFO_EMPTY ||
        deque_data_==DEQUE_FAILED_DUE_TO_UNKNOWN_ERROR || deque_data_ == DEQUE_FAILED_DUE_TO_COUNTER_NOT_DECREMENTING
    ){

    kprintln("An error occured while retrieving data with Error code : %d",deque_data_);
    }

    // check the deque data, counter , and status
    if(get_status_value()==STATUS_FIFO_EMPTY && get_count()==0 && deque_data_==enque_data_value){
    kprintln("Second deque operation is successful");
    kprintln("Test Passed!");
    }else{
    kprintln("Test Failed! Status or Count or Deque value is not correct");
    }
    
}

void run_basic_test_suit(){

    check_empty_status_test();
    kprintln("");

    check_for_enqueing_function_for_successful_operation_test();
    kprintln("");

    add_one_value_and_check_status_test();
    kprintln("");

    fill_until_full_fifo_and_check_status_test();
    kprintln("");

    check_deque_value_test();
    kprintln("");

    fill_buffer_and_empty_buffer_test();
    kprintln("");

    check_for_duplicate_value_enque_and_deque_test();
    kprintln("");

}

//--------------------------------------------------------------------------------
//                               End : TESTING FUNCTIONS
//--------------------------------------------------------------------------------


int main() {
    uart_init();
    kprintln("Hello this is bootrom!");

    // Print hartid
    kprintln("Hart ID: %ld", read_csr(mhartid));

    // run the tests
    kprintln("Running v4 tests...");

    run_basic_test_suit();
    
    return 0;
}