SECTIONS
{
  . = 0x400D0000;

  .text : ALIGN(4) { *(.literal .text .literal.* .text.*) }
  .rodata : ALIGN(4) { *(.rodata .rodata.*) }

  .data : ALIGN(4) { *(.data .data.*) }
  .bss : ALIGN(4) { *(.bss .bss.*) }

  .debug_info : { *(.debug_info) }
  .debug_abbrev : { *(.debug_abbrev) }
  .debug_line : { *(.debug_line) }
  .debug_str : { *(.debug_str) }
  .debug_loc : { *(.debug_loc) }
  .debug_ranges : { *(.debug_ranges) }

  /DISCARD/ : { *(.comment) *(.note*) *(.gnu*) }
}
