OUTPUT_ARCH("riscv")
ENTRY(_prog_start)

SECTIONS
{
  . = 0x80000000;
  
  .text : {
    *(.text.init)
    *(.text .text.*)
  }
  
  .rodata : {
    *(.rodata .rodata.*)
  }
  
  .data : {
    *(.data .data.*)
  }
  
  .bss : {
    *(.bss .bss.*)
    *(COMMON)
  }
  
  .tohost : {
    tohost = .;
    . += 8;
    fromhost = .;
    . += 8;
  }
  
  _end = .;
}
