/* Rovari CH32V203G6U User App Linker Script (Bootloader Mode)
 *
 * Flash layout (32KB zero-wait + 192KB extended = 224KB total):
 *   0x00000000 - 0x00004FFF  Bootloader (20KB, never modified over USB)
 *   0x00005000 - 0x00007FFF  User app fast region (12KB, zero-wait)
 *   0x00008000 - 0x00037FFF  User app slow region (192KB, extended flash)
 *
 * RAM: 10KB at 0x20000000 - 0x200027FF
 *   0x20000400 is reserved for bootloader magic word (do not use as variable)
 *
 * USB CDC interrupt handlers MUST be in zero-wait region (first 12KB of user code).
 * Code beyond 12KB spills into extended flash automatically (~10x slower fetch).
 */

ENTRY( _start )

__stack_size = 2048;
PROVIDE( _stack_size = __stack_size );

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00005000, LENGTH = 204K
    RAM  (xrw) : ORIGIN = 0x20000000, LENGTH = 10K
}

SECTIONS
{
    .init :
    {
        _sinit = .;
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
        _einit = .;
    } >FLASH AT>FLASH

    .vector :
    {
        *(.vector);
        . = ALIGN(64);
    } >FLASH AT>FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
    } >FLASH AT>FLASH

    .fini :
    {
        KEEP(*(SORT_NONE(.fini)))
        . = ALIGN(4);
    } >FLASH AT>FLASH

    PROVIDE( _etext = . );
    PROVIDE( _eitcm = . );

    .preinit_array :
    {
      PROVIDE_HIDDEN (__preinit_array_start = .);
      KEEP (*(.preinit_array))
      PROVIDE_HIDDEN (__preinit_array_end = .);
    } >FLASH AT>FLASH

    .init_array :
    {
      PROVIDE_HIDDEN (__init_array_start = .);
      KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
      KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
      PROVIDE_HIDDEN (__init_array_end = .);
    } >FLASH AT>FLASH

    .fini_array :
    {
      PROVIDE_HIDDEN (__fini_array_start = .);
      KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
      KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
      PROVIDE_HIDDEN (__fini_array_end = .);
    } >FLASH AT>FLASH

    .ctors :
    {
      KEEP (*crtbegin.o(.ctors))
      KEEP (*crtbegin?.o(.ctors))
      KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
      KEEP (*(SORT(.ctors.*)))
      KEEP (*(.ctors))
    } >FLASH AT>FLASH

    .dtors :
    {
      KEEP (*crtbegin.o(.dtors))
      KEEP (*crtbegin?.o(.dtors))
      KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
      KEEP (*(SORT(.dtors.*)))
      KEEP (*(.dtors))
    } >FLASH AT>FLASH

    .dalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
    } >RAM AT>FLASH

    .dlalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_lma = .);
    } >FLASH AT>FLASH

    .data :
    {
        *(.gnu.linkonce.r.*)
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE( __global_pointer$ = . + 0x800 );
        *(.sdata .sdata.*)
        *(.sdata2.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        *(.srodata.cst16)
        *(.srodata.cst8)
        *(.srodata.cst4)
        *(.srodata.cst2)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        PROVIDE( _edata = .);
    } >RAM AT>FLASH

    .bss :
    {
        . = ALIGN(4);
        PROVIDE( _sbss = .);
        *(.sbss*)
        *(.gnu.linkonce.sb.*)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON*)
        . = ALIGN(4);
        PROVIDE( _ebss = .);
    } >RAM AT>FLASH

    PROVIDE( _end = _ebss);
    PROVIDE( end = . );

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
    {
        PROVIDE( _heap_end = . );
        . = ALIGN(4);
        PROVIDE(_susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = .);
    } >RAM
}
