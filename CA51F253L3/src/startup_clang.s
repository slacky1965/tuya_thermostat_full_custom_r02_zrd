/* Reset/runtime startup for the CA51F253L3 clang (mcs51-llvm) build.
 *
 * Adapted from the toolchain's cc2530_startup.s, trimmed for this chip:
 *   * SP = 127: the hardware stack uses the upper half of internal RAM
 *     (0x80..0xFF); 0x00..0x2F stay free for register banks and the
 *     bit-addressable window backing the __bit globals (0x20..0x2F);
 *   * IRAM 0x01..0xFF is cleared (bit globals, DATA/IDATA window, banks);
 *   * XRAM is NOT touched here: main() starts with xdata_clear() which owns
 *     the whole XRAM initial state (project convention, same as the SDCC
 *     build where the XISEG copy is disabled);
 *   * initialized .data images are not copied either - see ld/ca51f253l3.ld.
 */

.section .vectors,"ax"
.globl reset
.type reset,@function
reset:
  ljmp __mcs51_start
.size reset, .-reset

.section .text.startup,"ax"
.globl __mcs51_start
.type __mcs51_start,@function
__mcs51_start:
  /* Hardware stack above register banks and the bit-address window. */
  mov sp, #127

  /* Clear IRAM 0x01-0xFF; R0 itself ends at 0. */
  mov r0, #0xff
.Lclear_iram:
  mov @r0, #0
  djnz r0, .Lclear_iram

  lcall main
.Lhalt:
  sjmp .Lhalt
.size __mcs51_start, .-__mcs51_start
