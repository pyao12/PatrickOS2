ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
}

SECTIONS
{
    . = 0;

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    . = ALIGN(4096);

    .data :
    {
        *(.data*)
        *(.got*)
    } :data

    .dynamic :
    {
        *(.dynamic)
    } :data :dynamic

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
