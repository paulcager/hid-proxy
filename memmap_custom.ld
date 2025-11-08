SECTIONS
{
    .flash_storage 0x101F0000 (NOLOAD) :
    {
        . = ALIGN(4);
        __flash_storage_start = .;
        . = . + 65536;
        __flash_storage_end = .;
    }
}