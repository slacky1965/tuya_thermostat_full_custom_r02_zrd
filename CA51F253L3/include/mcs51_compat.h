/*******************************************************************************/
/* clang (mcs51-llvm) compatibility layer.                                     */
/*                                                                             */
/* Force-included (-include) ONLY by the clang build path of the makefile;     */
/* SDCC never sees this header. The underscored address-space macros           */
/* (__xdata/__code/...) are pre-defined by the mcs51 clang target, and the     */
/* bare `xdata`/`code` aliases live in include/stdint.h (compiler-agnostic).   */
/* What is left for this header:                                               */
/*   * `data`/`idata`/`pdata` locals - clang rejects address-space-qualified   */
/*     automatics ("automatic variable qualified with an address space"), and  */
/*     the only uses in this project are loop counters forced into IRAM for    */
/*     SDCC's model-large; plain locals are correct for clang;                 */
/*   * `bit` - SDCC type, clang only has the address_space(6) attribute, so    */
/*     the macro carries the type;                                             */
/*   * `__interrupt(n)` - SDCC spelling of the clang interrupt attribute.      */
/*******************************************************************************/
#ifndef _MCS51_COMPAT_H_
#define _MCS51_COMPAT_H_

#if defined(__clang__)
#define data
#define idata
#define pdata
/* `bit` must lower to a PLAIN byte, never to address_space(6): clang -Oz
   miscompiles bit-space C variables whose value comes from a load - the load
   gets CSE'd across basic blocks and the store writes a stale ACC value
   (bench 2026-10-10: pwr_arm_power = power_on always stored 0, so the power
   hold never fired; the same class could silently hit prev_power/ext_ok/
   clock_colon). This is the mapping the owner's working clang copy uses.
   SFR bits are unaffected: they come from clang's builtin __sbit, which is
   only used for volatile hardware registers (EA, CY, ...). */
#define bit unsigned char
#define __interrupt(n) __attribute__((interrupt(n)))
#endif

#endif
