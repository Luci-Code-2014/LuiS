OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text.entry)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;
}