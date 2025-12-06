#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

/*************************************************************************
    MMIO Device Physical Address and Offsets
*************************************************************************/
#define ISB_BASE_ADDR       0x4000      // Physical base address from device tree
#define ISB_MAP_SIZE        0x1000      // 4KB region

#define GET_ISB_STATUS      0x0         // Offset from base
#define SET_ISB_INPUT_DATA  0x4         // Offset from base
#define GET_ISB_OUTPUT_DATA 0x8         // Offset from base

/*************************************************************************
    Status Values
*************************************************************************/
#define STATUS_WRONG_IMPL                   0
#define STATUS_VALID_DATA_FIFO_FULL         1
#define STATUS_FIFO_EMPTY                   2
#define STATUS_VALID_DATA_FIFO_NOT_FULL     3

/*************************************************************************
    Global pointer to mapped memory region
*************************************************************************/
static volatile uint32_t *isb_base = NULL;

/*************************************************************************
    Initialize MMIO access via /dev/mem
*************************************************************************/
int mmio_init(void) {
    int mem_fd;
    void *mapped_base;
    
    /* Open /dev/mem */
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        perror("Cannot open /dev/mem");
        fprintf(stderr, "Try running with sudo or as root\n");
        return -1;
    }
    
    /* Map the physical address to virtual address space */
    mapped_base = mmap(NULL, 
                      ISB_MAP_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      mem_fd,
                      ISB_BASE_ADDR);
    
    close(mem_fd);  // Can close fd after mmap
    
    if (mapped_base == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }
    
    isb_base = (volatile uint32_t *)mapped_base;
    printf("Successfully mapped ISB device at physical 0x%x to virtual %p\n", 
           ISB_BASE_ADDR, mapped_base);
    
    return 0;
}

/*************************************************************************
    Cleanup MMIO mapping
*************************************************************************/
void mmio_cleanup(void) {
    if (isb_base != NULL) {
        munmap((void *)isb_base, ISB_MAP_SIZE);
        isb_base = NULL;
    }
}

/*************************************************************************
    Read from MMIO register
*************************************************************************/
static inline uint32_t reg_read32(uint32_t offset) {
    return isb_base[offset / 4];
}

/*************************************************************************
    Write to MMIO register
*************************************************************************/
static inline void reg_write32(uint32_t offset, uint32_t value) {
    isb_base[offset / 4] = value;
}

/*************************************************************************
    Print status helper
*************************************************************************/
void print_status(uint8_t status) {
    printf("Status = ");
    if (status == STATUS_WRONG_IMPL) {
        printf("%d : Wrong implementation", status);
    } else if (status == STATUS_VALID_DATA_FIFO_FULL) {
        printf("%d : Deq data is valid, and the buffer is full", status);
    } else if (status == STATUS_FIFO_EMPTY) {
        printf("%d : Buffer is empty and ready to accept data", status);
    } else if (status == STATUS_VALID_DATA_FIFO_NOT_FULL) {
        printf("%d : Buffer is ready to accept data(has space) and also has valid data at output", status);
    }
    printf("\n");
}

/*************************************************************************
    Main program
*************************************************************************/
int main(void) {
    /* Disable output buffering for embedded Linux */
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    
    printf("ISB MMIO Test for Linux\n");
    printf("=======================\n");
    
    /* Initialize MMIO mapping */
    if (mmio_init() != 0) {
        fprintf(stderr, "Failed to initialize MMIO access\n");
        return 1;
    }
    
    /* Read initial status */
    uint8_t status = reg_read32(GET_ISB_STATUS) & 0b11;
    printf("\nInitial ");
    print_status(status);
    
    /* Write data to FIFO */
    printf("\nWriting value 10 to FIFO...\n");
    reg_write32(SET_ISB_INPUT_DATA, 10);
    
    /* Read status after write */
    status = reg_read32(GET_ISB_STATUS) & 0b11;
    printf("After write ");
    print_status(status);
    
    /* Read data from FIFO */
    uint32_t data = reg_read32(GET_ISB_OUTPUT_DATA);
    printf("\nDequeued value: %u\n", data);
    
    /* Read final status */
    status = reg_read32(GET_ISB_STATUS) & 0b11;
    printf("Final ");
    print_status(status);
    
    /* Cleanup */
    mmio_cleanup();
    
    printf("\nTest completed successfully\n");
    return 0;
}
