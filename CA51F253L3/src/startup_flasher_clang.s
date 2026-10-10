/* Reset/runtime startup for the OTA flasher, mcs51-llvm build.
 *
 * The app copies the blob to XRAM 0x0000 and jumps to 0x8000 with
 * MECON.REMAP=1, so the entry lands here (reset is the first thing in
 * .text at 0x8000). Mirrors the app startup: SP above the register banks,
 * initialized .data copied from its CODE load image, .bss cleared, then
 * main(). No interrupt handling - the flash being erased must stay intact.
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
  mov sp, #127

  /* Copy initialized XDATA objects from their CODE load image. */
  mov dptr, #__mcs51_data_start
  mov r2, dpl
  mov r3, dph
  mov dptr, #__mcs51_data_end
  mov r4, dpl
  mov r5, dph
  mov dptr, #__mcs51_data_load
  lcall .Lcopy_initialized_data

  /* Clear zero-initialized XDATA objects before entering C. */
  mov dptr, #__mcs51_bss_start
  mov r2, dpl
  mov r3, dph
  mov dptr, #__mcs51_bss_end
  mov r4, dpl
  mov r5, dph
  lcall .Lclear_xdata
  lcall main
.Lhalt:
  sjmp .Lhalt
.size __mcs51_start, .-__mcs51_start

.Lcopy_initialized_data:
  mov r0, dpl
  mov r1, dph
.Lcopy_data_loop:
  mov a, r2
  xrl a, r4
  jnz .Lcopy_data_byte
  mov a, r3
  xrl a, r5
  jz .Lcopy_data_done
.Lcopy_data_byte:
  mov dpl, r0
  mov dph, r1
  clr a
  movc a, @a+dptr
  mov r6, a
  inc r0
  cjne r0, #0, .Lsource_no_carry
  inc r1
.Lsource_no_carry:
  mov dpl, r2
  mov dph, r3
  mov a, r6
  movx @dptr, a
  inc dptr
  mov r2, dpl
  mov r3, dph
  sjmp .Lcopy_data_loop
.Lcopy_data_done:
  ret

.Lclear_xdata:
  /* check the bounds BEFORE the write so an empty range is a no-op */
  mov a, r2
  xrl a, r4
  jnz .Lclear_byte
  mov a, r3
  xrl a, r5
  jz .Lclear_done
.Lclear_byte:
  mov dpl, r2
  mov dph, r3
  clr a
  movx @dptr, a
  inc dptr
  mov r2, dpl
  mov r3, dph
  sjmp .Lclear_xdata
.Lclear_done:
  ret
