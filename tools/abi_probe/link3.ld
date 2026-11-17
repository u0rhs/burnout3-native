/* probe3 at 0x01000000; the ext_* callees are native entries the test hooks at fixed addresses. */
ext_mix__Fifififififififififif = 0x01200000;
ext_structs__F2I21V2I3if = 0x01200010;
ext_rv__Fif = 0x01200020;
ext_f__Fff = 0x01200030;
SECTIONS {
  . = 0x01000000;
  .text : { *(.text) *(.text.*) *(.rodata*) *(.vtables*) *(.sdata*) *(.data*) *(.sbss*) *(.bss*) }
  /DISCARD/ : { *(.comment) *(.mwcats*) *(.reginfo) *(.mdebug*) *(.pdr) *(.gnu.attributes) }
}
