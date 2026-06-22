/*==============================================================================
 * ARES OS - Kernel Linker Script
 *==============================================================================
 * Defines the memory layout of the kernel binary.
 * The kernel is linked to run at physical address 0x100000 (1MB).
 *
 * Memory layout:
 *   0x100000 - 0x10000F : Kernel signature + entry jmp
 *   0x100010 - 0x10XXXX : .text (executable code)
 *   0x10XXXX - 0x10YYYY : .rodata (read-only data)
 *   0x10YYYY - 0x10ZZZZ : .data (initialized data)
 *   0x10ZZZZ - 0x10WWWW : .bss (zero-initialized data)
 *==============================================================================*/

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);   /* Read + Execute */
    data    PT_LOAD FLAGS(6);   /* Read + Write */
}

SECTIONS
{
    /* Kernel is loaded at 1MB physical */
    . = 0x100000;

    /*------------------------------------------------------------------------*/
    /* Text section: executable code                                           */
    /*------------------------------------------------------------------------*/
    .text : ALIGN(4K)
    {
        *(.text.init)          /* Boot entry point (boot.asm) */
        *(.text)
        *(.text.*)
    } :text

    /*------------------------------------------------------------------------*/
    /* Read-only data section                                                  */
    /*------------------------------------------------------------------------*/
    .rodata : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    } :text

    /*------------------------------------------------------------------------*/
    /* Initialized data section                                                */
    /*------------------------------------------------------------------------*/
    .data : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    } :data

    /*------------------------------------------------------------------------*/
    /* BSS section: zero-initialized data                                      */
    /*------------------------------------------------------------------------*/
    .bss : ALIGN(4K)
    {
        _kernel_bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(8);
        _kernel_bss_end = .;
    } :data

    /*------------------------------------------------------------------------*/
    /* Symbols for kernel usage                                                */
    /*------------------------------------------------------------------------*/
    _kernel_start = 0x100000;
    _kernel_end = .;
    _kernel_size = _kernel_end - _kernel_start;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.comment.*)
        *(.note)
        *(.note.*)
        *(.debug)
        *(.debug.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.stab)
        *(.stabstr)
    }
}
