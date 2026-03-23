SECTIONS
{
    /DISCARD/ :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        *(.eh_frame* .gcc_except_table*)
        *(.preinit_array*)
        *(.init_array*)
        *(.fini_array*)
        *(.ctors*)
        *(.dtors*)
        *(.jcr*)
    }
}
