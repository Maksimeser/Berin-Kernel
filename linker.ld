/* Berin OS - linker script (x86_64, PVH) */
ENTRY(_start_efi)

PHDRS
{
    note   PT_NOTE FLAGS(4);
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;

    .note.Xen : ALIGN(4)
    {
        KEEP(*(.note.Xen))
    } :note :text

    .text : ALIGN(4K)
    {
        *(.text)
    } :text

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    } :rodata

    .data : ALIGN(4K)
    {
        *(.data)
    } :data

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    } :data

    /DISCARD/ :
    {
        *(.note.gnu.property)
        *(.note.gnu.build-id)
        *(.comment)
        *(.eh_frame)
    }

    . = ALIGN(4K);
    _kernel_end = .;
}
