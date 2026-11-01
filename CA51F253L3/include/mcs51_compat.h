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
/* `bit` maps to REAL bit space: storage in .mcs51_bit* (IRAM 0x20..0x2F),
   `x = !x` -> CPL, `x = 1`/`x = 0` -> SETB/CLR, load/store -> MOV C,bit /
   MOV bit,C - i.e. the SDCC `__bit` semantics. This needs the healed
   mcs51-llvm build (bdf4fc0): earlier builds CSE'd a bit-space load and
   stored a stale ACC value (the power hold never fired) and omitted CPL
   on inversion.
   REMAINING LIMITATION of the healed build: a value-select whose operands
   are two DIFFERENT bit-space loads crashes the backend ("Cannot select:
   SELECT_CMP16") - never write `dst = cond ? bit_a : bit_b;` and never
   merge two different bit variables in the branches of one if/else.
   Single-source selects (`cond ? bit : 1`, `bit ? 1 : 2`), binary ops and
   constant selects are fine; doc_local/temp/test_bit_codegen.py audits
   the sources and locks the codegen contract.
   clang rejects address-space-qualified AUTOMATICS: local flags stay
   bytes (see `ea` in main.c). SFR bits come from clang's builtin __sbit
   (volatile hardware registers) and are unaffected by this macro. */
#define bit unsigned char __attribute__((address_space(6)))
#define __interrupt(n) __attribute__((interrupt(n)))
#endif

#endif
