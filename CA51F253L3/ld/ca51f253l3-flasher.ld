/* XRAM-resident OTA flasher (src/flasher/flasher.c), mcs51-llvm link.
 * Mirrors the SDCC flasher flags: --code-loc 0x8000 --code-size 2048
 *                                 --xram-loc 0x0600 --xram-size 0x01F0
 * The app copies the whole blob to XRAM 0x0000 and sets MECON.REMAP=1, so
 * every fetch from 0x8000..0x87FF hits XRAM[0x0000..0x07FF]; addresses must
 * stay 0x8000-based. No interrupt vectors live here (the flash being written
 * must stay intact). XRAM 0x07F0 is reserved for SIZE_CELL (bin size
 * handshake). SFR/SBIT absolute symbols and the XSFR register sections come
 * from the generated build/gen/sfr_symbols.ld (flasher.c touches FSCMD,
 * LOCK, MECON and PTSL/PTSH). */
ENTRY(reset)

MEMORY
{
  CODE (rx) : ORIGIN = 0x8000, LENGTH = 0x800
  XRAM (rw) : ORIGIN = 0x0600, LENGTH = 0x01F0
  /* XDATA-mapped registers: only the generated NOLOAD sections land here. */
  XSFR (rw) : ORIGIN = 0x0000, LENGTH = 0x10000
}

SECTIONS
{
  .text 0x8000 :
  {
    *(.vectors)
    *(.text.startup)
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.mcs51.code.rodata .mcs51.code.rodata.*)
  } > CODE

  .data :
  {
    __mcs51_data_start = .;
    *(.data .data.*)
    *(.mcs51.xdata.init .mcs51.xdata.init.*)
    __mcs51_data_end = .;
  } > XRAM AT> CODE
  __mcs51_data_load = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    __mcs51_bss_start = .;
    *(.bss .bss.*)
    *(.mcs51.xdata.bss .mcs51.xdata.bss.*)
    *(COMMON)
    __mcs51_bss_end = .;
  } > XRAM

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }

  /* SFR/SBIT absolute symbols and the XSFR register sections (generated). */
  INCLUDE build/gen/sfr_symbols.ld
}

ASSERT(ADDR(.text) + SIZEOF(.text) <= 0x8800,
       "flasher exceeds the 2 KiB code window at 0x8000")
ASSERT(__mcs51_bss_end <= 0x07F0,
       "flasher XRAM collides with SIZE_CELL at 0x07F0")
ASSERT(ADDR(.text) + SIZEOF(.text) + SIZEOF(.data) <= 0x8600,
       "flasher image exceeds the 1536 B blob window (copy target XRAM 0x0000..0x05FF)")
