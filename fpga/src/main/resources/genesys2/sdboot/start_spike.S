.section .text.init
.globl _prog_start
_prog_start:
    # Set up stack pointer
    la sp, _end
    li t0, 4096
    add sp, sp, t0
    
    # Clear BSS
    la t0, __bss_start
    la t1, _end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    # Call main
    call main
    
    # Exit (should not reach here)
3:
    wfi
    j 3b

.bss
.globl __bss_start
__bss_start:
