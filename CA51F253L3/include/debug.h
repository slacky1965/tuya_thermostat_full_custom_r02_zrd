#ifndef _DEBUG_H_
#define _DEBUG_H_
#include "include/stdint.h"

/* Module debug switches (compile-time).
 *    *_EN        - per-module gates: DBG_BUTTONS/DBG_LED wrappers. A module's
 *                  output is printed ONLY if UART_DEBUG and its *_EN are ON.
 * Set a switch to OFF and the debug prints of that module are compiled
 * out completely (no code, no RAM used).                                          */

#define ON              1
#define OFF             0

#define UART_DEBUG      OFF               /* effective ROM cap for A/B settings    */

#define BUTTONS_EN      ON
#define LED_EN          OFF
#define RAW_EN          OFF
#define TEMP_EN         OFF
#define RTC_EN          OFF
#define LINK_EN         OFF                /* ZT3L link: raw RX/TX packet dump    */
#define LED_SCAN_EN     OFF                /* bench: run Led_CellScan at startup  */
#define FLASH_EN        OFF
#define FLASHER_EN      OFF               /* flasher log during OTA (UART0, P3.0/P3.1) */
#define SETTINGS_EN     OFF
#define TIMER_EN        OFF
/* Bench-only touch helpers (BTN_AnyDown/BTN_Baseline) - they have no call
 * site at all, so they get their own switch instead of riding on BUTTONS_EN. */
#define BTN_BENCH_EN    OFF
/* Temporary bench trace (2026-09-29): while the padlock blink runs, every LED
 * register event prints one char on UART0 - brightness level '0'..'8',
 * backlight channel 'a0'..'a3' (on) / 'A0'..'A3' (off), buffer push '.',
 * buffer clear '=', blink start '!' / end '#'. Used to catch the writer of the
 * "brighter than max" phase. NO line endings on purpose.                    */
#define LOCK_TRACE_EN   OFF

/* Fixed-format temperature log, UART_DEBUG && TEMP_EN (no varargs/printf).   */
void debug_temp(int16_t in_live, int16_t in_acc, int16_t ext_live, int16_t ext_acc);

#if UART_DEBUG
/*************************************************************************/
/* Low-level UART0 debug output (polled TX, P3.0/P3.1, Timer2 baudrate). */

    void debug_init(void);
    void debug_putc(uint8_t c);
    /* fixed-format primitives used by every DEBUG(flag, ...) call site        */
    void debug_puts(const char code *s);
    void debug_i16(int16_t v);
    void debug_dec(uint16_t v);
    void debug_dec2(uint8_t v);
    void debug_kv(const char code *tag, int16_t v);   /* tag + dec + CRLF     */
    void debug_hex(uint16_t v, uint8_t digits);

    /* DEBUG(flag, ...) dispatches on the flag token: expands to <flag>_DEBUG(...),
       whose body prints only when UART_DEBUG and <flag> are both ON and is empty
       otherwise. Calling with any other token is a compile error. */
    #define DEBUG(flag, ...)    flag##_DEBUG(__VA_ARGS__)

    /* Per-flag macros expand the STATEMENT BLOCK passed by the call site, e.g.
       DEBUG(BUTTONS_EN, { debug_puts("lock "); debug_i16(v); }). With the flag
       (or UART_DEBUG) off the block vanishes at preprocessing time, so a
       disabled flag costs no ROM, no string literals and no RAM.              */
    #if UART_DEBUG && BUTTONS_EN
    #define BUTTONS_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define BUTTONS_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && LED_EN
    #define LED_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define LED_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && RAW_EN
    #define RAW_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define RAW_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && TEMP_EN
    #define TEMP_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define TEMP_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && RTC_EN
    #define RTC_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define RTC_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && LINK_EN
    #define LINK_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define LINK_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && LED_SCAN_EN
    #define LED_SCAN_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define LED_SCAN_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && FLASH_EN
    #define FLASH_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define FLASH_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && SETTINGS_EN
    #define SETTINGS_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define SETTINGS_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && TIMER_EN
    #define TIMER_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define TIMER_EN_DEBUG(...)
    #endif

    #if UART_DEBUG && LOCK_TRACE_EN
    #define LOCK_TRACE_EN_DEBUG(...)  do { __VA_ARGS__ } while (0)
    #else
    #define LOCK_TRACE_EN_DEBUG(...)
    #endif

#else
    #define debug_init(...)
    #define DEBUG(flag, ...)
#endif

#endif



