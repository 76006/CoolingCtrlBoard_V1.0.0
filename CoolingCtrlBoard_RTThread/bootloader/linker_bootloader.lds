/* GNU linker script for the 32 KiB CoolingCtrlBoard Bootloader partition. */

MEMORY
{
    /* 最后一个 2 KiB Flash 页独立留给下载器写入 UID 密钥。 */
    ROM     (rx) : ORIGIN = 0x08000000, LENGTH = 30K
    UID_KEY (rx) : ORIGIN = 0x08007800, LENGTH = 2K
    RAM     (rw) : ORIGIN = 0x20000000, LENGTH = 40K
}
ENTRY(Reset_Handler)
_system_stack_size = 0x400;

SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        _stext = .;
        KEEP(*(.isr_vector))
        *(.text) *(.text.*)
        *(.rodata) *(.rodata*)
        *(.glue_7) *(.glue_7t) *(.gnu.linkonce.t*)
        . = ALIGN(4);
        __fsymtab_start = .; KEEP(*(FSymTab)) __fsymtab_end = .;
        __vsymtab_start = .; KEEP(*(VSymTab)) __vsymtab_end = .;
        __rt_init_start = .; KEEP(*(SORT(.rti_fn*))) __rt_init_end = .;
        . = ALIGN(4);
        PROVIDE(__ctors_start__ = .);
        KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array))
        PROVIDE(__ctors_end__ = .);
        _etext = .;
    } > ROM = 0

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > ROM
    __exidx_end = .;

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data) *(.data.*) *(.gnu.linkonce.d*)
        PROVIDE(__dtors_start__ = .);
        KEEP(*(SORT(.dtors.*))) KEEP(*(.dtors))
        PROVIDE(__dtors_end__ = .);
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> ROM
    _sidata = LOADADDR(.data);

    /* 下载器在 Bootloader BIN 的 0x7800 偏移处覆盖这 12 个 0xFF。 */
    .uid_key :
    {
        . = ALIGN(4);
        KEEP(*(.uid_key))
    } > UID_KEY = 0xFF

    ASSERT(ADDR(.uid_key) == 0x08007800, "UID key address changed")
    ASSERT(SIZEOF(.uid_key) == 12, "UID key slot must be 12 bytes")

    .stack :
    {
        . = ALIGN(4);
        _sstack = .;
        . = . + _system_stack_size;
        . = ALIGN(4);
        _estack = .;
    } > RAM

    __bss_start = .;
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss) *(.bss.*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        *(.bss.init)
    } > RAM
    __bss_end = .;
    _end = .;

    .stab 0 : { *(.stab) }
    .stabstr 0 : { *(.stabstr) }
    .comment 0 : { *(.comment) }
    .debug 0 : { *(.debug) }
    .line 0 : { *(.line) }
    .debug_aranges 0 : { *(.debug_aranges) }
    .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_line 0 : { *(.debug_line) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_str 0 : { *(.debug_str) }
    .debug_loc 0 : { *(.debug_loc) }
}
