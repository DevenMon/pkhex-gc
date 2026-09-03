/* PKHeX-GC GBA save agent: multiboot image linked into EWRAM. */
ENTRY(_ram_entry)
MEMORY
{
    ewram (rwx) : ORIGIN = 0x020000C0, LENGTH = 0x0003FF40
}
SECTIONS
{
    .text : ALIGN(4)
    {
        *(.text.entry)
        *(.text*)
        *(.rodata*)
    } > ewram

    .data : ALIGN(4) { *(.data*) } > ewram

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ewram

    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.note*) }
}
